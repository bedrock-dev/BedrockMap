#include "controller/workspacesession.h"

#include <utility>

WorkspaceSession::WorkspaceSession(QString kind, QString path, QString title, QObject* parent)
    : QObject(parent), kind_(std::move(kind)), path_(std::move(path)), title_(std::move(title)) {}

void WorkspaceSession::setTitle(const QString& title) {
    if (title_ == title) return;
    title_ = title;
    emit titleChanged();
}

void WorkspaceSession::setActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    emit activeChanged();
}

void WorkspaceSession::setPath(const QString& path) {
    if (path_ == path) return;
    path_ = path;
    emit pathChanged();
}

void WorkspaceSession::setDirty(bool dirty) {
    if (dirty_ == dirty) return;
    dirty_ = dirty;
    emit dirtyChanged();
}

void WorkspaceSession::setSaving(bool saving) {
    if (saving_ == saving) return;
    saving_ = saving;
    emit savingChanged();
}
