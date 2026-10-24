#include "controller/appcontroller.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtConcurrent>
#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

#include "base/config.h"
#include "base/platform.h"
#include "controller/workspacesession.h"
#include "controller/worldsession.h"
#include "model/notifications.h"
#include "model/worldlistmodel.h"
#include "model/worldpath.h"

namespace {
    QString internalWorldsRoot() {
        const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        return root.isEmpty() ? QString{} : QDir(root).filePath(QStringLiteral("worlds"));
    }

    QString importTargetForSource(const QString& source) {
        const QString root = internalWorldsRoot();
        if (root.isEmpty()) return {};
        const QString key =
            QString::fromLatin1(QCryptographicHash::hash(world_path::identityKey(source).toUtf8(), QCryptographicHash::Sha256).toHex());
        return QDir(root).filePath(key);
    }

    bool isCompleteWorld(const QString& path) {
        const QDir dir(path);
        return QFileInfo(dir.filePath(QStringLiteral("level.dat"))).isFile() && QFileInfo(dir.filePath(QStringLiteral("db"))).isDir();
    }

#ifdef Q_OS_ANDROID
    QString resolveContentTreePath(const QString& source) {
        const auto context = QNativeInterface::QAndroidApplication::context();
        const auto sourceString = QJniObject::fromString(source);
        const auto result = QJniObject::callStaticObjectMethod("org/bedrockmap/WorldDirectoryImport", "resolveTreePath",
                                                               "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
                                                               context.object(), sourceString.object<jstring>());
        QJniEnvironment env;
        if (env.checkAndClearExceptions() || !result.isValid()) return {};
        return result.toString();
    }
#endif
}  // namespace

AppController::AppController(QObject* parent) : QObject(parent), sessions_(this) {
    connect(&sessions_, &SessionController::activeSessionChanged, this, &AppController::activeSessionChanged);
    connect(&sessions_, &SessionController::activeIndexChanged, this, &AppController::workspaceIndexChanged);
    connect(&sessions_, &SessionController::closeRequested, this,
            [this](WorkspaceSession* session, int index) { emit sessionCloseRequested(session, index); });
    connect(&sessions_, &SessionController::sessionAboutToClose, this,
            [this](WorkspaceSession* session) { emit sessionAboutToClose(session); });
    connect(&sessions_, &SessionController::sessionsChanged, this, [this] {
        if (sessions_.hasSessions()) return;
        setWorkspaceOpen(false);
        setCurrentPage(0);
    });
    connect(&sessions_, &SessionController::warningOccurred, this, [this](const QString& message, const QString& detail) {
        if (notifications_) notifications_->warning(message, {}, detail);
    });
    connect(&sessions_, &SessionController::errorOccurred, this, [this](const QString& message, const QString& detail) {
        if (notifications_) notifications_->error(message, {}, detail);
    });
}
AppController::~AppController() {
    // The importer owns only value captures and must finish before shutdown.
    import_watcher_.waitForFinished();
}
void AppController::setCurrentPage(int page) {
    // Application-level destinations only. World-internal data and tools live
    // inside the active workspace.
    page = qBound(0, page, 4);
    if (current_page_ == page) return;
    current_page_ = page;
    emit currentPageChanged();
}
void AppController::navigateToPage(int page) {
    setCurrentPage(page);
    setWorkspaceOpen(false);
}

void AppController::selectWorkspace(int index) {
    if (!sessions_.sessions()->at(index)) return;
    sessions_.setActiveIndex(index);
    setWorkspaceOpen(true);
}
QString AppController::defaultWorldFolder() const {
#ifdef Q_OS_ANDROID
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#else
    // Same start folder as MainWindow::openLevel(): the first generic data
    // location plus the MCBE worlds suffix.
    const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    if (locations.isEmpty()) return {};
    return locations.first() + QString::fromUtf8(constant::MCBE_LEVEL_PATH);
#endif
}

QUrl AppController::defaultWorldFolderUrl() const {
    const QString path = defaultWorldFolder();
    return path.isEmpty() ? QUrl{} : QUrl::fromLocalFile(path);
}

bool AppController::isWorldFolder(const QString& path) const {
    const QString target = world_path::normalize(path);
    if (target.isEmpty() || !QFileInfo(target).isDir()) return false;
    const QDir dir(target);
    return dir.exists(QStringLiteral("level.dat")) || dir.exists(QStringLiteral("levelname.txt"));
}

void AppController::openWorldFolder(const QString& path) {
    if (path.isEmpty()) return;
#ifdef Q_OS_ANDROID
    const QUrl url(path);
    if (url.scheme() == QStringLiteral("content")) {
        if (importing_world_) return;
        const QString source = url.toString(QUrl::FullyEncoded);
        const QString target = importTargetForSource(source);
        if (platform::hasAllFilesAccess()) {
            const QString resolved = resolveContentTreePath(source);
            if (!resolved.isEmpty() && isCompleteWorld(resolved)) {
                emit worldSourceDiscovered(resolved);
                openWorld(resolved);
                return;
            }
        }
        // No native path is available; reopen the imported copy if one exists.
        if (!target.isEmpty() && isCompleteWorld(target)) {
            openWorld(target);
            return;
        }
        emit safWorldCopyRequested(source);
        return;
    }
#endif
    if (!isWorldFolder(path)) {
        if (notifications_) notifications_->warning(tr("That folder is not a Bedrock world (no level.dat)."));
        return;
    }
    openWorld(path);
}

void AppController::openWorld(const QString& path, const QString& title) {
    if (auto* session = sessions_.openWorld(path, title)) {
        if (auto* world = qobject_cast<WorldSession*>(session)) world->ensureTerrainData();
        setWorkspaceOpen(true);
    }
}

void AppController::copySafWorld(const QString& path) {
    if (importing_world_) {
        if (notifications_) notifications_->warning(tr("Wait for the current world import to finish."));
        return;
    }

    const QUrl url(path);
    if (url.scheme() != QStringLiteral("content")) {
        if (notifications_) notifications_->warning(tr("Only SAF folders can be copied."));
        return;
    }
    const QString source = url.toString(QUrl::FullyEncoded);

    const QString parent = internalWorldsRoot();
    const QString target = importTargetForSource(source);
    if (parent.isEmpty() || target.isEmpty()) {
        if (notifications_) notifications_->error(tr("World import failed."));
        return;
    }
    if (isCompleteWorld(target)) {
        openWorld(target);
        return;
    }

    importing_world_ = true;
    emit importingWorldChanged();
    if (notifications_) notifications_->info(tr("Copying the world to internal storage..."));

    import_watcher_.disconnect(this);
    connect(&import_watcher_, &QFutureWatcher<QString>::finished, this, [this, target] {
        const QString error = import_watcher_.result();
        importing_world_ = false;
        emit importingWorldChanged();
        if (!error.isEmpty()) {
            if (notifications_) notifications_->error(tr("World import failed."), {}, error);
            return;
        }
        if (notifications_) notifications_->info(tr("World copied. Changes are saved to the internal copy."));
        openWorld(target);
    });

    import_watcher_.setFuture(QtConcurrent::run([source, parent, target]() -> QString {
        if (!QDir().mkpath(parent)) return QCoreApplication::translate("AppController", "Cannot create the world import directory.");
        QTemporaryDir staging(QDir(parent).filePath(QStringLiteral(".import-XXXXXX")));
        if (!staging.isValid()) return QCoreApplication::translate("AppController", "Cannot create the world import directory.");

#ifdef Q_OS_ANDROID
        const auto context = QNativeInterface::QAndroidApplication::context();
        const auto sourceString = QJniObject::fromString(source);
        const auto destinationString = QJniObject::fromString(staging.path());
        const auto result =
            QJniObject::callStaticObjectMethod("org/bedrockmap/WorldDirectoryImport", "copyWorld",
                                               "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                                               context.object(), sourceString.object<jstring>(), destinationString.object<jstring>());
        QJniEnvironment env;
        if (env.checkAndClearExceptions() || !result.isValid())
            return QCoreApplication::translate("AppController", "Cannot access the selected Android folder.");
        const QString error = result.toString();
        if (!error.isEmpty()) return error;
#else
        return QCoreApplication::translate("AppController", "Content folders are only supported on Android.");
#endif

        if (!isCompleteWorld(staging.path())) {
            return QCoreApplication::translate("AppController", "The selected directory must contain level.dat and db.");
        }
        const QFileInfo targetInfo(target);
        if (targetInfo.isDir()) {
            if (!QDir(target).removeRecursively())
                return QCoreApplication::translate("AppController", "Cannot replace the previous imported world.");
        } else if (targetInfo.exists() && !QFile::remove(target)) {
            return QCoreApplication::translate("AppController", "Cannot replace the previous imported world.");
        }
        if (!QDir().rename(staging.path(), target)) return QCoreApplication::translate("AppController", "Cannot save the imported world.");
        staging.setAutoRemove(false);
        return {};
    }));
}

void AppController::openMcstructureFile(const QString& path) {
    if (sessions_.openMcstructureFile(path)) setWorkspaceOpen(true);
}

void AppController::openNbtFile(const QString& path) {
    if (sessions_.openNbtFile(path)) setWorkspaceOpen(true);
}

void AppController::newNbtDocument() {
    if (sessions_.newNbtDocument()) setWorkspaceOpen(true);
}

void AppController::closeSession(int index) { sessions_.closeSession(index); }

void AppController::discardSession(int index) { sessions_.discardSession(index); }

void AppController::discardSessionObject(QObject* session) {
    if (auto* workspace = qobject_cast<WorkspaceSession*>(session)) sessions_.discardSession(workspace);
}

void AppController::revealPath(const QString& path) {
    const QFileInfo info(path);
    const QString target = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (target.isEmpty() || !QFileInfo(target).isDir()) {
        if (notifications_) notifications_->warning(tr("Could not open the containing folder."), {}, path);
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(target)) && notifications_)
        notifications_->warning(tr("Could not open the containing folder."), {}, target);
}

bool AppController::requestCloseFirstDirtySession() {
    auto* session = sessions_.firstDirtySession();
    if (!session) return false;
    sessions_.closeSession(session);
    return true;
}

void AppController::openConfigDirectory() {
    const QString path = app_paths::configDirectory();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)) && notifications_)
        notifications_->warning(tr("Could not open the configuration folder."), {}, path);
}

void AppController::setWorkspaceOpen(bool open) {
    if (workspace_open_ == open) return;
    workspace_open_ = open;
    emit workspaceOpenChanged();
    sessions_.setActive(open);
}
