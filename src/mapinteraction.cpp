#include "mapinteraction.h"

#include <QWidget>
#include <cmath>

#include "loguru/loguru.hpp"
#include "mapview.h"

namespace {
    // Arrow-key pan speed, per 16 ms tick, in view pixels.
    constexpr qreal PAN_SPEED = 20.0 / 60.0;
}  // namespace

MapInteraction::MapInteraction(MapView* view, QObject* parent) : QObject(parent), view_(view) {
    // Keyboard pan timer: ~60fps for smooth arrow-key movement.
    pan_timer_ = new QTimer(this);
    pan_timer_->setInterval(16);
    connect(pan_timer_, &QTimer::timeout, this, &MapInteraction::onPanTick);
}

void MapInteraction::setView(MapView* view) {
    reset();
    view_ = view;
}

bool MapInteraction::isPanningWithKeys() const { return !pressed_keys_.isEmpty(); }

QSize MapInteraction::viewportSize() const {
    if (auto* widget = qobject_cast<QWidget*>(parent())) return widget->size();
    return view_ ? view_->viewportSize() : QSize();
}

void MapInteraction::reset() {
    panning_ = false;
    pressed_keys_.clear();
    if (pan_timer_) pan_timer_->stop();
}

void MapInteraction::onPanTick() {
    if (!view_) return;
    // Dragging the view by a positive delta moves the world the same way, so the
    // arrow keys are inverted relative to the direction the map travels.
    QPointF delta;
    if (pressed_keys_.contains(Qt::Key_Left)) delta += QPointF(PAN_SPEED, 0);
    if (pressed_keys_.contains(Qt::Key_Right)) delta += QPointF(-PAN_SPEED, 0);
    if (pressed_keys_.contains(Qt::Key_Up)) delta += QPointF(0, PAN_SPEED);
    if (pressed_keys_.contains(Qt::Key_Down)) delta += QPointF(0, -PAN_SPEED);
    if (delta.isNull()) return;
    view_->translate(delta);
}

bool MapInteraction::mousePress(QMouseEvent* event) {
    if (!view_) return false;
    if (event->button() == Qt::LeftButton) {
        panning_ = true;
        last_drag_pos_ = event->position();
        return true;
    }
    if (event->button() == Qt::MiddleButton) {
        view_->beginSelectionDrag(view_->chunkPosAt(event->position(), viewportSize()));
        return true;
    }
    return false;
}

bool MapInteraction::mouseMove(QMouseEvent* event) {
    if (!view_) return false;

    if (event->buttons() & Qt::LeftButton) {
        const QPointF pos = event->position();
        if (!panning_) {
            panning_ = true;
            last_drag_pos_ = pos;
            return false;
        }
        const QPointF delta_screen = pos - last_drag_pos_;
        last_drag_pos_ = pos;
        const qreal scale = std::abs(view_->scale());
        if (scale > 0.0) view_->translate(delta_screen / scale);
        return true;
    }

    if (event->buttons() & Qt::MiddleButton) {
        const auto cp = view_->chunkPosAt(event->position(), viewportSize());
        if (!view_->selection().isDragging()) {
            view_->beginSelectionDrag(cp);
        } else {
            view_->updateSelectionDrag(cp);
        }
        return true;
    }

    if (event->buttons() & Qt::RightButton) return false;

    const QPoint block = view_->blockPosAt(event->position(), viewportSize());
    emit cursorBlockChanged(block.x(), block.y(), view_->dim());
    return false;
}

bool MapInteraction::mouseRelease(QMouseEvent* event) {
    if (!view_) return false;

    if (event->button() == Qt::LeftButton) {
        const bool was_panning = panning_;
        panning_ = false;
        return was_panning;
    }

    if (event->button() == Qt::MiddleButton && view_->selection().isDragging()) {
        const QRect rect = view_->selection().dragRect();
        view_->finishSelectionDrag();
        LOG_F(INFO, "Selection applied: mode=%d rect=(%d,%d,%d,%d) total=%d rects", static_cast<int>(view_->selection().mode()), rect.x(),
              rect.y(), rect.width(), rect.height(), static_cast<int>(view_->selection().rectCount()));
        return true;
    }

    return false;
}

bool MapInteraction::wheel(QWheelEvent* event) {
    if (!view_) return false;
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->accept();
        return true;
    }
    view_->zoomToAdjacentLevel(delta > 0 ? 1 : -1, event->position());
    event->accept();
    return true;
}

bool MapInteraction::keyPress(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
            pressed_keys_.insert(event->key());
            if (!pan_timer_->isActive()) pan_timer_->start();
            event->accept();
            return true;
        default:
            return false;
    }
}

bool MapInteraction::keyRelease(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
            pressed_keys_.remove(event->key());
            if (pressed_keys_.isEmpty()) pan_timer_->stop();
            event->accept();
            return true;
        default:
            return false;
    }
}
