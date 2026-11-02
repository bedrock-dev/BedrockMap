#ifndef BEDROCKMAP_IO_BROWSERVFS_H
#define BEDROCKMAP_IO_BROWSERVFS_H

#include "io/vfs.h"

#ifdef Q_OS_WASM
namespace io {
    /// Accesses Qt native browser file handles without converting them to local paths.
    class BrowserVfsBackend final : public Vfs {
       public:
        using SelectionCallback = std::function<void(const QUrl&, const QString&)>;
        static void selectDirectory(SelectionCallback callback);
        static void registerWritable(const QUrl& url);
        [[nodiscard]] bool requiresSaveAs(const QUrl& url) const override;
        [[nodiscard]] bool supports(const QUrl& url) const;
        [[nodiscard]] OpenReadResult openRead(const QUrl& url) const override;
        [[nodiscard]] ReadResult readAll(const QUrl& url, qint64 max_bytes = -1) const override;
        [[nodiscard]] QString readStream(const QUrl& url, const StreamReader& reader) const override;
        [[nodiscard]] QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options = {}) const override;
        [[nodiscard]] QString displayName(const QUrl& url) const override;

    };
}  // namespace io
#endif

#endif  // BEDROCKMAP_IO_BROWSERVFS_H
