#include "controller/sessioncontroller.h"

#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <algorithm>

#include "base/platform.h"
#include "controller/mcstructuresession.h"
#include "controller/nbtsession.h"
#include "io/documentio.h"
#include "model/worldlistmodel.h"

WorkspaceSessionModel::WorkspaceSessionModel(QObject* parent) : QAbstractListModel(parent) {}

int WorkspaceSessionModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : sessions_.size(); }

QVariant WorkspaceSessionModel::data(const QModelIndex& index, int role) const {
    auto* session = at(index.row());
    if (!session) return {};
    switch (role) {
        case Qt::DisplayRole:
        case TitleRole:
            return session->title();
        case PathRole:
            return session->path();
        case SessionIndexRole:
            return index.row();
        case SessionRole:
            return QVariant::fromValue<QObject*>(session);
        case DirtyRole:
            return session->isDirty();
        case KindRole:
            return session->kind();
        default:
            return {};
    }
}

QHash<int, QByteArray> WorkspaceSessionModel::roleNames() const {
    return {{PathRole, "path"},       {TitleRole, "title"}, {SessionIndexRole, "sessionIndex"},
            {SessionRole, "session"}, {DirtyRole, "dirty"}, {KindRole, "kind"}};
}

int WorkspaceSessionModel::addSession(WorkspaceSession* session) {
    if (!session) return -1;
    const int row = sessions_.size();
    connect(session, &WorkspaceSession::titleChanged, this, [this, session] {
        const int row = sessions_.indexOf(session);
        if (row >= 0) emit dataChanged(index(row), index(row), {TitleRole});
    });
    connect(session, &WorkspaceSession::dirtyChanged, this, [this, session] {
        const int row = sessions_.indexOf(session);
        if (row >= 0) emit dataChanged(index(row), index(row), {DirtyRole});
    });
    connect(session, &WorkspaceSession::pathChanged, this, [this, session] {
        const int row = sessions_.indexOf(session);
        if (row >= 0) emit dataChanged(index(row), index(row), {PathRole});
    });
    beginInsertRows({}, row, row);
    sessions_.append(session);
    endInsertRows();
    emit countChanged();
    return row;
}

void WorkspaceSessionModel::removeSession(int index) {
    if (index < 0 || index >= sessions_.size()) return;
    disconnect(sessions_.at(index), nullptr, this, nullptr);
    beginRemoveRows({}, index, index);
    sessions_.removeAt(index);
    endRemoveRows();
    // Shifted rows keep their delegates, so refresh the row-dependent role.
    if (index < sessions_.size()) emit dataChanged(this->index(index), this->index(sessions_.size() - 1), {SessionIndexRole});
    emit countChanged();
}

WorkspaceSession* WorkspaceSessionModel::at(int index) const {
    return index >= 0 && index < sessions_.size() ? sessions_.at(index) : nullptr;
}

int WorkspaceSessionModel::indexOf(WorkspaceSession* session) const { return sessions_.indexOf(session); }

SessionController::SessionController(AppContext& context, QObject* parent)
    : QObject(parent), open_sessions_(this), context_(context) {}

SessionController::~SessionController() {
    for (auto& session : sessions_) session->close();
}

WorkspaceSession* SessionController::activeSession() const { return open_sessions_.at(active_index_); }

int SessionController::firstDirtyIndex() const {
    for (int i = 0; i < open_sessions_.count(); ++i) {
        const auto* session = open_sessions_.at(i);
        if (session && session->isDirty()) return i;
    }
    return -1;
}

WorkspaceSession* SessionController::firstDirtySession() const {
    const int index = firstDirtyIndex();
    return index >= 0 ? open_sessions_.at(index) : nullptr;
}

WorkspaceSession* SessionController::openWorld(const QString& path, const QString& title) {
    const QString target = platform::normalizeLocalPath(path);
    if (target.isEmpty()) {
        emit warningOccurred(tr("World path is not available."), {});
        return nullptr;
    }

    for (int i = 0; i < open_sessions_.count(); ++i) {
        auto* session = open_sessions_.at(i);
        if (session && session->kind() == QStringLiteral("world") && session->path() == target) {
            setActiveIndex(i);
            return session;
        }
    }

    auto session = std::make_unique<WorldSession>(target, title, context_, this);
    auto* session_ptr = session.get();
    connect(session_ptr, &WorldSession::opened, this, [this, session_ptr] {
        if (world_model_) world_model_->recordRecentWorld(session_ptr->path());
    });
    connect(session_ptr, &WorldSession::stateChanged, this, [this, session_ptr] {
        if (!session_ptr->failed()) return;
        // Finish state notifications before removing the failed workspace.
        QTimer::singleShot(0, session_ptr, [this, session_ptr] {
            const int row = open_sessions_.indexOf(session_ptr);
            if (row < 0 || !session_ptr->failed()) return;
            QString name = session_ptr->title();
            if (name.isEmpty() || name == QStringLiteral("World workspace")) name = QFileInfo(session_ptr->path()).fileName();
            const QString message = session_ptr->errorCode() == QStringLiteral("world-write-denied")
                                        ? tr("Could not open %1: the world directory is not writable.").arg(name)
                                    : session_ptr->errorCode() == QStringLiteral("database-open-failed")
                                        ? tr("Could not open %1: the world database is locked by another program or unreadable.").arg(name)
                                        : tr("Could not open %1 (%2).").arg(name, session_ptr->errorCode());
            emit errorOccurred(message, session_ptr->path());
            removeSessionAt(row);
        });
    });

    appendSession(std::move(session));
    session_ptr->open();
    return session_ptr;
}

WorkspaceSession* SessionController::openMcstructureFile(const QUrl& url) {
    if (url.isEmpty()) return nullptr;
    const QString path = io::DocumentIo::identifier(url);
    for (int i = 0; i < open_sessions_.count(); ++i) {
        auto* session = open_sessions_.at(i);
        if (session && session->kind() == QStringLiteral("mcstructure") && session->path() == path) {
            setActiveIndex(i);
            return session;
        }
    }
    auto session = std::make_unique<McstructureSession>(url, io::DocumentIo::displayName(url), context_, this);
    auto* sessionPtr = session.get();
    connect(sessionPtr->controller(), &BlockVolumeController::loadFailed, this,
            [this, path](const QString& error) { emit errorOccurred(error, path); });
    appendSession(std::move(session));
    sessionPtr->open();
    return sessionPtr;
}

WorkspaceSession* SessionController::openNbtFile(const QUrl& url) {
    if (url.isEmpty()) return nullptr;
    // Provider display names can carry an extension even when the URI itself
    // only contains a document ID. Keep the original URI for file access.
    const QString fileName = io::DocumentIo::displayName(url);
    if (QFileInfo(fileName).suffix().compare(QStringLiteral("mcstructure"), Qt::CaseInsensitive) == 0) return openMcstructureFile(url);
    const QString path = io::DocumentIo::identifier(url);
    for (int i = 0; i < open_sessions_.count(); ++i) {
        auto* session = open_sessions_.at(i);
        if (session && session->kind() == QStringLiteral("nbt") && session->path() == path) {
            setActiveIndex(i);
            return session;
        }
    }

    auto session = std::make_unique<NbtSession>(path, fileName, this);
    auto* session_ptr = session.get();
    if (!session_ptr->loadFile(url)) {
        emit errorOccurred(session_ptr->document()->lastError(), path);
        return nullptr;
    }
    appendSession(std::move(session));
    return session_ptr;
}

WorkspaceSession* SessionController::newNbtDocument() {
    auto session = std::make_unique<NbtSession>(QString(), QStringLiteral("Untitled"), this);
    auto* session_ptr = session.get();
    session_ptr->createNew();
    appendSession(std::move(session));
    return session_ptr;
}

int SessionController::appendSession(std::unique_ptr<WorkspaceSession> session) {
    auto* session_ptr = session.get();
    const int row = open_sessions_.addSession(session_ptr);
    sessions_.push_back(std::move(session));
    setActiveIndex(row);
    // The first inserted session reuses index 0, so setActiveIndex() has
    // nothing to change and would otherwise leave activeSession unset in QML.
    emit activeSessionChanged();
    emit sessionsChanged();
    emit sessionAdded(session_ptr, row);
    return row;
}

void SessionController::setActiveIndex(int index) {
    const int count = open_sessions_.count();
    if (count == 0) return;
    const int bounded = std::clamp(index, 0, count - 1);
    if (active_index_ != bounded) {
        active_index_ = bounded;
        emit activeIndexChanged();
        emit activeSessionChanged();
    }
    updateSessionActivation();
}

void SessionController::setActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    updateSessionActivation();
}

void SessionController::closeSession(int index) {
    auto* session = open_sessions_.at(index);
    if (!session) return;
    if (session->isDirty()) {
        emit closeRequested(session, index);
        return;
    }
    removeSessionAt(index);
}

void SessionController::closeSession(WorkspaceSession* session) {
    if (!session) return;
    const int index = open_sessions_.indexOf(session);
    if (index < 0) return;
    if (session->isDirty()) {
        emit closeRequested(session, index);
        return;
    }
    removeSessionAt(index);
}

void SessionController::discardSession(int index) { removeSessionAt(index); }

void SessionController::discardSession(WorkspaceSession* session) {
    if (!session) return;
    const int index = open_sessions_.indexOf(session);
    if (index >= 0) removeSessionAt(index);
}

void SessionController::updateSessionActivation() {
    for (int i = 0; i < open_sessions_.count(); ++i) {
        if (auto* session = open_sessions_.at(i)) session->setActive(active_ && i == active_index_);
    }
}

void SessionController::removeSessionAt(int index) {
    auto* session = open_sessions_.at(index);
    if (!session) return;
    auto* previous_active = activeSession();
    session->setActive(false);
    // Cached pages must detach while their controllers and models still exist.
    emit sessionAboutToClose(session);
    open_sessions_.removeSession(index);
    std::unique_ptr<WorkspaceSession> closing_session;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it->get() == session) {
            closing_session = std::move(*it);
            sessions_.erase(it);
            break;
        }
    }

    int next = 0;
    if (open_sessions_.count() > 0) {
        next = active_index_;
        if (index < next)
            --next;
        else if (index == next)
            next = std::min(index, open_sessions_.count() - 1);
        next = std::clamp(next, 0, open_sessions_.count() - 1);
    }
    if (next != active_index_) {
        active_index_ = next;
        emit activeIndexChanged();
    }
    // Replacing a tab at the same index still changes the session identity.
    if (activeSession() != previous_active) emit activeSessionChanged();
    updateSessionActivation();
    emit sessionsChanged();

    session->close();
    if (closing_session) {
        // A QML callback or an async completion may still be on this object's
        // stack. The QObject parent also owns it if the event loop stops first.
        closing_session.release()->deleteLater();
    }
}
