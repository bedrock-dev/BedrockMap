#include "base/clipboarddata.h"

#include <QFileInfo>

#include "io/documentio.h"

namespace clipboard_data {
    QByteArray read(const QMimeData* mimeData, const char* mimeType, const QStringList& fileExtensions) {
        if (!mimeData) return {};

        const auto raw = mimeData->data(QString::fromLatin1(mimeType));
        if (!raw.isEmpty()) return raw;

        for (const auto& url : mimeData->urls()) {
            if (!url.isLocalFile() && url.scheme() != QStringLiteral("content")) continue;
            const QString name = io::DocumentIo::displayName(url);
            if (!fileExtensions.contains(QFileInfo(name).suffix(), Qt::CaseInsensitive)) continue;
            const auto fileData = io::DocumentIo::readAll(url);
            if (fileData.ok() && !fileData.data.isEmpty()) return fileData.data;
        }
        return {};
    }
}  // namespace clipboard_data
