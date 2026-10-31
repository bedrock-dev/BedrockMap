#include "io/vfs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <exception>

namespace io {
    namespace {
        QUrl localUrl(const QUrl& url) {
            if (url.isLocalFile()) return url;
            const QString text = url.toString();
#ifdef Q_OS_WIN
            const bool drivePath = text.size() >= 2 && text.at(0).isLetter() && text.at(1) == QLatin1Char(':');
#else
            const bool drivePath = false;
#endif
            if (url.scheme().isEmpty() || drivePath) return QUrl::fromLocalFile(text);
            return {};
        }

        class VfsRouter final : public Vfs {
           public:
            OpenReadResult openRead(const QUrl& url) const override {
                if (local_.supports(url)) return local_.openRead(url);
                return saf_tree_.supports(url) ? saf_tree_.openRead(url) : content_.openRead(url);
            }

            ReadResult readAll(const QUrl& url, qint64 max_bytes) const override {
                if (local_.supports(url)) return local_.readAll(url, max_bytes);
                return saf_tree_.supports(url) ? saf_tree_.readAll(url, max_bytes) : content_.readAll(url, max_bytes);
            }

            QString readStream(const QUrl& url, const StreamReader& reader) const override {
                if (local_.supports(url)) return local_.readStream(url, reader);
                return saf_tree_.supports(url) ? saf_tree_.readStream(url, reader) : content_.readStream(url, reader);
            }

            QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options) const override {
                if (local_.supports(url)) return local_.writeStream(url, writer, options);
                return saf_tree_.supports(url) ? saf_tree_.writeStream(url, writer, options) : content_.writeStream(url, writer, options);
            }

            QString displayName(const QUrl& url) const override {
                if (local_.supports(url)) return local_.displayName(url);
                return saf_tree_.supports(url) ? saf_tree_.displayName(url) : content_.displayName(url);
            }

            QString copyTree(const QUrl& source, const QUrl& destination) const override {
                return saf_tree_.supports(source) ? saf_tree_.copyTree(source, destination) : Vfs::copyTree(source, destination);
            }

            QString resolveNativeDirectory(const QUrl& source) const override {
                if (local_.supports(source)) return local_.resolveNativeDirectory(source);
                return saf_tree_.supports(source) ? saf_tree_.resolveNativeDirectory(source) : QString{};
            }

           private:
            LocalVfsBackend local_;
            ContentDocumentVfsBackend content_;
            SafTreeVfsBackend saf_tree_;
        };
    }  // namespace

    QString Vfs::copyTree(const QUrl&, const QUrl&) const { return QStringLiteral("Directory copy is not supported for this URI."); }

    QString Vfs::resolveNativeDirectory(const QUrl&) const { return {}; }

    QString LocalVfsBackend::resolveLocalPath(const QUrl& url) {
        const QUrl local = localUrl(url);
        if (!local.isValid() || !local.isLocalFile() || local.hasQuery() || local.hasFragment()) return {};
        return QDir::cleanPath(local.toLocalFile());
    }

    bool LocalVfsBackend::supports(const QUrl& url) const { return !resolveLocalPath(url).isEmpty(); }

    OpenReadResult LocalVfsBackend::openRead(const QUrl& url) const {
        const QString path = resolveLocalPath(url);
        if (path.isEmpty()) return {{}, QStringLiteral("The local file path is invalid.")};
        auto file = std::make_unique<QFile>(path);
        if (!file->open(QIODevice::ReadOnly)) return {{}, file->errorString()};
        return {std::move(file), {}};
    }

    ReadResult LocalVfsBackend::readAll(const QUrl& url, qint64 max_bytes) const {
        auto opened = openRead(url);
        if (!opened.ok()) return {{}, opened.error};
        if (max_bytes >= 0 && opened.file->size() > max_bytes) return {{}, QStringLiteral("The file is too large.")};
        const QByteArray data = opened.file->readAll();
        if (opened.file->error() != QFileDevice::NoError) return {{}, opened.file->errorString()};
        return {data, {}};
    }

    QString LocalVfsBackend::readStream(const QUrl& url, const StreamReader& reader) const {
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

    QString LocalVfsBackend::writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options) const {
        if (!writer) return QStringLiteral("No file writer was provided.");
        const QString path = resolveLocalPath(url);
        if (path.isEmpty()) return QStringLiteral("The local file path is invalid.");
        const QFileInfo info(path);
        if (options.createParentDirectories && !QDir().mkpath(info.absolutePath()))
            return QStringLiteral("Could not create the destination directory.");

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return file.errorString();
        try {
            if (const QString error = writer(file); !error.isEmpty()) {
                file.cancelWriting();
                return error;
            }
        } catch (const std::exception& error) {
            file.cancelWriting();
            return QString::fromUtf8(error.what());
        } catch (...) {
            file.cancelWriting();
            return QStringLiteral("The file writer failed.");
        }
        return file.commit() ? QString() : file.errorString();
    }

    QString LocalVfsBackend::displayName(const QUrl& url) const {
        const QString path = resolveLocalPath(url);
        return path.isEmpty() ? QString() : QFileInfo(path).fileName();
    }

    Vfs& fileSystem() {
        static VfsRouter backend;
        return backend;
    }
}  // namespace io
