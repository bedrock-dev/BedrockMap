#ifndef BEDROCKMAP_IO_DOCUMENTIO_H
#define BEDROCKMAP_IO_DOCUMENTIO_H

#include "io/vfs.h"

namespace io {
    /// URI-aware file operations for every non-LevelDB document path.
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
    };
}  // namespace io

#endif  // BEDROCKMAP_IO_DOCUMENTIO_H
