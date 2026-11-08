#include <QCoreApplication>
#include <QFile>
#include <QTemporaryFile>
#include <exception>

#include "base/platform.h"
#include "io/vfs.h"

namespace io {
    OpenReadResult ContentDocumentVfsBackend::openRead(const QUrl& url) const {
        if (!platform::isContentUri(url)) return {{}, QCoreApplication::translate("Vfs", "The URI is not a content document.")};
        auto file = std::make_unique<QFile>(url.toString(QUrl::FullyEncoded));
        if (const QString error = platform::openDocumentFile(*file, QIODevice::ReadOnly); !error.isEmpty()) return {{}, error};
        return {std::move(file), {}};
    }

    QString ContentDocumentVfsBackend::writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions&) const {
        if (!platform::isContentUri(url)) return QCoreApplication::translate("Vfs", "The URI is not a content document.");
        if (!writer) return QCoreApplication::translate("Vfs", "No file writer was provided.");

        // Providers cannot replace a document atomically. Serialize completely
        // first, so a failing writer never truncates the existing document.
        QTemporaryFile staging;
        if (!staging.open()) return staging.errorString();
        try {
            if (const QString error = writer(staging); !error.isEmpty()) return error;
        } catch (const std::exception& error) {
            return QString::fromUtf8(error.what());
        } catch (...) {
            return QCoreApplication::translate("Vfs", "The file writer failed.");
        }
        if (!staging.flush() || !staging.seek(0)) return staging.errorString();

        QFile file(url.toString(QUrl::FullyEncoded));
        if (const QString error = platform::openDocumentFile(file, QIODevice::WriteOnly | QIODevice::Truncate); !error.isEmpty())
            return error;
        while (!staging.atEnd()) {
            const QByteArray block = staging.read(256 * 1024);
            if (staging.error() != QFileDevice::NoError) return staging.errorString();
            if (file.write(block) != block.size()) return file.errorString();
        }
        if (!file.flush()) return file.errorString();
        file.close();
        return file.error() == QFileDevice::NoError ? QString() : file.errorString();
    }

    QString ContentDocumentVfsBackend::displayName(const QUrl& url) const {
        return platform::fileDisplayName(url.toString(QUrl::FullyEncoded));
    }
}  // namespace io
