#include "io/vfs.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <exception>

#include "io/browservfs.h"
#include "io/privilegedvfs.h"

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace io {
    namespace {
        QString unsupported() { return QCoreApplication::translate("Vfs", "This operation is not supported for this location."); }

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

#ifndef Q_OS_WIN
        QString errnoText(int error) { return QString::fromLocal8Bit(std::strerror(error)); }
#endif

        template <typename Callback>
        QString guarded(const Callback& callback, const char* failure) {
            try {
                return callback();
            } catch (const std::exception& error) {
                return QString::fromUtf8(error.what());
            } catch (...) {
                return QCoreApplication::translate("Vfs", failure);
            }
        }

#ifdef Q_OS_ANDROID
        struct AndroidAppIdentity {
            QString package;
            QString appData;
            QString canonicalAppData;
        };

        const AndroidAppIdentity& androidAppIdentity() {
            static const AndroidAppIdentity identity = [] {
                AndroidAppIdentity result;
                const QJniObject context(QNativeInterface::QAndroidApplication::context());
                if (context.isValid()) {
                    result.package = context.callObjectMethod<jstring>("getPackageName").toString();
                    QJniEnvironment env;
                    env.checkAndClearExceptions();
                }
                result.appData = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
                result.canonicalAppData = QFileInfo(result.appData).canonicalFilePath();
                return result;
            }();
            return identity;
        }

        bool isInside(const QString& path, const QString& root) {
            return !root.isEmpty() && (path == root || path.startsWith(root + QLatin1Char('/')));
        }
#endif

        class VfsRouter final : public Vfs {
           public:
            OpenReadResult openRead(const QUrl& url) const override { return backend(url).openRead(url); }
            ReadResult readAll(const QUrl& url, qint64 max_bytes) const override { return backend(url).readAll(url, max_bytes); }
            QString readStream(const QUrl& url, const StreamReader& reader) const override { return backend(url).readStream(url, reader); }
            QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options) const override {
                return backend(url).writeStream(url, writer, options);
            }
            QString displayName(const QUrl& url) const override { return backend(url).displayName(url); }
            bool requiresSaveAs(const QUrl& url) const override { return backend(url).requiresSaveAs(url); }
            QString copyTree(const QUrl& source, const QUrl& destination) const override { return backend(source).copyTree(source, destination); }
            QString resolveNativeDirectory(const QUrl& source) const override { return backend(source).resolveNativeDirectory(source); }
            StatResult stat(const QUrl& url) const override { return backend(url).stat(url); }
            ListResult list(const QUrl& url) const override { return backend(url).list(url); }
            QString makeDirectory(const QUrl& url, bool recursive) const override { return backend(url).makeDirectory(url, recursive); }
            QString remove(const QUrl& url) const override { return backend(url).remove(url); }
            QString rename(const QUrl& from, const QUrl& to) const override {
                const Vfs& source = backend(from);
                if (&source != &backend(to)) return QCoreApplication::translate("Vfs", "Cannot move files between storage locations.");
                return source.rename(from, to);
            }
            NativeFileResult openNative(const QUrl& url, NativeOpen flags, int mode) const override {
                return backend(url).openNative(url, flags, mode);
            }

           private:
            const Vfs& backend(const QUrl& url) const {
#ifdef Q_OS_WASM
                if (browser_.supports(url)) return browser_;
#endif
                if (const QString path = LocalVfsBackend::resolveLocalPath(url); !path.isEmpty()) {
#ifdef Q_OS_ANDROID
                    // Route by path policy rather than probing local access:
                    // Android/data can look accessible but list as empty.
                    if (isPrivilegedPath(path) && privileged_.available()) return privileged_;
#endif
                    return local_;
                }
                return saf_tree_.supports(url) ? static_cast<const Vfs&>(saf_tree_) : content_;
            }

#ifdef Q_OS_WASM
            BrowserVfsBackend browser_;
#endif
#ifdef Q_OS_ANDROID
            PrivilegedVfsBackend privileged_;
#endif
            LocalVfsBackend local_;
            ContentDocumentVfsBackend content_;
            SafTreeVfsBackend saf_tree_;
        };
    }  // namespace

    ReadResult Vfs::readAll(const QUrl& url, qint64 max_bytes) const {
        auto opened = openRead(url);
        if (!opened.ok()) return {{}, opened.error};
        if (max_bytes >= 0 && !opened.file->isSequential() && opened.file->size() > max_bytes)
            return {{}, QCoreApplication::translate("Vfs", "The file is too large.")};
        const QByteArray data = max_bytes >= 0 ? opened.file->read(max_bytes + 1) : opened.file->readAll();
        if (opened.file->error() != QFileDevice::NoError) return {{}, opened.file->errorString()};
        if (max_bytes >= 0 && data.size() > max_bytes) return {{}, QCoreApplication::translate("Vfs", "The file is too large.")};
        return {data, {}};
    }

    QString Vfs::readStream(const QUrl& url, const StreamReader& reader) const {
        if (!reader) return QCoreApplication::translate("Vfs", "No file reader was provided.");
        auto opened = openRead(url);
        if (!opened.ok()) return opened.error;
        return guarded([&] { return reader(*opened.file); }, "The file reader failed.");
    }

    QString Vfs::copyTree(const QUrl&, const QUrl&) const { return QCoreApplication::translate("Vfs", "Directory copy is not supported for this URI."); }
    QString Vfs::resolveNativeDirectory(const QUrl&) const { return {}; }
    StatResult Vfs::stat(const QUrl&) const { return {{}, unsupported()}; }
    ListResult Vfs::list(const QUrl&) const { return {{}, unsupported()}; }
    QString Vfs::makeDirectory(const QUrl&, bool) const { return unsupported(); }
    QString Vfs::remove(const QUrl&) const { return unsupported(); }
    QString Vfs::rename(const QUrl&, const QUrl&) const { return unsupported(); }
    NativeFileResult Vfs::openNative(const QUrl&, NativeOpen, int) const { return {-1, unsupported()}; }

    QString LocalVfsBackend::resolveLocalPath(const QUrl& url) {
        const QUrl local = localUrl(url);
        if (!local.isValid() || !local.isLocalFile() || local.hasQuery() || local.hasFragment()) return {};
        return QDir::cleanPath(local.toLocalFile());
    }

    bool LocalVfsBackend::supports(const QUrl& url) const { return !resolveLocalPath(url).isEmpty(); }

    OpenReadResult LocalVfsBackend::openRead(const QUrl& url) const {
        const QString path = resolveLocalPath(url);
        if (path.isEmpty()) return {{}, QCoreApplication::translate("Vfs", "The local file path is invalid.")};
        auto file = std::make_unique<QFile>(path);
        if (!file->open(QIODevice::ReadOnly)) return {{}, file->errorString()};
        return {std::move(file), {}};
    }

    QString LocalVfsBackend::writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options) const {
        if (!writer) return QCoreApplication::translate("Vfs", "No file writer was provided.");
        const QString path = resolveLocalPath(url);
        if (path.isEmpty()) return QCoreApplication::translate("Vfs", "The local file path is invalid.");
        const QFileInfo info(path);
        if (options.createParentDirectories && !QDir().mkpath(info.absolutePath()))
            return QCoreApplication::translate("Vfs", "Could not create the destination directory.");

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return file.errorString();
        if (const QString error = guarded([&] { return writer(file); }, "The file writer failed."); !error.isEmpty()) {
            file.cancelWriting();
            return error;
        }
        return file.commit() ? QString() : file.errorString();
    }

    QString LocalVfsBackend::displayName(const QUrl& url) const {
        const QString path = resolveLocalPath(url);
        return path.isEmpty() ? QString() : QFileInfo(path).fileName();
    }

    QString LocalVfsBackend::resolveNativeDirectory(const QUrl& source) const {
        const QString path = resolveLocalPath(source);
        return !path.isEmpty() && QFileInfo(path).isDir() ? path : QString{};
    }

    StatResult LocalVfsBackend::stat(const QUrl& url) const {
        const QString path = resolveLocalPath(url);
        if (path.isEmpty()) return {{}, QCoreApplication::translate("Vfs", "The local file path is invalid.")};
#ifdef Q_OS_WIN
        const QFileInfo file(path);
        if (!file.exists() && !file.isSymLink() && !file.isJunction()) return {};
        FileInfo info;
        info.exists = true;
        info.isLink = file.isSymLink() || file.isJunction();
        info.isFile = !info.isLink && file.isFile();
        info.isDirectory = !info.isLink && file.isDir();
        info.size = info.isFile ? static_cast<std::uint64_t>(std::max<qint64>(0, file.size())) : 0;
        info.modified = file.lastModified().toSecsSinceEpoch();
        return {info, {}};
#else
        struct ::stat status{};
        if (::lstat(QFile::encodeName(path).constData(), &status) != 0) {
            const int error = errno;
            if (error == ENOENT || error == ENOTDIR) return {};
            return {{}, errnoText(error)};
        }
        FileInfo info;
        info.exists = true;
        info.isFile = S_ISREG(status.st_mode);
        info.isDirectory = S_ISDIR(status.st_mode);
        info.isLink = S_ISLNK(status.st_mode);
        info.size = info.isFile ? static_cast<std::uint64_t>(status.st_size) : 0;
        info.modified = static_cast<std::int64_t>(status.st_mtime);
        return {info, {}};
#endif
    }

    ListResult LocalVfsBackend::list(const QUrl& url) const {
        const QString path = resolveLocalPath(url);
        if (path.isEmpty()) return {{}, QCoreApplication::translate("Vfs", "The local file path is invalid.")};
        ListResult result;
#ifdef Q_OS_WIN
        const QDir directory(path);
        if (!directory.exists() || !directory.isReadable()) return {{}, QCoreApplication::translate("Vfs", "Directory unavailable.")};
        result.names = directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
#else
        DIR* directory = ::opendir(QFile::encodeName(path).constData());
        if (!directory) return {{}, errnoText(errno)};
        errno = 0;
        while (const dirent* entry = ::readdir(directory)) {
            const QString name = QFile::decodeName(entry->d_name);
            if (name != QStringLiteral(".") && name != QStringLiteral("..")) result.names.push_back(name);
            errno = 0;
        }
        const int error = errno;
        ::closedir(directory);
        if (error != 0) return {{}, errnoText(error)};
        result.names.sort();
#endif
        return result;
    }

    QString LocalVfsBackend::makeDirectory(const QUrl& url, bool recursive) const {
        const QString path = resolveLocalPath(url);
        if (path.isEmpty()) return QCoreApplication::translate("Vfs", "The local file path is invalid.");
        if (QFileInfo(path).isDir()) return {};
        const bool created = recursive ? QDir().mkpath(path) : QDir().mkdir(path);
        return created ? QString{} : QCoreApplication::translate("Vfs", "Could not create directory: %1").arg(path);
    }

    QString LocalVfsBackend::remove(const QUrl& url) const {
        const QString path = resolveLocalPath(url);
        if (path.isEmpty()) return QCoreApplication::translate("Vfs", "The local file path is invalid.");
#ifdef Q_OS_WIN
        const QFileInfo info(path);
        const bool directory = info.isDir() && !info.isSymLink() && !info.isJunction();
        const auto native = reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16());
        if (directory ? RemoveDirectoryW(native) : DeleteFileW(native)) return {};
        return QCoreApplication::translate("Vfs", "Could not remove %1 (error %2).").arg(path).arg(GetLastError());
#else
        return ::remove(QFile::encodeName(path).constData()) == 0 ? QString{} : errnoText(errno);
#endif
    }

    QString LocalVfsBackend::rename(const QUrl& from, const QUrl& to) const {
        const QString source = resolveLocalPath(from);
        const QString destination = resolveLocalPath(to);
        if (source.isEmpty() || destination.isEmpty()) return QCoreApplication::translate("Vfs", "The local file path is invalid.");
#ifdef Q_OS_WIN
        if (MoveFileExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(source).utf16()),
                        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(destination).utf16()),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return {};
        return QCoreApplication::translate("Vfs", "Could not rename %1 (error %2).").arg(source).arg(GetLastError());
#else
        return ::rename(QFile::encodeName(source).constData(), QFile::encodeName(destination).constData()) == 0 ? QString{}
                                                                                                             : errnoText(errno);
#endif
    }

    NativeFileResult LocalVfsBackend::openNative(const QUrl& url, NativeOpen flags, int mode) const {
        const QString path = resolveLocalPath(url);
        if (path.isEmpty()) return {-1, QCoreApplication::translate("Vfs", "The local file path is invalid.")};
        const bool read = hasFlag(flags, NativeOpen::Read);
        const bool write = hasFlag(flags, NativeOpen::Write);
#ifdef Q_OS_WIN
        int native = _O_BINARY | _O_NOINHERIT | (read && write ? _O_RDWR : write ? _O_WRONLY : _O_RDONLY);
        if (hasFlag(flags, NativeOpen::Create)) native |= _O_CREAT;
        if (hasFlag(flags, NativeOpen::Exclusive)) native |= _O_EXCL;
        if (hasFlag(flags, NativeOpen::Truncate)) native |= _O_TRUNC;
        if (hasFlag(flags, NativeOpen::Append)) native |= _O_APPEND;
        Q_UNUSED(mode);
        const int descriptor =
            _wopen(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()), native, _S_IREAD | _S_IWRITE);
        if (descriptor < 0) return {-1, QCoreApplication::translate("Vfs", "Could not open %1.").arg(path)};
        return {descriptor, {}};
#else
        int native = O_CLOEXEC | (read && write ? O_RDWR : write ? O_WRONLY : O_RDONLY);
        if (hasFlag(flags, NativeOpen::Create)) native |= O_CREAT;
        if (hasFlag(flags, NativeOpen::Exclusive)) native |= O_EXCL;
        if (hasFlag(flags, NativeOpen::Truncate)) native |= O_TRUNC;
        if (hasFlag(flags, NativeOpen::Append)) native |= O_APPEND;
        const int descriptor = ::open(QFile::encodeName(path).constData(), native, mode);
        if (descriptor < 0) return {-1, errnoText(errno)};
        return {descriptor, {}};
#endif
    }

    bool isPrivilegedPath(const QString& path) {
#ifdef Q_OS_ANDROID
        const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
        const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const auto& self = androidAppIdentity();
        for (qsizetype index = 0; index + 1 < parts.size(); ++index) {
            if (parts[index] != QStringLiteral("Android")) continue;
            if (parts[index + 1] != QStringLiteral("data") && parts[index + 1] != QStringLiteral("obb")) continue;
            // The app's own external files directory stays ordinary storage.
            return index + 2 >= parts.size() || parts[index + 2] != self.package;
        }
        if (parts.empty() || parts.front() != QStringLiteral("data")) return false;
        if (isInside(normalized, self.appData) || isInside(normalized, self.canonicalAppData)) return false;
        // /data/data/<pkg>, /data/user/<n>/<pkg> and /data/user_de/<n>/<pkg>.
        const qsizetype packageIndex = parts.size() > 1 && parts[1] == QStringLiteral("data") ? 2 : 3;
        const bool userData = parts.size() > 1 && (parts[1] == QStringLiteral("data") || parts[1] == QStringLiteral("user") ||
                                                   parts[1] == QStringLiteral("user_de"));
        return !(userData && packageIndex < parts.size() && !self.package.isEmpty() && parts[packageIndex] == self.package);
#else
        Q_UNUSED(path);
        return false;
#endif
    }

    Vfs& fileSystem() {
        static VfsRouter backend;
        return backend;
    }
}  // namespace io
