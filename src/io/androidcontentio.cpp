#include "io/androidcontentio.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "base/platform.h"

#if defined(Q_OS_ANDROID)
#include <unistd.h>

#include <QJniEnvironment>
#include <QJniObject>
#include <QScopeGuard>
#endif

namespace io::detail {
    QString openContentDocument(QFile& file, QIODevice::OpenMode mode) {
#if defined(Q_OS_ANDROID)
        const QUrl url(file.fileName());
        if (platform::isContentUri(url)) {
            const QJniObject context(QNativeInterface::QAndroidApplication::context());
            const auto resolver = context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
            const auto source = QJniObject::fromString(url.toString(QUrl::FullyEncoded));
            const auto uri = QJniObject::callStaticObjectMethod("io/github/hhhxiao/bedrockmap/DocumentMetadata", "grantedUri",
                                                                "(Landroid/content/Context;Ljava/lang/String;)Landroid/net/Uri;",
                                                                context.object(), source.object<jstring>());
            QString access = QStringLiteral("r");
            if (mode & QIODevice::WriteOnly) {
                access = mode & QIODevice::ReadOnly ? QStringLiteral("rw") : QStringLiteral("w");
                if (mode & QIODevice::Append)
                    access += QLatin1Char('a');
                else if (mode & QIODevice::Truncate)
                    access += QLatin1Char('t');
            }
            const auto accessString = QJniObject::fromString(access);
            QJniEnvironment env;
            const auto exceptionMessage = [&env]() -> QString {
                if (!env->ExceptionCheck()) return {};
                const auto exception = env->ExceptionOccurred();
                env->ExceptionClear();
                return QJniObject::fromLocalRef(exception).callObjectMethod<jstring>("toString").toString();
            };
            // Qt 6.11 rebuilds document IDs before checking permissions, which
            // changes e.g. literal parentheses to %28/%29 and loses the grant.
            const auto openMethod = env->GetMethodID(resolver.objectClass(), "openFileDescriptor",
                                                     "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;");
            if (const QString error = exceptionMessage(); !error.isEmpty()) return error;
            const auto descriptorObject = env->CallObjectMethod(resolver.object(), openMethod, uri.object(), accessString.object());
            if (const QString error = exceptionMessage(); !error.isEmpty()) return error;
            const auto descriptor = QJniObject::fromLocalRef(descriptorObject);
            const auto missingDescriptor = [] {
                return QCoreApplication::translate("io", "The document provider returned no file descriptor.");
            };
            if (!descriptor.isValid()) return missingDescriptor();
            const auto closeDescriptor = qScopeGuard([&descriptor] { descriptor.callMethod<void>("close"); });
            const auto detachMethod = env->GetMethodID(descriptor.objectClass(), "detachFd", "()I");
            if (const QString error = exceptionMessage(); !error.isEmpty()) return error;
            const int fd = env->CallIntMethod(descriptor.object(), detachMethod);
            if (const QString error = exceptionMessage(); !error.isEmpty()) return error;
            if (fd < 0) return missingDescriptor();
            if (file.open(fd, mode, QFileDevice::AutoCloseHandle)) return {};
            const QString error = file.errorString();
            ::close(fd);
            return error;
        }
#endif
        return file.open(mode) ? QString{} : file.errorString();
    }

    QString contentDisplayName(const QUrl& url) {
#if defined(Q_OS_ANDROID)
        if (platform::isContentUri(url)) {
            const auto context = QNativeInterface::QAndroidApplication::context();
            const auto source = QJniObject::fromString(url.toString(QUrl::FullyEncoded));
            const auto result = QJniObject::callStaticObjectMethod("io/github/hhhxiao/bedrockmap/DocumentMetadata", "displayName",
                                                                   "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
                                                                   context.object(), source.object<jstring>());
            QJniEnvironment env;
            if (!env.checkAndClearExceptions() && result.isValid()) {
                const QString name = result.toString();
                if (!name.isEmpty()) return name;
            }
        }
#endif
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded);
        const QString name = QFileInfo(path).fileName();
        if (!name.isEmpty()) return name;
        const QString uriName = QFileInfo(url.fileName()).fileName();
        return uriName.isEmpty() ? url.toString() : uriName;
    }

    QString resolveContentTreePath(const QUrl& url) {
#if defined(Q_OS_ANDROID)
        if (!platform::isContentUri(url) || !platform::hasAllFilesPermission()) return {};
        const auto context = QNativeInterface::QAndroidApplication::context();
        const auto source = QJniObject::fromString(url.toString(QUrl::FullyEncoded));
        const auto result = QJniObject::callStaticObjectMethod("io/github/hhhxiao/bedrockmap/SafTreeIo", "resolveTreePath",
                                                               "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
                                                               context.object(), source.object<jstring>());
        QJniEnvironment env;
        if (env.checkAndClearExceptions() || !result.isValid()) return {};
        return result.toString();
#else
        Q_UNUSED(url);
        return {};
#endif
    }
}  // namespace io::detail
