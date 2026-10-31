#ifndef BEDROCKMAP_SESSIONCONTROLLER_H
#define BEDROCKMAP_SESSIONCONTROLLER_H

#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <memory>
#include <vector>

#include "controller/worldsession.h"

class WorldListModel;
class ResourceManager;

class WorkspaceSessionModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
   public:
    enum Role { PathRole = Qt::UserRole + 1, TitleRole, SessionIndexRole, SessionRole, DirtyRole, KindRole };
    explicit WorkspaceSessionModel(QObject* parent);

    [[nodiscard]] int count() const { return sessions_.size(); }
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    int addSession(WorkspaceSession* session);
    void removeSession(int index);
    [[nodiscard]] WorkspaceSession* at(int index) const;
    [[nodiscard]] int indexOf(WorkspaceSession* session) const;

   signals:
    void countChanged();

   private:
    QList<WorkspaceSession*> sessions_;
};

/// Owns every open workspace session: worlds, NBT documents and future
/// structure sessions. It contains no frontend page code.
class SessionController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(WorkspaceSessionModel* sessions READ sessions CONSTANT)
    Q_PROPERTY(WorkspaceSession* activeSession READ activeSession NOTIFY activeSessionChanged)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY activeIndexChanged)

   public:
    explicit SessionController(QObject* parent = nullptr);
    ~SessionController() override;

    void setWorldListModel(WorldListModel* model) { world_model_ = model; }
    void setResourceManager(ResourceManager* resources) { resources_ = resources; }
    [[nodiscard]] WorkspaceSessionModel* sessions() { return &open_sessions_; }
    [[nodiscard]] WorkspaceSession* activeSession() const;
    [[nodiscard]] int activeIndex() const { return active_index_; }
    [[nodiscard]] int firstDirtyIndex() const;
    [[nodiscard]] WorkspaceSession* firstDirtySession() const;

    WorkspaceSession* openWorld(const QString& path, const QString& title = {});
    WorkspaceSession* openMcstructureFile(const QUrl& url);
    WorkspaceSession* openNbtFile(const QUrl& url);
    WorkspaceSession* newNbtDocument();
    void setActiveIndex(int index);
    void setActive(bool active);
    void closeSession(int index);
    void closeSession(WorkspaceSession* session);
    void discardSession(int index);
    void discardSession(WorkspaceSession* session);

    bool hasSessions() const { return open_sessions_.count() > 0; }

   signals:
    void activeSessionChanged();
    void activeIndexChanged();
    void sessionsChanged();
    void sessionAdded(WorkspaceSession* session, int index);
    void sessionAboutToClose(WorkspaceSession* session);
    void closeRequested(WorkspaceSession* session, int index);
    void warningOccurred(const QString& message, const QString& detail);
    void errorOccurred(const QString& message, const QString& detail);

   private:
    void removeSessionAt(int index);
    void updateSessionActivation();
    int appendSession(std::unique_ptr<WorkspaceSession> session);

    WorkspaceSessionModel open_sessions_;
    WorldListModel* world_model_{nullptr};
    ResourceManager* resources_{nullptr};
    std::vector<std::unique_ptr<WorkspaceSession>> sessions_;
    int active_index_{0};
    bool active_{false};
};

#endif  // BEDROCKMAP_SESSIONCONTROLLER_H
