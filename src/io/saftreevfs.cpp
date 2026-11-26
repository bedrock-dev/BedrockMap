#include <QtGlobal>

#if defined(Q_OS_ANDROID)

#include <QCoreApplication>
#include <QFileInfo>
#include <QJniEnvironment>
#include <QJniObject>
#include <QUrlQuery>

#include "base/platform.h"
#include "io/androidcontentio.h"
#include "io/vfs.h"

namespace io {
    namespace {
        constexpr auto relativePathQuery = "__bedrockmap_relative_path";

        struct TreeLocation {
            QUrl root;
            QString relative;
        };

        TreeLocation treeLocation(const QUrl& url) {
            QUrlQuery query(url);
            const QString relative = query.queryItemValue(QString::fromLatin1(relativePathQuery), QUrl::FullyDecoded);
            query.removeAllQueryItems(QString::fromLatin1(relativePathQuery));
            QUrl root = url;
            root.setQuery(query);
            return {root, relative};
        }

        QString jniString(const QJniObject& object) { return object.isValid() ? object.toString() : QString{}; }

        QString callTreeString(const char* method, const char* signature, const QUrl& root, const QString& relative) {
            const auto context = QNativeInterface::QAndroidApplication::context();
            if (!context.isValid()) return QCoreApplication::translate("Vfs", "The Android activity is unavailable.");
            const auto tree = QJniObject::fromString(root.toString(QUrl::FullyEncoded));
            const auto path = QJniObject::fromString(relative);
            const auto result = QJniObject::callStaticObjectMethod("io/github/hhhxiao/bedrockmap/SafTreeIo", method, signature,
                                                                   context.object(), tree.object<jstring>(), path.object<jstring>());
            QJniEnvironment env;
            if (env.checkAndClearExceptions() || !result.isValid())
                return QCoreApplication::translate("Vfs", "Cannot access the selected Android folder.");
            return jniString(result);
        }
    }  // namespace

    bool SafTreeVfsBackend::supports(const QUrl& url) const {
        const auto location = treeLocation(url);
        if (!platform::isContentUri(location.root)) return false;
        const auto context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return false;
        const auto source = QJniObject::fromString(location.root.toString(QUrl::FullyEncoded));
        const jboolean isTree = QJniObject::callStaticMethod<jboolean>("io/github/hhhxiao/bedrockmap/DocumentMetadata", "isTreeUri",
                                                                       "(Landroid/content/Context;Ljava/lang/String;)Z", context.object(),
                                                                       source.object<jstring>());
        QJniEnvironment env;
        return !env.checkAndClearExceptions() && isTree;
    }

    OpenReadResult SafTreeVfsBackend::openRead(const QUrl& url) const {
        const auto location = treeLocation(url);
        if (location.relative.isEmpty())
            return {{}, QCoreApplication::translate("Vfs", "The selected tree is a directory, not a document.")};
        const QString document =
            callTreeString("documentUri", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                           location.root, location.relative);
        if (document.isEmpty() || document.startsWith(QStringLiteral("ERR:")) || document.startsWith(QStringLiteral("MISSING")))
            return {{}, QCoreApplication::translate("Vfs", "The selected document is unavailable.")};
        return ContentDocumentVfsBackend{}.openRead(QUrl(document));
    }

    QString SafTreeVfsBackend::writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options) const {
        Q_UNUSED(url);
        Q_UNUSED(writer);
        Q_UNUSED(options);
        return QCoreApplication::translate("Vfs", "SAF directories are read-only.");
    }

    QString SafTreeVfsBackend::displayName(const QUrl& url) const {
        const auto location = treeLocation(url);
        if (!location.relative.isEmpty()) return QFileInfo(location.relative).fileName();
        return detail::contentDisplayName(location.root);
    }

    QString SafTreeVfsBackend::copyDirectory(const QUrl& source, const QUrl& destination) const {
        const auto context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return QCoreApplication::translate("Vfs", "The Android activity is unavailable.");
        if (supports(source) && destination.isLocalFile()) {
            const auto location = treeLocation(source);
            const auto sourceString = QJniObject::fromString(location.root.toString(QUrl::FullyEncoded));
            const auto destinationString = QJniObject::fromString(destination.toLocalFile());
            const auto result =
                QJniObject::callStaticObjectMethod("io/github/hhhxiao/bedrockmap/SafTreeIo", "copyTree",
                                                   "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                                                   context.object(), sourceString.object<jstring>(), destinationString.object<jstring>());
            QJniEnvironment env;
            if (env.checkAndClearExceptions() || !result.isValid())
                return QCoreApplication::translate("Vfs", "Cannot access the selected Android folder.");
            return jniString(result);
        }
        return QCoreApplication::translate("Vfs", "Cannot copy directories between these storage locations.");
    }

    QString SafTreeVfsBackend::resolveNativeDirectory(const QUrl& source) const { return detail::resolveContentTreePath(source); }

    StatResult SafTreeVfsBackend::stat(const QUrl& url) const {
        const auto location = treeLocation(url);
        const QString result =
            callTreeString("statTree", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", location.root,
                           location.relative);
        if (result == QStringLiteral("MISSING")) return {};
        if (result.startsWith(QStringLiteral("ERR:"))) return {{}, result.mid(4)};
        if (!result.startsWith(QStringLiteral("OK:"))) return {{}, QCoreApplication::translate("Vfs", "Invalid tree metadata.")};
        const QStringList fields = result.mid(3).split(QChar::Null);
        if (fields.size() != 3) return {{}, QCoreApplication::translate("Vfs", "Invalid tree metadata.")};
        FileInfo info;
        info.exists = true;
        info.isDirectory = fields[0] == QStringLiteral("1");
        info.isFile = !info.isDirectory;
        info.size = fields[1].toULongLong();
        info.modified = fields[2].toLongLong();
        return {info, {}};
    }

    ListResult SafTreeVfsBackend::list(const QUrl& url) const {
        const auto location = treeLocation(url);
        const QString result =
            callTreeString("listTree", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", location.root,
                           location.relative);
        if (result.startsWith(QStringLiteral("ERR:"))) return {{}, result.mid(4)};
        if (!result.startsWith(QStringLiteral("OK:"))) return {{}, QCoreApplication::translate("Vfs", "Invalid directory listing.")};
        QStringList names = result.mid(3).split(QChar::Null, Qt::SkipEmptyParts);
        names.sort();
        return {names, {}};
    }

}  // namespace io

#endif
