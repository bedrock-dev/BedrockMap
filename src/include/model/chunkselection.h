#ifndef BEDROCKMAP_CHUNKSELECTION_H
#define BEDROCKMAP_CHUNKSELECTION_H

#include <QPoint>
#include <QRect>
#include <QRegion>
#include <algorithm>
#include <utility>

#include "bedrock_key.h"

/// Chunk selection state and the drag that produces it. Shared by every map
/// renderer, so a selection made on one is shown and editable on the others.
class ChunkSelection {
   public:
    enum class Mode { Replace, Add, Subtract };

    [[nodiscard]] bool isEmpty() const { return region_.isEmpty() && !dragging_; }
    [[nodiscard]] bool isDragging() const { return dragging_; }
    [[nodiscard]] const QRegion& region() const { return region_; }
    [[nodiscard]] Mode mode() const { return mode_; }
    void setMode(Mode mode) { mode_ = mode; }
    void clear() { region_ = QRegion(); }
    void setRegion(const QRegion& region) {
        dragging_ = false;
        region_ = region;
    }
    [[nodiscard]] int rectCount() const { return static_cast<int>(region_.rectCount()); }
    [[nodiscard]] int chunkCount() const {
        int count = 0;
        for (const auto& rect : region_) count += rect.width() * rect.height();
        return count;
    }

    void startDrag(const bl::chunk_pos& position) {
        dragging_ = true;
        drag_start_ = position;
        drag_current_ = position;
    }
    void updateDrag(const bl::chunk_pos& position) { drag_current_ = position; }
    QRect finishDrag() {
        dragging_ = false;
        auto [minimum, maximum] = normalizeDragRange(drag_start_, drag_current_);
        const QRect rect(minimum.x, minimum.z, maximum.x - minimum.x + 1, maximum.z - minimum.z + 1);
        applyRect(rect);
        return rect;
    }
    void cancelDrag() { dragging_ = false; }
    /// The rectangle currently being dragged out, in chunk coordinates.
    [[nodiscard]] QRect dragRect() const {
        auto [minimum, maximum] = normalizeDragRange(drag_start_, drag_current_);
        return QRect(minimum.x, minimum.z, maximum.x - minimum.x + 1, maximum.z - minimum.z + 1);
    }

    [[nodiscard]] bool contains(const QPoint& point) const { return !region_.isEmpty() && region_.contains(point); }

   private:
    [[nodiscard]] static std::pair<bl::chunk_pos, bl::chunk_pos> normalizeDragRange(const bl::chunk_pos& a, const bl::chunk_pos& b) {
        bl::chunk_pos minimum;
        bl::chunk_pos maximum;
        minimum.dim = maximum.dim = a.dim;
        minimum.x = std::min(a.x, b.x);
        maximum.x = std::max(a.x, b.x);
        minimum.z = std::min(a.z, b.z);
        maximum.z = std::max(a.z, b.z);
        return {minimum, maximum};
    }

    /// Apply a rectangle in chunk coordinates using the current mode.
    void applyRect(const QRect& chunk_rect) {
        const QRegion rect(chunk_rect);
        switch (mode_) {
            case Mode::Replace:
                region_ = rect;
                break;
            case Mode::Add:
                region_ = region_.united(rect);
                break;
            case Mode::Subtract:
                region_ = region_.subtracted(rect);
                break;
        }
    }

    QRegion region_;
    Mode mode_{Mode::Replace};
    bl::chunk_pos drag_start_;
    bl::chunk_pos drag_current_;
    bool dragging_{false};
};

#endif  // BEDROCKMAP_CHUNKSELECTION_H
