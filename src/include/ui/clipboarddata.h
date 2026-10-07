#ifndef BEDROCKMAP_CLIPBOARDDATA_H
#define BEDROCKMAP_CLIPBOARDDATA_H

#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QStringList>
#include <QUrl>

namespace clipboard_data {
    inline constexpr char MCSTRUCTURE_MIME_TYPE[] = "application/x-mcstructure";
    inline constexpr char CHUNK_REGION_MIME_TYPE[] = "application/x-bedrockmap-region";

    inline QByteArray read(const QMimeData* mimeData, const char* mimeType, const QStringList& fileExtensions) {
        if (!mimeData) return {};

        const auto raw = mimeData->data(QString::fromLatin1(mimeType));
        if (!raw.isEmpty()) return raw;

        for (const auto& url : mimeData->urls()) {
            if (!url.isLocalFile()) continue;

            const QFileInfo fileInfo(url.toLocalFile());
            if (!fileInfo.isFile() || !fileExtensions.contains(fileInfo.suffix(), Qt::CaseInsensitive)) continue;

            QFile file(fileInfo.filePath());
            if (!file.open(QIODevice::ReadOnly)) continue;

            const auto fileData = file.readAll();
            if (!fileData.isEmpty()) return fileData;
        }
        return {};
    }

    inline void write(QMimeData& mimeData, const char* mimeType, const QByteArray& raw) {
        mimeData.setData(QString::fromLatin1(mimeType), raw);
    }
}  // namespace clipboard_data

#endif  // BEDROCKMAP_CLIPBOARDDATA_H
