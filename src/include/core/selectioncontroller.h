#ifndef BEDROCKMAP_SELECTIONCONTROLLER_H
#define BEDROCKMAP_SELECTIONCONTROLLER_H

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPoint>
#include <QRect>
#include <QRegion>

#include "bedrock_key.h"
#include "selectionregion.h"
#include "utils.h"

/// Chunk selection state plus the drag that produces it. Shared by every map
/// renderer, so a selection made on one is shown - and editable - on the others.
class SelectionController {
   public:
    using Mode = SelectionRegion::Mode;

    bool isEmpty() const { return region_.isEmpty() && !dragging_; }
    bool isDragging() const { return dragging_; }
    const QRegion& region() const { return region_.region(); }
    Mode mode() const { return region_.mode(); }
    void setMode(Mode m) { region_.setMode(m); }
    void clear() { region_ = SelectionRegion(); }
    size_t rectCount() const { return region_.region().rectCount(); }
    int chunkCount() const { return region_.chunkCount(); }

    void startDrag(const bl::chunk_pos& p) {
        dragging_ = true;
        drag_start_ = p;
        drag_current_ = p;
    }
    void updateDrag(const bl::chunk_pos& p) { drag_current_ = p; }
    QRect finishDrag() {
        dragging_ = false;
        auto [mn, mx] = normalize_chunk_range(drag_start_, drag_current_);
        QRect rect(mn.x, mn.z, mx.x - mn.x + 1, mx.z - mn.z + 1);
        region_.applyRect(rect);
        return rect;
    }
    /// The rectangle currently being dragged out, in chunk coordinates.
    [[nodiscard]] QRect dragRect() const {
        auto [mn, mx] = normalize_chunk_range(drag_start_, drag_current_);
        return QRect(mn.x, mn.z, mx.x - mn.x + 1, mx.z - mn.z + 1);
    }

    bool contains(const QPoint& p) const { return !region_.isEmpty() && region_.region().contains(p); }

    /// Draws into a painter whose transform maps chunk units to pixels.
    void draw(QPainter* p, qreal scaleLevel) const {
        if (region_.isEmpty() && !dragging_) return;

        QPainterPath path;
        for (const auto& r : region_.region()) {
            path.addRect(QRectF(static_cast<qreal>(r.x()), static_cast<qreal>(r.y()), static_cast<qreal>(r.width()),
                                static_cast<qreal>(r.height())));
        }
        path = path.simplified();
        p->fillPath(path, QColor(218, 255, 251, 80));
        p->strokePath(path, QPen(QColor(218, 255, 251), 2.0 / scaleLevel));

        if (dragging_) {
            p->setPen(QPen(QColor(218, 255, 251), 3.0 / scaleLevel, Qt::DashLine));
            p->setBrush(QColor(218, 255, 251, 60));
            p->drawRect(QRectF(dragRect()));
        }
    }

   private:
    SelectionRegion region_;
    bl::chunk_pos drag_start_;
    bl::chunk_pos drag_current_;
    bool dragging_{false};
};

#endif  // BEDROCKMAP_SELECTIONCONTROLLER_H
