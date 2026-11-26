#ifndef BEDROCKMAP_IO_ANDROIDCONTENTIO_H
#define BEDROCKMAP_IO_ANDROIDCONTENTIO_H

#include <QFile>
#include <QString>
#include <QUrl>

namespace io::detail {
    /// Opens a provider document on Android, or an ordinary QFile elsewhere.
    [[nodiscard]] QString openContentDocument(QFile& file, QIODevice::OpenMode mode);

    /// Returns the provider display name, with a URI/local fallback.
    [[nodiscard]] QString contentDisplayName(const QUrl& url);

    /// Resolves an external-storage SAF tree to a native path when permitted.
    [[nodiscard]] QString resolveContentTreePath(const QUrl& url);
}  // namespace io::detail

#endif  // BEDROCKMAP_IO_ANDROIDCONTENTIO_H
