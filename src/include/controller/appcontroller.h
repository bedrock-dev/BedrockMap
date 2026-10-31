#ifndef BEDROCKMAP_APPCONTROLLER_H
#define BEDROCKMAP_APPCONTROLLER_H

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>
#include <QUrl>
#include <memory>

#include "base/updatechecker.h"
#include "controller/sessioncontroller.h"
#include "controller/settingsviewmodel.h"
#include "model/logmodel.h"
#include "model/notifications.h"
#include "model/worldlistmodel.h"

class ResourceManager;

/// Application services and workspace operations. Session ownership and
/// lifecycle live in SessionController; UI objects remain in the frontend.
class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(SettingsViewModel* settings READ settings CONSTANT)
    Q_PROPERTY(WorldListModel* worlds READ worlds CONSTANT)
    Q_PROPERTY(NotificationCenter* notifications READ notifications CONSTANT)
    Q_PROPERTY(LogModel* logs READ logs CONSTANT)
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(bool workspaceOpen READ workspaceOpen NOTIFY workspaceOpenChanged)
    Q_PROPERTY(int workspaceIndex READ workspaceIndex NOTIFY workspaceIndexChanged)
    Q_PROPERTY(WorkspaceSessionModel* openSessions READ openSessions CONSTANT)
    Q_PROPERTY(QObject* activeSession READ activeSession NOTIFY activeSessionChanged)
    Q_PROPERTY(bool importingWorld READ importingWorld NOTIFY importingWorldChanged)
    Q_PROPERTY(bool deletingWorld READ deletingWorld NOTIFY deletingWorldChanged)
    Q_PROPERTY(bool exportingWorld READ exportingWorld NOTIFY exportingWorldChanged)
    Q_PROPERTY(bool checkingForUpdates READ checkingForUpdates NOTIFY checkingForUpdatesChanged)
    Q_PROPERTY(bool startupNoticeRequired READ startupNoticeRequired NOTIFY startupNoticeRequiredChanged)
    Q_PROPERTY(bool allFilesPermission READ allFilesPermission NOTIFY permissionsChanged)
    Q_PROPERTY(QString shizukuPermission READ shizukuPermission NOTIFY permissionsChanged)
    Q_PROPERTY(QString rootPermission READ rootPermission NOTIFY permissionsChanged)

   public:
    explicit AppController(std::unique_ptr<LogModel> logs, ResourceManager& resources, QObject* parent = nullptr);
    ~AppController() override;

    [[nodiscard]] static AppController* instance();
    [[nodiscard]] ResourceManager& resources() const { return *resources_; }
    [[nodiscard]] SettingsViewModel* settings() { return &settings_; }
    [[nodiscard]] WorldListModel* worlds() { return &worlds_; }
    [[nodiscard]] NotificationCenter* notifications() { return &notifications_; }
    [[nodiscard]] LogModel* logs() const { return logs_.get(); }

    [[nodiscard]] int currentPage() const { return current_page_; }
    [[nodiscard]] bool workspaceOpen() const { return workspace_open_; }
    [[nodiscard]] int workspaceIndex() const { return sessions_.activeIndex(); }
    [[nodiscard]] WorkspaceSessionModel* openSessions() { return sessions_.sessions(); }
    [[nodiscard]] QObject* activeSession() const { return sessions_.activeSession(); }
    [[nodiscard]] bool importingWorld() const { return importing_world_; }
    [[nodiscard]] bool deletingWorld() const { return deleting_world_; }
    [[nodiscard]] bool exportingWorld() const { return exporting_world_; }
    [[nodiscard]] bool checkingForUpdates() const { return checking_for_updates_; }
    [[nodiscard]] bool startupNoticeRequired() const { return startup_notice_required_; }
    Q_INVOKABLE void acknowledgeStartupNotice();
    [[nodiscard]] bool allFilesPermission() const;
    Q_INVOKABLE void requestAllFilesPermission();
    [[nodiscard]] QString shizukuPermission() const;
    Q_INVOKABLE void requestShizukuPermission();
    [[nodiscard]] QString rootPermission() const;
    Q_INVOKABLE void requestRootPermission();
    Q_INVOKABLE void refreshPermissions();

    void setCurrentPage(int page);
    Q_INVOKABLE void navigateToPage(int page);
    Q_INVOKABLE void selectWorkspace(int index);
    Q_INVOKABLE void openWorld(const QString& path, const QString& title = {});
    Q_INVOKABLE void openWorldFolder(const QUrl& url);
    /// Copies a world to the app's internal directory and opens it.
    Q_INVOKABLE void copyWorldToInternal(const QUrl& url);
    Q_INVOKABLE bool saveImage(const QUrl& destination, const QImage& image);
    Q_INVOKABLE void openMcworldFile(const QUrl& url);
    /// Called after the frontend confirms permanent deletion of an internal world.
    Q_INVOKABLE void deleteWorld(const QString& path);
    Q_INVOKABLE void exportWorld(const QString& path, const QUrl& destination);
    /// Dispatches a standalone file to world import, structure preview or NBT.
    Q_INVOKABLE void openFile(const QUrl& url);
    Q_INVOKABLE void openMcstructureFile(const QUrl& url);
    Q_INVOKABLE void openNbtFile(const QUrl& url);
    Q_INVOKABLE void newNbtDocument();
    Q_INVOKABLE void closeSession(int index);
    Q_INVOKABLE void discardSession(int index);
    Q_INVOKABLE void discardSessionObject(QObject* session);
    Q_INVOKABLE void revealPath(const QString& path);
    /// Close the first dirty session and prompt through sessionCloseRequested().
    /// Returns false when no dirty session remains.
    [[nodiscard]] Q_INVOKABLE bool requestCloseFirstDirtySession();
    Q_INVOKABLE void openConfigDirectory();
    Q_INVOKABLE void checkForUpdates(bool manual = false);

    [[nodiscard]] Q_INVOKABLE bool isWorldFolder(const QUrl& url) const;
    [[nodiscard]] Q_INVOKABLE int firstDirtySessionIndex() const { return sessions_.firstDirtyIndex(); }

   signals:
    void currentPageChanged();
    void workspaceOpenChanged();
    void workspaceIndexChanged();
    void activeSessionChanged();
    void importingWorldChanged();
    void deletingWorldChanged();
    void exportingWorldChanged();
    void checkingForUpdatesChanged();
    void startupNoticeRequiredChanged();
    void permissionsChanged();
    void worldCopyRequested(const QUrl& url);
    void sessionCloseRequested(QObject* session, int index);
    void sessionAboutToClose(QObject* session);
    void updateAvailable(const QString& newVersion, const QString& releaseNotes, const QString& htmlUrl);

   private:
    void finishUpdateCheck();
    void setWorkspaceOpen(bool open);
    void importWorldToInternal(const QUrl& url, bool archive);
    [[nodiscard]] bool checkWorldFileOperationIdle();
    [[nodiscard]] bool hasOpenSessionInWorld(const QString& path);

    // Destroy sessions before their services, and logging last.
    std::unique_ptr<LogModel> logs_;
    ResourceManager* resources_{nullptr};
    SettingsViewModel settings_;
    WorldListModel worlds_;
    NotificationCenter notifications_;
    UpdateChecker update_checker_;
    bool checking_for_updates_{false};
    bool manual_update_check_{false};
    bool startup_notice_required_{true};
    int current_page_{0};
    bool workspace_open_{false};
    SessionController sessions_;
    QFutureWatcher<QString> import_watcher_;
    bool importing_world_{false};
    QFutureWatcher<QString> delete_watcher_;
    bool deleting_world_{false};
    QFutureWatcher<QString> export_watcher_;
    bool exporting_world_{false};
};

#endif  // BEDROCKMAP_APPCONTROLLER_H
