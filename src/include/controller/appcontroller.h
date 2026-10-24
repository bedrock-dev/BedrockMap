#ifndef BEDROCKMAP_APPCONTROLLER_H
#define BEDROCKMAP_APPCONTROLLER_H

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QUrl>

#include "controller/sessioncontroller.h"

class NotificationCenter;
class WorldListModel;

/// Application shell. Session ownership and lifecycle live in
/// SessionController; this class adds page navigation and platform file import.
class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(bool workspaceOpen READ workspaceOpen NOTIFY workspaceOpenChanged)
    Q_PROPERTY(int workspaceIndex READ workspaceIndex NOTIFY workspaceIndexChanged)
    Q_PROPERTY(WorkspaceSessionModel* openSessions READ openSessions CONSTANT)
    Q_PROPERTY(QObject* activeSession READ activeSession NOTIFY activeSessionChanged)
    Q_PROPERTY(bool importingWorld READ importingWorld NOTIFY importingWorldChanged)

   public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    [[nodiscard]] int currentPage() const { return current_page_; }
    [[nodiscard]] bool workspaceOpen() const { return workspace_open_; }
    [[nodiscard]] int workspaceIndex() const { return sessions_.activeIndex(); }
    [[nodiscard]] WorkspaceSessionModel* openSessions() { return sessions_.sessions(); }
    [[nodiscard]] QObject* activeSession() const { return sessions_.activeSession(); }
    [[nodiscard]] bool importingWorld() const { return importing_world_; }

    void setNotificationCenter(NotificationCenter* notifications) { notifications_ = notifications; }
    void setWorldListModel(WorldListModel* model) { sessions_.setWorldListModel(model); }

    void setCurrentPage(int page);
    Q_INVOKABLE void navigateToPage(int page);
    Q_INVOKABLE void selectWorkspace(int index);
    Q_INVOKABLE void openWorld(const QString& path, const QString& title = {});
    Q_INVOKABLE void openWorldFolder(const QString& path);
    /// Copies a SAF world to the app's internal directory and opens it.
    Q_INVOKABLE void copySafWorld(const QString& path);
    Q_INVOKABLE void openMcstructureFile(const QString& path);
    Q_INVOKABLE void openNbtFile(const QString& path);
    Q_INVOKABLE void newNbtDocument();
    Q_INVOKABLE void closeSession(int index);
    Q_INVOKABLE void discardSession(int index);
    Q_INVOKABLE void discardSessionObject(QObject* session);
    Q_INVOKABLE void revealPath(const QString& path);
    /// Close the first dirty session and prompt through sessionCloseRequested().
    /// Returns false when no dirty session remains.
    [[nodiscard]] Q_INVOKABLE bool requestCloseFirstDirtySession();
    Q_INVOKABLE void openConfigDirectory();

    [[nodiscard]] Q_INVOKABLE QString defaultWorldFolder() const;
    [[nodiscard]] Q_INVOKABLE QUrl defaultWorldFolderUrl() const;
    [[nodiscard]] Q_INVOKABLE bool isWorldFolder(const QString& path) const;
    [[nodiscard]] Q_INVOKABLE int firstDirtySessionIndex() const { return sessions_.firstDirtyIndex(); }

   signals:
    void currentPageChanged();
    void workspaceOpenChanged();
    void workspaceIndexChanged();
    void activeSessionChanged();
    void importingWorldChanged();
    void safWorldCopyRequested(const QString& path);
    void worldSourceDiscovered(const QString& path);
    void sessionCloseRequested(QObject* session, int index);
    void sessionAboutToClose(QObject* session);

   private:
    void setWorkspaceOpen(bool open);

    int current_page_{0};
    bool workspace_open_{false};
    SessionController sessions_;
    NotificationCenter* notifications_{nullptr};
    QFutureWatcher<QString> import_watcher_;
    bool importing_world_{false};
};

#endif  // BEDROCKMAP_APPCONTROLLER_H
