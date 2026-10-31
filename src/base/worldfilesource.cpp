#include "base/worldfilesource.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <algorithm>
#include <memory>

#ifdef Q_OS_ANDROID
#include <sys/stat.h>
#include <unistd.h>

#include <QJniEnvironment>
#include <QJniObject>
#include <cerrno>
#include <cstring>
#endif

namespace platform {
    namespace {
        class LocalWorldFileSource final : public WorldFileSource {
           public:
            QString fileInfo(const QString& path, WorldFileInfo& info) const override {
                info = {};
                const QFileInfo file(path);
                if (!file.exists() && !file.isSymLink() && !file.isJunction()) return QStringLiteral("File not found.");
                info = {file.isFile(), file.isDir(), file.isSymLink() || file.isJunction(),
                        static_cast<std::uint64_t>(std::max<qint64>(0, file.size())), file.lastModified().toSecsSinceEpoch()};
                return {};
            }

            QString children(const QString& path, QStringList* result) const override {
                result->clear();
                const QDir directory(path);
                const QFileInfo info(path);
                if (!directory.exists() || !directory.isReadable()
#ifdef Q_OS_UNIX
                    || !info.isExecutable()
#endif
                )
                    return QStringLiteral("Directory unavailable.");
                *result = directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
                return {};
            }

            std::unique_ptr<QFile> openRead(const QString& path) const override {
                auto file = std::make_unique<QFile>(path);
                return file->open(QIODevice::ReadOnly) ? std::move(file) : nullptr;
            }
        };

#ifdef Q_OS_ANDROID
        QString fileError(int error) { return QString::fromLocal8Bit(std::strerror(error)); }

        class PrivilegedWorldFileSource final : public WorldFileSource {
           public:
            explicit PrivilegedWorldFileSource(QJniObject files) : files_(std::move(files)) {}

            QString fileInfo(const QString& path, WorldFileInfo& info) const override {
                info = {};
                const auto name = QJniObject::fromString(path);
                const auto result = files_.callObjectMethod("stat", "(Ljava/lang/String;)[J", name.object<jstring>());
                QJniEnvironment jni;
                if (jni.checkAndClearExceptions() || !result.isValid()) return fileError(EIO);
                const auto array = result.object<jlongArray>();
                if (jni->GetArrayLength(array) != 4) return fileError(EIO);
                jlong values[4];
                jni->GetLongArrayRegion(array, 0, 4, values);
                if (jni.checkAndClearExceptions()) return fileError(EIO);
                if (values[0] < 0) return fileError(static_cast<int>(-values[0]));
                info.isFile = S_ISREG(values[1]);
                info.isDirectory = S_ISDIR(values[1]);
                info.isLink = S_ISLNK(values[1]);
                info.size = std::max<jlong>(0, values[2]);
                info.modified = values[3];
                return {};
            }

            QString children(const QString& path, QStringList* result) const override {
                result->clear();
                const auto name = QJniObject::fromString(path);
                const auto entries = files_.callObjectMethod("list", "(Ljava/lang/String;)[Ljava/lang/String;", name.object<jstring>());
                QJniEnvironment jni;
                if (jni.checkAndClearExceptions() || !entries.isValid()) return fileError(EIO);
                const auto array = entries.object<jobjectArray>();
                const auto count = jni->GetArrayLength(array);
                for (jsize index = 0; index < count; ++index) {
                    const auto entry = QJniObject::fromLocalRef(jni->GetObjectArrayElement(array, index));
                    result->push_back(entry.toString());
                }
                result->sort();
                return jni.checkAndClearExceptions() ? fileError(EIO) : QString{};
            }

            std::unique_ptr<QFile> openRead(const QString& path) const override {
                const auto name = QJniObject::fromString(path);
                const int fd = files_.callMethod<jint>("openRead", "(Ljava/lang/String;)I", name.object<jstring>());
                QJniEnvironment jni;
                if (jni.checkAndClearExceptions() || fd < 0) return nullptr;
                auto file = std::make_unique<QFile>();
                if (!file->open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
                    ::close(fd);
                    return nullptr;
                }
                return file;
            }

           private:
            QJniObject files_;
        };
#endif

#ifdef Q_OS_ANDROID
        QStringList pathComponents(const QString& path) {
            return QDir::cleanPath(QDir::fromNativeSeparators(path)).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        }

        bool isInside(const QString& path, const QString& root) { return path == root || path.startsWith(root + QLatin1Char('/')); }
#endif
    }  // namespace

    std::unique_ptr<WorldFileSource> worldFileSource(const QString& path) {
        if (path.isEmpty()) return {};
        auto local = std::make_unique<LocalWorldFileSource>();
        QStringList children;
        if (local->children(path, &children).isEmpty()) return local;
#ifdef Q_OS_ANDROID
        const auto name = QJniObject::fromString(path);
        const auto files = QJniObject::callStaticObjectMethod(
            "io/github/hhhxiao/bedrockmap/WorldFileAccess", "connect", "(Ljava/lang/String;)Lio/github/hhhxiao/bedrockmap/WorldFileAccess;", name.object<jstring>());
        QJniEnvironment jni;
        if (!jni.checkAndClearExceptions() && files.isValid()) return std::make_unique<PrivilegedWorldFileSource>(files);
#endif
        return {};
    }

    bool requiresWorldCopy(const QString& path) {
#ifdef Q_OS_ANDROID
        const QStringList parts = pathComponents(path);
        for (qsizetype index = 0; index + 1 < parts.size(); ++index) {
            if (parts[index] == QStringLiteral("Android") && parts[index + 1] == QStringLiteral("data")) return true;
        }
        if (parts.empty() || parts.front() != QStringLiteral("data")) return false;

        const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
        const QString appData =
            QDir::cleanPath(QDir::fromNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)));
        const QString canonicalAppData = QFileInfo(appData).canonicalFilePath();
        return !((!appData.isEmpty() && isInside(normalized, appData)) ||
                 (!canonicalAppData.isEmpty() && isInside(normalized, canonicalAppData)));
#else
        Q_UNUSED(path);
        return false;
#endif
    }
}  // namespace platform
