#include "io/privilegedvfs.h"

#ifdef Q_OS_ANDROID
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJniEnvironment>
#include <QJniObject>
#include <QUuid>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <exception>

namespace io {
    namespace {
        constexpr auto kAccessClass = "io/github/hhhxiao/bedrockmap/WorldFileAccess";

        QString errnoText(int error) { return QString::fromLocal8Bit(std::strerror(error)); }

        QString pathOf(const QUrl& url) { return LocalVfsBackend::resolveLocalPath(url); }

        QString invalidPath() { return QCoreApplication::translate("Vfs", "The local file path is invalid."); }

        /// Java methods return 0 on success or a negated errno.
        int checkedStatus(jint result) {
            QJniEnvironment env;
            return env.checkAndClearExceptions() ? -EIO : result;
        }

        int pathStatus(const char* method, const QString& path) {
            const auto name = QJniObject::fromString(path);
            return checkedStatus(QJniObject::callStaticMethod<jint>(kAccessClass, method, "(Ljava/lang/String;)I", name.object<jstring>()));
        }

        int pathModeStatus(const char* method, const QString& path, int mode) {
            const auto name = QJniObject::fromString(path);
            return checkedStatus(QJniObject::callStaticMethod<jint>(kAccessClass, method, "(Ljava/lang/String;I)I", name.object<jstring>(),
                                                                    static_cast<jint>(mode)));
        }

        int pathPairStatus(const char* method, const QString& first, const QString& second) {
            const auto a = QJniObject::fromString(first);
            const auto b = QJniObject::fromString(second);
            return checkedStatus(QJniObject::callStaticMethod<jint>(kAccessClass, method, "(Ljava/lang/String;Ljava/lang/String;)I",
                                                                    a.object<jstring>(), b.object<jstring>()));
        }

        int openDescriptor(const QString& path, int flags, int mode) {
            const auto name = QJniObject::fromString(path);
            return checkedStatus(QJniObject::callStaticMethod<jint>(kAccessClass, "open", "(Ljava/lang/String;II)I", name.object<jstring>(),
                                                                    static_cast<jint>(flags), static_cast<jint>(mode)));
        }

        int nativeFlags(NativeOpen flags) {
            const bool read = hasFlag(flags, NativeOpen::Read);
            const bool write = hasFlag(flags, NativeOpen::Write);
            int native = read && write ? O_RDWR : write ? O_WRONLY : O_RDONLY;
            if (hasFlag(flags, NativeOpen::Create)) native |= O_CREAT;
            if (hasFlag(flags, NativeOpen::Exclusive)) native |= O_EXCL;
            if (hasFlag(flags, NativeOpen::Truncate)) native |= O_TRUNC;
            if (hasFlag(flags, NativeOpen::Append)) native |= O_APPEND;
            return native;
        }
    }  // namespace

    bool PrivilegedVfsBackend::available() const {
        const jboolean result = QJniObject::callStaticMethod<jboolean>(kAccessClass, "available", "()Z");
        QJniEnvironment env;
        return !env.checkAndClearExceptions() && result;
    }

    NativeFileResult PrivilegedVfsBackend::openNative(const QUrl& url, NativeOpen flags, int mode) const {
        const QString path = pathOf(url);
        if (path.isEmpty()) return {-1, invalidPath()};
        const int descriptor = openDescriptor(path, nativeFlags(flags), mode);
        if (descriptor < 0) return {-1, errnoText(-descriptor)};
        return {descriptor, {}};
    }

    OpenReadResult PrivilegedVfsBackend::openRead(const QUrl& url) const {
        const auto opened = openNative(url, NativeOpen::Read);
        if (!opened.ok()) return {{}, opened.error};
        auto file = std::make_unique<QFile>();
        if (!file->open(opened.descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
            ::close(opened.descriptor);
            return {{}, file->errorString()};
        }
        return {std::move(file), {}};
    }

    QString PrivilegedVfsBackend::writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options) const {
        if (!writer) return QCoreApplication::translate("Vfs", "No file writer was provided.");
        const QString path = pathOf(url);
        if (path.isEmpty()) return invalidPath();
        const QString parent = QFileInfo(path).absolutePath();
        if (options.createParentDirectories) {
            if (const QString error = makeDirectory(QUrl::fromLocalFile(parent), true); !error.isEmpty()) return error;
        }

        // Write a sibling and rename it over the target, so a failed writer
        // leaves the existing file intact (same contract as QSaveFile).
        const QString temporary = QDir(parent).filePath(QStringLiteral(".%1.%2.tmp")
                                                            .arg(QFileInfo(path).fileName(), QUuid::createUuid().toString(QUuid::Id128).left(12)));
        const QUrl temporaryUrl = QUrl::fromLocalFile(temporary);
        const auto opened = openNative(temporaryUrl, NativeOpen::Write | NativeOpen::Create | NativeOpen::Exclusive, 0660);
        if (!opened.ok()) return opened.error;

        QString error;
        {
            QFile file;
            if (!file.open(opened.descriptor, QIODevice::WriteOnly, QFileDevice::DontCloseHandle)) {
                error = file.errorString();
            } else {
                try {
                    error = writer(file);
                } catch (const std::exception& exception) {
                    error = QString::fromUtf8(exception.what());
                } catch (...) {
                    error = QCoreApplication::translate("Vfs", "The file writer failed.");
                }
                if (error.isEmpty() && !file.flush()) error = file.errorString();
            }
        }
        if (error.isEmpty() && ::fsync(opened.descriptor) != 0) error = errnoText(errno);
        if (::close(opened.descriptor) != 0 && error.isEmpty()) error = errnoText(errno);
        if (error.isEmpty()) error = rename(temporaryUrl, url);
        if (!error.isEmpty()) static_cast<void>(remove(temporaryUrl));
        return error;
    }

    QString PrivilegedVfsBackend::displayName(const QUrl& url) const {
        const QString path = pathOf(url);
        return path.isEmpty() ? QString() : QFileInfo(path).fileName();
    }

    StatResult PrivilegedVfsBackend::stat(const QUrl& url) const {
        const QString path = pathOf(url);
        if (path.isEmpty()) return {{}, invalidPath()};
        const auto name = QJniObject::fromString(path);
        const auto result = QJniObject::callStaticObjectMethod(kAccessClass, "stat", "(Ljava/lang/String;)[J", name.object<jstring>());
        QJniEnvironment env;
        if (env.checkAndClearExceptions() || !result.isValid()) return {{}, errnoText(EIO)};
        const auto array = result.object<jlongArray>();
        if (env->GetArrayLength(array) != 4) return {{}, errnoText(EIO)};
        jlong values[4];
        env->GetLongArrayRegion(array, 0, 4, values);
        if (env.checkAndClearExceptions()) return {{}, errnoText(EIO)};
        if (values[0] == -ENOENT || values[0] == -ENOTDIR) return {};
        if (values[0] < 0) return {{}, errnoText(static_cast<int>(-values[0]))};
        FileInfo info;
        info.exists = true;
        info.isFile = S_ISREG(values[1]);
        info.isDirectory = S_ISDIR(values[1]);
        info.isLink = S_ISLNK(values[1]);
        info.size = info.isFile ? static_cast<std::uint64_t>(std::max<jlong>(0, values[2])) : 0;
        info.modified = values[3];
        return {info, {}};
    }

    ListResult PrivilegedVfsBackend::list(const QUrl& url) const {
        const QString path = pathOf(url);
        if (path.isEmpty()) return {{}, invalidPath()};
        QJniEnvironment env;
        jintArray created = env->NewIntArray(1);
        if (!created) {
            env.checkAndClearExceptions();
            return {{}, errnoText(ENOMEM)};
        }
        // fromLocalRef() deletes the local reference; use only the wrapper afterwards.
        const auto errorArray = QJniObject::fromLocalRef(created);
        const auto error = errorArray.object<jintArray>();
        const auto name = QJniObject::fromString(path);
        const auto entries = QJniObject::callStaticObjectMethod(kAccessClass, "list", "(Ljava/lang/String;[I)[Ljava/lang/String;",
                                                                name.object<jstring>(), error);
        if (env.checkAndClearExceptions()) return {{}, errnoText(EIO)};
        if (!entries.isValid()) {
            jint code = EIO;
            env->GetIntArrayRegion(error, 0, 1, &code);
            env.checkAndClearExceptions();
            return {{}, errnoText(code > 0 ? code : EIO)};
        }
        ListResult result;
        const auto array = entries.object<jobjectArray>();
        const jsize count = env->GetArrayLength(array);
        for (jsize index = 0; index < count; ++index) {
            const auto entry = QJniObject::fromLocalRef(env->GetObjectArrayElement(array, index));
            result.names.push_back(entry.toString());
        }
        if (env.checkAndClearExceptions()) return {{}, errnoText(EIO)};
        result.names.sort();
        return result;
    }

    QString PrivilegedVfsBackend::makeDirectory(const QUrl& url, bool recursive) const {
        const QString path = pathOf(url);
        if (path.isEmpty()) return invalidPath();
        QStringList pending{path};
        if (recursive) {
            // Collect missing ancestors, then create them from the top down.
            pending.clear();
            for (QString current = path; !current.isEmpty() && current != QStringLiteral("/"); current = QFileInfo(current).path()) {
                const auto status = stat(QUrl::fromLocalFile(current));
                if (!status.ok()) return status.error;
                if (status.info.exists) {
                    if (!status.info.isDirectory) return errnoText(ENOTDIR);
                    break;
                }
                pending.push_front(current);
            }
        }
        for (const auto& directory : pending) {
            const int result = pathModeStatus("mkdir", directory, 0771);
            if (result < 0 && result != -EEXIST) return errnoText(-result);
        }
        return {};
    }

    QString PrivilegedVfsBackend::remove(const QUrl& url) const {
        const QString path = pathOf(url);
        if (path.isEmpty()) return invalidPath();
        const int result = pathStatus("remove", path);
        return result < 0 ? errnoText(-result) : QString{};
    }

    QString PrivilegedVfsBackend::rename(const QUrl& from, const QUrl& to) const {
        const QString source = pathOf(from);
        const QString destination = pathOf(to);
        if (source.isEmpty() || destination.isEmpty()) return invalidPath();
        const int result = pathPairStatus("rename", source, destination);
        return result < 0 ? errnoText(-result) : QString{};
    }
}  // namespace io
#endif
