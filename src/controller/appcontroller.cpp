#include "controller/appcontroller.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QPointer>
#include <QSettings>
#include <QUrl>
#include <QtConcurrent>
#include <exception>
#include <memory>
#ifdef Q_OS_ANDROID
#include <QGuiApplication>
#include <QJniEnvironment>
#include <QJniObject>
#endif

#include "base/appcontext.h"
#include "base/apppaths.h"
#include "base/platform.h"
#include "controller/nbtsession.h"
#include "controller/workspacesession.h"
#include "controller/worldsession.h"
#include "io/documentio.h"
#include "loader/leveloperator.h"
#include "model/notifications.h"
#include "model/worldlistmodel.h"

namespace {
    QPointer<AppController> applicationController;
#ifdef Q_OS_ANDROID
    void onPermissionStateChanged(JNIEnv*, jclass) {
        auto* app = QCoreApplication::instance();
        if (!app) return;
        QMetaObject::invokeMethod(
            app,
            [] {
                if (applicationController) applicationController->refreshPermissions();
            },
            Qt::QueuedConnection);
    }

#endif

    void initializePermissionListeners() {
#ifdef Q_OS_ANDROID
        QJniEnvironment env;
        if (env.registerNativeMethods("io/github/hhhxiao/bedrockmap/ShizukuPermission",
                                      {{"onStateChanged", "()V", reinterpret_cast<void*>(onPermissionStateChanged)}})) {
            const auto context = QNativeInterface::QAndroidApplication::context();
            QJniObject::callStaticMethod<void>("io/github/hhhxiao/bedrockmap/ShizukuPermission", "initialize",
                                               "(Landroid/content/Context;)V", context.object());
        }
        env.checkAndClearExceptions();
        env.registerNativeMethods("io/github/hhhxiao/bedrockmap/RootPermission",
                                  {{"onStateChanged", "()V", reinterpret_cast<void*>(onPermissionStateChanged)}});
        env.checkAndClearExceptions();
#endif
    }
}  // namespace

AppController::AppController(std::unique_ptr<LogModel> logs, AppContext& context, QObject* parent)
    : QObject(parent),
      logs_(std::move(logs)),
      context_(context),
      settings_(this),
      runtime_settings_(this),
      worlds_(this),
      notifications_(this),
      update_checker_(this),
      sessions_(context, this) {
    if (applicationController) qFatal("Only one AppController can exist");
    if (!logs_) qFatal("AppController requires an initialized log model");
    applicationController = this;
    QSettings startupSettings(app_paths::configFile(), QSettings::IniFormat);
    startup_notice_required_ = !startupSettings.value(QStringLiteral("Startup/notice_acknowledged"), false).toBool();
    logs_->setParent(this);
    connect(&update_checker_, &UpdateChecker::updateAvailable, this,
            [this](const QString& version, const QString& notes, const QString& url) {
                finishUpdateCheck();
                emit updateAvailable(version, notes, url);
            });
    connect(&update_checker_, &UpdateChecker::upToDate, this, [this] {
        if (manual_update_check_) notifications_.info(tr("Already up to date."));
        finishUpdateCheck();
    });
    connect(&update_checker_, &UpdateChecker::checkFailed, this, [this](const QString&) {
        if (manual_update_check_) notifications_.error(tr("Unable to check for updates."));
        finishUpdateCheck();
    });
    sessions_.setWorldListModel(&worlds_);
    initializePermissionListeners();
    connect(&sessions_, &SessionController::activeSessionChanged, this, &AppController::activeSessionChanged);
    connect(&sessions_, &SessionController::activeIndexChanged, this, &AppController::workspaceIndexChanged);
    connect(&sessions_, &SessionController::closeRequested, this,
            [this](WorkspaceSession* session, int index) { emit sessionCloseRequested(session, index); });
    connect(&sessions_, &SessionController::sessionAboutToClose, this,
            [this](WorkspaceSession* session) { emit sessionAboutToClose(session); });
    connect(&sessions_, &SessionController::sessionAdded, this, [this](WorkspaceSession* session, int) {
        if (auto* nbt = qobject_cast<NbtSession*>(session)) {
            connect(nbt->document(), &NbtDocumentController::saved, this, [this] { notifications_.info(tr("Save successful")); });
        } else if (auto* world = qobject_cast<WorldSession*>(session)) {
            connect(world, &WorldSession::saved, this, [this] { notifications_.info(tr("Save successful")); });
            connect(world, &WorldSession::saveFailed, this, [this](const QString&) { notifications_.error(tr("Save failed")); });
            connect(world->voxelView(), &BlockVolumeController::importFinished, this,
                    [this] { notifications_.info(tr("Import successful")); });
        }
    });
    connect(&sessions_, &SessionController::sessionsChanged, this, [this] {
        if (sessions_.hasSessions()) return;
        setWorkspaceOpen(false);
        setCurrentPage(0);
    });
    connect(&sessions_, &SessionController::warningOccurred, this,
            [this](const QString& message, const QString& detail) { notifications_.warning(message, {}, detail); });
    connect(&sessions_, &SessionController::errorOccurred, this,
            [this](const QString& message, const QString& detail) { notifications_.error(message, {}, detail); });
}
AppController::~AppController() {
    // File workers own only value captures and must finish before shutdown.
    import_watcher_.waitForFinished();
    delete_watcher_.waitForFinished();
    export_watcher_.waitForFinished();
}

AppController* AppController::instance() { return applicationController; }

ResourceManager& AppController::resources() const { return context_.resources(); }

void AppController::acknowledgeStartupNotice() {
    if (!startup_notice_required_) return;
    QSettings startupSettings(app_paths::configFile(), QSettings::IniFormat);
    startupSettings.setValue(QStringLiteral("Startup/notice_acknowledged"), true);
    startupSettings.sync();
    startup_notice_required_ = false;
    emit startupNoticeRequiredChanged();
}

void AppController::checkForUpdates(bool manual) {
    if (!manual && !settings_.checkUpdate()) return;
    if (manual) manual_update_check_ = true;
    if (checking_for_updates_) return;
    checking_for_updates_ = true;
    emit checkingForUpdatesChanged();
    update_checker_.checkForUpdates();
}

void AppController::finishUpdateCheck() {
    checking_for_updates_ = false;
    manual_update_check_ = false;
    emit checkingForUpdatesChanged();
}

void AppController::setWorldTask(const QString& title, const QString& detail) {
    if (world_task_title_ == title && world_task_detail_ == detail) return;
    world_task_title_ = title;
    world_task_detail_ = detail;
    emit worldTaskChanged();
}

void AppController::setCurrentPage(int page) {
    // Application-level destinations only. World-internal data and tools live
    // inside the active workspace.
    page = qBound(0, page, 3);
    if (current_page_ == page) return;
    current_page_ = page;
    emit currentPageChanged();
}

void AppController::navigateToPage(int page) {
    setCurrentPage(page);
    setWorkspaceOpen(false);
}

bool AppController::allFilesPermission() const { return platform::hasAllFilesPermission(); }

void AppController::requestAllFilesPermission() { platform::requestAllFilesPermission(); }

QString AppController::shizukuPermission() const { return platform::shizukuPermission(); }

void AppController::requestShizukuPermission() { platform::requestShizukuPermission(); }

QString AppController::rootPermission() const { return platform::rootPermission(); }

void AppController::requestRootPermission() { platform::requestRootPermission(); }

void AppController::refreshPermissions() { emit permissionsChanged(); }

void AppController::selectWorkspace(int index) {
    if (!sessions_.sessions()->at(index)) return;
    sessions_.setActiveIndex(index);
    setWorkspaceOpen(true);
}
bool AppController::isWorldFolder(const QUrl& url) const {
    if (!url.isLocalFile()) return false;
    const QString target = platform::normalizeLocalPath(url.toLocalFile());
    // A synchronous local check for desktop drag-and-drop. Opening the database,
    // including privileged Android access, runs on the session's open worker.
    return !target.isEmpty() && LevelOperator::isCompleteWorld(target);
}

void AppController::openWorldFolder(const QUrl& url) {
    if (!checkWorldFileOperationIdle()) return;
    if (url.isEmpty()) return;
#ifdef Q_OS_ANDROID
    if (platform::isContentUri(url)) {
        if (importing_world_) return;
        const QString resolved = LevelOperator::resolveContentWorldFolder(url);
        if (!resolved.isEmpty())
            openWorld(resolved);
        else
            emit worldCopyRequested(url);
        return;
    }
#endif
    if (!url.isLocalFile()) {
        notifications_.error(tr("Import failed"));
        return;
    }
    openWorld(url.toLocalFile());
}

void AppController::openWorld(const QString& path, const QString& title) {
    if (!checkWorldFileOperationIdle()) return;
    // LevelDB uses the default Env, which cannot open privileged paths.
    if (io::isPrivilegedPath(path)) {
        importWorldToInternal(QUrl::fromLocalFile(path), false);
        return;
    }
    if (auto* session = sessions_.openWorld(path, title)) {
        if (auto* world = qobject_cast<WorldSession*>(session)) world->ensureTerrainData();
        setWorkspaceOpen(true);
    }
}

void AppController::copyWorldToInternal(const QUrl& url) { importWorldToInternal(url, false); }

bool AppController::saveImage(const QUrl& destination, const QImage& image) {
    if (destination.isEmpty() || image.isNull()) return false;
    const QString error = io::DocumentIo::writeStream(destination, [&image](QIODevice& device) {
        QImageWriter writer(&device, "PNG");
        return writer.write(image) ? QString() : writer.errorString();
    });
    return error.isEmpty();
}

void AppController::openMcworldFile(const QUrl& url) { importWorldToInternal(url, true); }

bool AppController::checkWorldFileOperationIdle() {
    if (deleting_world_) {
        notifications_.warning(tr("Wait for the current world deletion to finish."));
        return false;
    }
    if (exporting_world_) {
        notifications_.warning(tr("Wait for the current world export to finish."));
        return false;
    }
    return true;
}

bool AppController::hasOpenSessionInWorld(const QString& path) {
#ifdef Q_OS_WIN
    constexpr auto caseSensitivity = Qt::CaseInsensitive;
#else
    constexpr auto caseSensitivity = Qt::CaseSensitive;
#endif
    for (int i = 0; i < sessions_.sessions()->count(); ++i) {
        const auto* session = sessions_.sessions()->at(i);
        const QString openPath = platform::normalizeLocalPath(session->path());
        if (openPath.compare(path, caseSensitivity) == 0 || openPath.startsWith(path + QLatin1Char('/'), caseSensitivity)) return true;
    }
    return false;
}

void AppController::deleteWorld(const QString& path) {
    if (!checkWorldFileOperationIdle()) return;
    if (importing_world_) {
        notifications_.warning(tr("Wait for the current world import to finish."));
        return;
    }
    const QString target = platform::normalizeLocalPath(path);
    if (!LevelOperator::isInternalWorld(target)) {
        notifications_.error(tr("Only worlds in the app's internal storage can be deleted."));
        return;
    }
    if (hasOpenSessionInWorld(target)) {
        notifications_.warning(tr("Close the world and any files inside it before deleting it."));
        return;
    }

    deleting_world_ = true;
    emit deletingWorldChanged();
    notifications_.info(tr("Deleting world..."));
    delete_watcher_.disconnect(this);
    connect(&delete_watcher_, &QFutureWatcher<QString>::finished, this, [this] {
        const QString error = delete_watcher_.result();
        deleting_world_ = false;
        emit deletingWorldChanged();
        // Refresh also prunes deleted directories from recent-world history.
        // Refresh on failure too, because a partial deletion may invalidate it.
        worlds_.refresh();
        if (error.isEmpty())
            notifications_.info(tr("World deleted."));
        else
            notifications_.error(tr("World deletion failed."), {}, error);
    });
    delete_watcher_.setFuture(QtConcurrent::run([target] { return LevelOperator::deleteInternalWorld(target); }));
}

void AppController::exportWorld(const QString& path, const QUrl& destination) {
    if (!checkWorldFileOperationIdle()) return;
    if (importing_world_) {
        notifications_.warning(tr("Wait for the current world import to finish."));
        return;
    }
    const QString source = platform::normalizeLocalPath(path);
    if (source.isEmpty() || destination.isEmpty()) {
        notifications_.error(tr("Export failed"));
        return;
    }
    QUrl target = destination;
    if (target.isLocalFile()) {
        QString targetPath = platform::normalizeLocalPath(target.toLocalFile());
        if (targetPath.isEmpty()) {
            notifications_.error(tr("Export failed"));
            return;
        }
        if (QFileInfo(targetPath).suffix().isEmpty()) targetPath += QStringLiteral(".mcworld");
        target = QUrl::fromLocalFile(targetPath);
    }
    if (hasOpenSessionInWorld(source)) {
        notifications_.warning(tr("Close the world and any files inside it before exporting it."));
        return;
    }

    const QString export_name = QFileInfo(source).fileName();
    setWorldTask(tr("Exporting world"), export_name);
    exporting_world_ = true;
    emit exportingWorldChanged();
    export_watcher_.disconnect(this);
    connect(&export_watcher_, &QFutureWatcher<QString>::finished, this, [this, export_name] {
        QString error;
        try {
            error = export_watcher_.result();
        } catch (const std::exception& exception) {
            error = QString::fromUtf8(exception.what());
        } catch (...) {
            error = tr("The world export worker failed.");
        }
        exporting_world_ = false;
        emit exportingWorldChanged();
        if (error.isEmpty())
            notifications_.info(tr("Export successful"));
        else
            notifications_.error(tr("Export failed"), export_name, error);
    });
    export_watcher_.setFuture(QtConcurrent::run([source, target] { return LevelOperator::exportMcworld(source, target); }));
}

void AppController::openFile(const QUrl& url) {
    const QString fileName = io::DocumentIo::displayName(url);
    const QString suffix = QFileInfo(fileName).suffix();
    if (suffix.compare(QStringLiteral("mcworld"), Qt::CaseInsensitive) == 0)
        openMcworldFile(url);
    else if (suffix.compare(QStringLiteral("mcstructure"), Qt::CaseInsensitive) == 0)
        openMcstructureFile(url);
    else
        openNbtFile(url);
}

void AppController::importWorldToInternal(const QUrl& url, bool archive) {
    if (!checkWorldFileOperationIdle()) return;
    if (importing_world_) {
        notifications_.warning(tr("Wait for the current world import to finish."));
        return;
    }

    const QString import_name = io::DocumentIo::displayName(url);
    if (url.isEmpty()) {
        notifications_.error(tr("Import failed"), {}, tr("No source file was provided."));
        return;
    }
    const QString source = io::DocumentIo::identifier(url);
    if (source.isEmpty()) {
        notifications_.error(tr("Import failed"), import_name, tr("The selected file location could not be identified."));
        return;
    }

    const QString target = LevelOperator::targetForSource(source);
    if (target.isEmpty()) {
        notifications_.error(tr("Import failed"), import_name, tr("Could not create a destination path for the imported world."));
        return;
    }
    if (LevelOperator::isCompleteWorld(target)) {
        openWorld(target);
        return;
    }

    setWorldTask(archive ? tr("Extracting world archive") : tr("Copying world"), import_name);
    importing_world_ = true;
    emit importingWorldChanged();

    import_watcher_.disconnect(this);
    connect(&import_watcher_, &QFutureWatcher<QString>::finished, this, [this, target, import_name] {
        QString error;
        try {
            error = import_watcher_.result();
        } catch (const std::exception& exception) {
            error = QString::fromUtf8(exception.what());
        } catch (...) {
            error = tr("The world import worker failed.");
        }
        importing_world_ = false;
        emit importingWorldChanged();
        if (!error.isEmpty()) {
            notifications_.error(tr("Import failed"), import_name, error);
            return;
        }
        notifications_.info(tr("Import successful"));
        openWorld(target);
    });
    import_watcher_.setFuture(QtConcurrent::run([url, target, archive] {
        return archive ? LevelOperator::extractToInternal(url, target) : LevelOperator::copyToInternal(url, target);
    }));
}

void AppController::openMcstructureFile(const QUrl& url) {
    if (!checkWorldFileOperationIdle()) return;
    if (sessions_.openMcstructureFile(url)) setWorkspaceOpen(true);
}

void AppController::openNbtFile(const QUrl& url) {
    if (!checkWorldFileOperationIdle()) return;
    if (sessions_.openNbtFile(url)) setWorkspaceOpen(true);
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
        notifications_.warning(tr("Could not open the containing folder."), {}, path);
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(target)))
        notifications_.warning(tr("Could not open the containing folder."), {}, target);
}

bool AppController::requestCloseFirstDirtySession() {
    auto* session = sessions_.firstDirtySession();
    if (!session) return false;
    sessions_.closeSession(session);
    return true;
}

void AppController::openConfigDirectory() {
    const QString path = app_paths::configDirectory();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        notifications_.warning(tr("Could not open the configuration folder."), {}, path);
}

void AppController::setWorkspaceOpen(bool open) {
    if (workspace_open_ == open) return;
    workspace_open_ = open;
    emit workspaceOpenChanged();
    sessions_.setActive(open);
}
