#ifndef BEDROCKMAP_IO_VFS_H
#define BEDROCKMAP_IO_VFS_H

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QUrl>
#include <functional>
#include <memory>

namespace io {
    struct ReadResult {
        QByteArray data;
        QString error;

        [[nodiscard]] bool ok() const { return error.isEmpty(); }
    };

    struct OpenReadResult {
        std::unique_ptr<QFile> file;
        QString error;

        [[nodiscard]] bool ok() const { return file != nullptr && error.isEmpty(); }
    };

    struct WriteOptions {
        bool createParentDirectories{true};
    };

    /// Return an empty string on success, or a user-facing error.
    using StreamReader = std::function<QString(QIODevice& device)>;
    using StreamWriter = std::function<QString(QIODevice& device)>;

    /// Minimal file abstraction currently needed by user-facing documents.
    class Vfs {
       public:
        virtual ~Vfs() = default;

        [[nodiscard]] virtual OpenReadResult openRead(const QUrl& url) const = 0;
        [[nodiscard]] virtual ReadResult readAll(const QUrl& url, qint64 max_bytes = -1) const = 0;
        [[nodiscard]] virtual QString readStream(const QUrl& url, const StreamReader& reader) const = 0;
        [[nodiscard]] virtual QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options = {}) const = 0;
        [[nodiscard]] virtual QString displayName(const QUrl& url) const = 0;
        [[nodiscard]] virtual QString copyTree(const QUrl& source, const QUrl& destination) const;
        [[nodiscard]] virtual QString resolveNativeDirectory(const QUrl& source) const;
    };

    class LocalVfsBackend final : public Vfs {
       public:
        [[nodiscard]] bool supports(const QUrl& url) const;
        [[nodiscard]] OpenReadResult openRead(const QUrl& url) const override;
        [[nodiscard]] ReadResult readAll(const QUrl& url, qint64 max_bytes = -1) const override;
        [[nodiscard]] QString readStream(const QUrl& url, const StreamReader& reader) const override;
        [[nodiscard]] QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options = {}) const override;
        [[nodiscard]] QString displayName(const QUrl& url) const override;

       private:
        [[nodiscard]] static QString resolveLocalPath(const QUrl& url);
    };

    class ContentDocumentVfsBackend final : public Vfs {
       public:
        [[nodiscard]] OpenReadResult openRead(const QUrl& url) const override;
        [[nodiscard]] ReadResult readAll(const QUrl& url, qint64 max_bytes = -1) const override;
        [[nodiscard]] QString readStream(const QUrl& url, const StreamReader& reader) const override;
        [[nodiscard]] QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options = {}) const override;
        [[nodiscard]] QString displayName(const QUrl& url) const override;
    };

    class SafTreeVfsBackend final : public Vfs {
       public:
        [[nodiscard]] bool supports(const QUrl& url) const;
        [[nodiscard]] OpenReadResult openRead(const QUrl& url) const override;
        [[nodiscard]] ReadResult readAll(const QUrl& url, qint64 max_bytes = -1) const override;
        [[nodiscard]] QString readStream(const QUrl& url, const StreamReader& reader) const override;
        [[nodiscard]] QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options = {}) const override;
        [[nodiscard]] QString displayName(const QUrl& url) const override;
        [[nodiscard]] QString copyTree(const QUrl& source, const QUrl& destination) const override;
        [[nodiscard]] QString resolveNativeDirectory(const QUrl& source) const override;
    };

    /// Shared URI-aware backend used by DocumentIo.
    [[nodiscard]] Vfs& fileSystem();
}  // namespace io

#endif  // BEDROCKMAP_IO_VFS_H
