#ifndef BEDROCKMAP_IO_VFS_H
#define BEDROCKMAP_IO_VFS_H

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <cstdint>
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

    struct FileInfo {
        bool exists{false};
        bool isFile{false};
        bool isDirectory{false};
        bool isLink{false};
        std::uint64_t size{0};
        /// Seconds since the Unix epoch.
        std::int64_t modified{0};
    };

    /// A missing entry is a successful result with info.exists == false.
    struct StatResult {
        FileInfo info;
        QString error;

        [[nodiscard]] bool ok() const { return error.isEmpty(); }
    };

    struct ListResult {
        /// Entry names without "." and "..", sorted by name.
        QStringList names;
        QString error;

        [[nodiscard]] bool ok() const { return error.isEmpty(); }
    };

    /// Portable open flags for descriptor-level access, e.g. a future LevelDB Env.
    enum class NativeOpen : unsigned {
        Read = 1u << 0,
        Write = 1u << 1,
        Create = 1u << 2,
        Exclusive = 1u << 3,
        Truncate = 1u << 4,
        Append = 1u << 5,
    };
    [[nodiscard]] constexpr NativeOpen operator|(NativeOpen a, NativeOpen b) {
        return static_cast<NativeOpen>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }
    [[nodiscard]] constexpr bool hasFlag(NativeOpen flags, NativeOpen flag) {
        return (static_cast<unsigned>(flags) & static_cast<unsigned>(flag)) != 0;
    }

    /// An owned POSIX/CRT file descriptor. The caller must close it.
    struct NativeFileResult {
        int descriptor{-1};
        QString error;

        [[nodiscard]] bool ok() const { return descriptor >= 0 && error.isEmpty(); }
    };

    struct WriteOptions {
        bool createParentDirectories{true};
    };

    /// Return an empty string on success, or a user-facing error.
    using StreamReader = std::function<QString(QIODevice& device)>;
    using StreamWriter = std::function<QString(QIODevice& device)>;

    /// File abstraction for every external, non-LevelDB file and directory.
    /// Directory operations default to "not supported" for document providers.
    class Vfs {
       public:
        virtual ~Vfs() = default;

        [[nodiscard]] virtual OpenReadResult openRead(const QUrl& url) const = 0;
        [[nodiscard]] virtual ReadResult readAll(const QUrl& url, qint64 max_bytes = -1) const;
        [[nodiscard]] virtual QString readStream(const QUrl& url, const StreamReader& reader) const;
        /// Implementations must leave an existing file unchanged when the writer fails.
        [[nodiscard]] virtual QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options = {}) const = 0;
        [[nodiscard]] virtual QString displayName(const QUrl& url) const = 0;
        [[nodiscard]] virtual bool requiresSaveAs(const QUrl&) const { return false; }
        [[nodiscard]] virtual QString copyTree(const QUrl& source, const QUrl& destination) const;
        [[nodiscard]] virtual QString resolveNativeDirectory(const QUrl& source) const;

        /// Does not follow a final symbolic link.
        [[nodiscard]] virtual StatResult stat(const QUrl& url) const;
        [[nodiscard]] virtual ListResult list(const QUrl& url) const;
        /// Succeeds when the directory already exists.
        [[nodiscard]] virtual QString makeDirectory(const QUrl& url, bool recursive = true) const;
        /// Removes a file, link or empty directory.
        [[nodiscard]] virtual QString remove(const QUrl& url) const;
        /// Replaces an existing destination file atomically where the backend allows it.
        [[nodiscard]] virtual QString rename(const QUrl& from, const QUrl& to) const;
        [[nodiscard]] virtual NativeFileResult openNative(const QUrl& url, NativeOpen flags, int mode = 0644) const;
    };

    class LocalVfsBackend final : public Vfs {
       public:
        [[nodiscard]] bool supports(const QUrl& url) const;
        [[nodiscard]] OpenReadResult openRead(const QUrl& url) const override;
        [[nodiscard]] QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options = {}) const override;
        [[nodiscard]] QString displayName(const QUrl& url) const override;
        [[nodiscard]] QString resolveNativeDirectory(const QUrl& source) const override;
        [[nodiscard]] StatResult stat(const QUrl& url) const override;
        [[nodiscard]] ListResult list(const QUrl& url) const override;
        [[nodiscard]] QString makeDirectory(const QUrl& url, bool recursive = true) const override;
        [[nodiscard]] QString remove(const QUrl& url) const override;
        [[nodiscard]] QString rename(const QUrl& from, const QUrl& to) const override;
        [[nodiscard]] NativeFileResult openNative(const QUrl& url, NativeOpen flags, int mode = 0644) const override;

        /// Clean local path for a file URL or plain path; empty for other URLs.
        [[nodiscard]] static QString resolveLocalPath(const QUrl& url);
    };

    class ContentDocumentVfsBackend final : public Vfs {
       public:
        [[nodiscard]] OpenReadResult openRead(const QUrl& url) const override;
        [[nodiscard]] QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options = {}) const override;
        [[nodiscard]] QString displayName(const QUrl& url) const override;
    };

    class SafTreeVfsBackend final : public Vfs {
       public:
        [[nodiscard]] bool supports(const QUrl& url) const;
        [[nodiscard]] OpenReadResult openRead(const QUrl& url) const override;
        [[nodiscard]] QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options = {}) const override;
        [[nodiscard]] QString displayName(const QUrl& url) const override;
        [[nodiscard]] QString copyTree(const QUrl& source, const QUrl& destination) const override;
        [[nodiscard]] QString resolveNativeDirectory(const QUrl& source) const override;
    };

    /// Local paths that ordinary app access cannot reach on this platform
    /// (Android/data and other apps' private data). Such paths use the
    /// privileged backend when Shizuku or root is authorized.
    [[nodiscard]] bool isPrivilegedPath(const QString& path);

    /// Shared URI-aware router used by DocumentIo.
    [[nodiscard]] Vfs& fileSystem();
}  // namespace io

#endif  // BEDROCKMAP_IO_VFS_H
