#include "io/documentio.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QImageWriter>

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

    QString DocumentIo::writePng(const QUrl& url, const QImage& image) {
        if (url.isEmpty() || image.isNull()) return QCoreApplication::translate("DocumentIo", "The image or destination is invalid.");
        return writeStream(url, [&image](QIODevice& device) {
            QImageWriter writer(&device, "PNG");
            return writer.write(image) ? QString() : writer.errorString();
        });
    }

    bool DocumentIo::hasZipSignature(const QUrl& url) {
        QByteArray header;
        const QString error = readStream(url, [&header](QIODevice& device) {
            header = device.read(4);
            return QString{};
        });
        if (!error.isEmpty()) return false;
        return header.startsWith(QByteArrayLiteral("PK\x03\x04")) || header.startsWith(QByteArrayLiteral("PK\x05\x06")) ||
               header.startsWith(QByteArrayLiteral("PK\x07\x08"));
    }

    bool DocumentIo::isDirectoryEmpty(const QUrl& url) {
        const auto entries = list(url);
        return entries.ok() && entries.names.isEmpty();
    }

    QString DocumentIo::displayName(const QUrl& url) { return fileSystem().displayName(url); }

    bool DocumentIo::requiresSaveAs(const QUrl& url) { return url.isEmpty() || fileSystem().requiresSaveAs(url); }

    QString DocumentIo::identifier(const QUrl& url) {
        if (url.isEmpty()) return {};
        const QString text = url.toString(QUrl::FullyEncoded);
#ifdef Q_OS_WIN
        const bool drivePath = text.size() >= 2 && text.at(0).isLetter() && text.at(1) == QLatin1Char(':');
#else
        const bool drivePath = false;
#endif
        if (!url.isLocalFile() && !url.scheme().isEmpty() && !drivePath) return text;
        return io::canonicalLocalPath(url);
    }

    QString DocumentIo::canonicalLocalPath(const QString& input) { return io::canonicalLocalPath(input); }

    QString DocumentIo::canonicalLocalPath(const QUrl& url) { return io::canonicalLocalPath(url); }

    QString DocumentIo::copyDirectory(const QUrl& source, const QUrl& destination) {
        return fileSystem().copyDirectory(source, destination);
    }

    QString DocumentIo::resolveNativeDirectory(const QUrl& source) { return fileSystem().resolveNativeDirectory(source); }

    StatResult DocumentIo::stat(const QUrl& url) { return fileSystem().stat(url); }

    ListResult DocumentIo::list(const QUrl& url) { return fileSystem().list(url); }

    QString DocumentIo::makeDirectory(const QUrl& url, bool recursive) { return fileSystem().makeDirectory(url, recursive); }

    QString DocumentIo::remove(const QUrl& url, bool recursive) { return fileSystem().remove(url, recursive); }

    QString DocumentIo::rename(const QUrl& from, const QUrl& to) { return fileSystem().rename(from, to); }

    NativeFileResult DocumentIo::openNative(const QUrl& url, NativeOpen flags, int mode) {
        return fileSystem().openNative(url, flags, mode);
    }

}  // namespace io
