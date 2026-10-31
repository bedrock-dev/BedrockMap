#include <QFile>
#include <exception>

#include "base/platform.h"
#include "io/vfs.h"

namespace io {
    OpenReadResult ContentDocumentVfsBackend::openRead(const QUrl& url) const {
        if (!platform::isContentUri(url)) return {{}, QStringLiteral("The URI is not a content document.")};
        auto file = std::make_unique<QFile>(url.toString(QUrl::FullyEncoded));
        if (const QString error = platform::openDocumentFile(*file, QIODevice::ReadOnly); !error.isEmpty()) return {{}, error};
        return {std::move(file), {}};
    }

    ReadResult ContentDocumentVfsBackend::readAll(const QUrl& url, qint64 max_bytes) const {
        auto opened = openRead(url);
        if (!opened.ok()) return {{}, opened.error};
        if (max_bytes >= 0 && opened.file->size() > max_bytes) return {{}, QStringLiteral("The file is too large.")};
        const QByteArray data = opened.file->readAll();
        if (opened.file->error() != QFileDevice::NoError) return {{}, opened.file->errorString()};
        return {data, {}};
    }

    QString ContentDocumentVfsBackend::readStream(const QUrl& url, const StreamReader& reader) const {
        if (!platform::isContentUri(url)) return QStringLiteral("The URI is not a content document.");
        if (!reader) return QStringLiteral("No file reader was provided.");
        auto opened = openRead(url);
        if (!opened.ok()) return opened.error;
        try {
            return reader(*opened.file);
        } catch (const std::exception& error) {
            return QString::fromUtf8(error.what());
        } catch (...) {
            return QStringLiteral("The file reader failed.");
        }
    }

    QString ContentDocumentVfsBackend::writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions&) const {
        if (!platform::isContentUri(url)) return QStringLiteral("The URI is not a content document.");
        if (!writer) return QStringLiteral("No file writer was provided.");

        QFile file(url.toString(QUrl::FullyEncoded));
        if (const QString error = platform::openDocumentFile(file, QIODevice::WriteOnly | QIODevice::Truncate); !error.isEmpty())
            return error;
        try {
            if (const QString error = writer(file); !error.isEmpty()) return error;
        } catch (const std::exception& error) {
            return QString::fromUtf8(error.what());
        } catch (...) {
            return QStringLiteral("The file writer failed.");
        }
        if (!file.flush()) return file.errorString();
        file.close();
        return file.error() == QFileDevice::NoError ? QString() : file.errorString();
    }

    QString ContentDocumentVfsBackend::displayName(const QUrl& url) const {
        return platform::fileDisplayName(url.toString(QUrl::FullyEncoded));
    }
}  // namespace io
