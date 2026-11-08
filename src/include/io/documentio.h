#ifndef BEDROCKMAP_IO_DOCUMENTIO_H
#define BEDROCKMAP_IO_DOCUMENTIO_H

#include "io/vfs.h"

namespace io {
    /// URI-aware file and directory operations for every external, non-LevelDB path.
    class DocumentIo final {
       public:
        DocumentIo() = delete;

        [[nodiscard]] static OpenReadResult openRead(const QUrl& url);
        [[nodiscard]] static ReadResult readAll(const QUrl& url, qint64 max_bytes = -1);
        [[nodiscard]] static QString readStream(const QUrl& url, const StreamReader& reader);
        [[nodiscard]] static QString writeBytes(const QUrl& url, const QByteArray& bytes, const WriteOptions& options = {});
        [[nodiscard]] static QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options = {});
        [[nodiscard]] static QString displayName(const QUrl& url);
        [[nodiscard]] static bool requiresSaveAs(const QUrl& url);
        [[nodiscard]] static QString identifier(const QUrl& url);
        [[nodiscard]] static QString copyTree(const QUrl& source, const QUrl& destination);
        [[nodiscard]] static QString resolveNativeDirectory(const QUrl& source);
        [[nodiscard]] static StatResult stat(const QUrl& url);
        [[nodiscard]] static ListResult list(const QUrl& url);
        [[nodiscard]] static QString makeDirectory(const QUrl& url, bool recursive = true);
        [[nodiscard]] static QString remove(const QUrl& url);
        [[nodiscard]] static QString rename(const QUrl& from, const QUrl& to);
        [[nodiscard]] static NativeFileResult openNative(const QUrl& url, NativeOpen flags, int mode = 0644);
    };
}  // namespace io

#endif  // BEDROCKMAP_IO_DOCUMENTIO_H
