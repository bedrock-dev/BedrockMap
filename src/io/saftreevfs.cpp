#include "base/platform.h"
#include "io/vfs.h"

#if defined(Q_OS_ANDROID)
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace io {
    bool SafTreeVfsBackend::supports(const QUrl& url) const {
#if defined(Q_OS_ANDROID)
        if (!platform::isContentUri(url)) return false;
        const auto context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return false;
        const auto source = QJniObject::fromString(url.toString(QUrl::FullyEncoded));
        const jboolean isTree = QJniObject::callStaticMethod<jboolean>("io/github/hhhxiao/bedrockmap/DocumentMetadata", "isTreeUri",
                                                                       "(Landroid/content/Context;Ljava/lang/String;)Z", context.object(),
                                                                       source.object<jstring>());
        QJniEnvironment env;
        return !env.checkAndClearExceptions() && isTree;
#else
        Q_UNUSED(url);
        return false;
#endif
    }

    OpenReadResult SafTreeVfsBackend::openRead(const QUrl& url) const { return ContentDocumentVfsBackend{}.openRead(url); }

    QString SafTreeVfsBackend::writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options) const {
        return ContentDocumentVfsBackend{}.writeStream(url, writer, options);
    }

    QString SafTreeVfsBackend::displayName(const QUrl& url) const { return platform::fileDisplayName(url.toString(QUrl::FullyEncoded)); }

    QString SafTreeVfsBackend::copyTree(const QUrl& source, const QUrl& destination) const {
#if defined(Q_OS_ANDROID)
        if (!supports(source) || !destination.isLocalFile()) return QStringLiteral("The SAF directory or local destination is invalid.");
        const auto context = QNativeInterface::QAndroidApplication::context();
        const auto sourceString = QJniObject::fromString(source.toString(QUrl::FullyEncoded));
        const auto destinationString = QJniObject::fromString(destination.toLocalFile());
        const auto result = QJniObject::callStaticObjectMethod(
            "io/github/hhhxiao/bedrockmap/SafTreeIo", "copyTree", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            context.object(), sourceString.object<jstring>(), destinationString.object<jstring>());
        QJniEnvironment env;
        if (env.checkAndClearExceptions() || !result.isValid()) return QStringLiteral("Cannot access the selected Android folder.");
        return result.toString();
#else
        Q_UNUSED(source);
        Q_UNUSED(destination);
        return QStringLiteral("SAF directories are only supported on Android.");
#endif
    }

    QString SafTreeVfsBackend::resolveNativeDirectory(const QUrl& source) const { return platform::resolveContentDirectory(source); }
}  // namespace io
