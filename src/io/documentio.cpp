#include "io/documentio.h"

#include <QDir>
#include <QFileInfo>

namespace io {
    OpenReadResult DocumentIo::openRead(const QUrl& url) { return fileSystem().openRead(url); }

    ReadResult DocumentIo::readAll(const QUrl& url, qint64 max_bytes) { return fileSystem().readAll(url, max_bytes); }

    QString DocumentIo::readStream(const QUrl& url, const StreamReader& reader) { return fileSystem().readStream(url, reader); }

    QString DocumentIo::writeBytes(const QUrl& url, const QByteArray& bytes, const WriteOptions& options) {
        return fileSystem().writeStream(
            url, [&bytes](QIODevice& device) { return device.write(bytes) == bytes.size() ? QString() : device.errorString(); }, options);
    }

    QString DocumentIo::writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options) {
        return fileSystem().writeStream(url, writer, options);
    }

    QString DocumentIo::displayName(const QUrl& url) { return fileSystem().displayName(url); }

    QString DocumentIo::identifier(const QUrl& url) {
        if (url.isEmpty()) return {};
        const QString text = url.toString(QUrl::FullyEncoded);
#ifdef Q_OS_WIN
        const bool drivePath = text.size() >= 2 && text.at(0).isLetter() && text.at(1) == QLatin1Char(':');
#else
        const bool drivePath = false;
#endif
        if (!url.isLocalFile() && !url.scheme().isEmpty() && !drivePath) return text;
        const QString localPath = url.isLocalFile() ? url.toLocalFile() : text;
        const QString path = QDir::cleanPath(localPath);
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
    }

    QString DocumentIo::copyTree(const QUrl& source, const QUrl& destination) { return fileSystem().copyTree(source, destination); }

    QString DocumentIo::resolveNativeDirectory(const QUrl& source) { return fileSystem().resolveNativeDirectory(source); }
}  // namespace io
