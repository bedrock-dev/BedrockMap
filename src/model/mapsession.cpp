#include "model/mapsession.h"

MapSession::MapSession(QObject* parent) : QObject(parent), view_(this) {
    view_.setScale(MapView::DEFAULT_SCALE, QPointF());
    connect(&view_, &MapView::selectionChanged, this, &MapSession::selectionChanged);
}

bool MapSession::toggleOther(RenderOption::OtherType other) {
    const bool now = !view_.options().getOther(other);
    view_.setOther(other, now);
    return now;
}
