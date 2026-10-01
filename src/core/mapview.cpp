#include "mapview.h"

#include <algorithm>
#include <cmath>

#include "config.h"

MapView::MapView(QObject* parent) : QObject(parent) { world_to_view_.scale(DEFAULT_SCALE, DEFAULT_SCALE); }

void MapView::applyConfiguredZoomLimits(bool chunk_index_available) {
    setMinScale(chunk_index_available ? static_cast<qreal>(constant::MINIMUM_ZOOM_SCALE)
                                      : static_cast<qreal>(setting::current().MINIMUM_SCALE_LEVEL));
    setMaxScale(static_cast<qreal>(setting::current().MAXIMUM_SCALE_LEVEL));
}

void MapView::setViewportSize(const QSize& size) {
    // The margin keeps a ring of terrain around the viewport so panning reveals
    // already-rendered pixels instead of a gap.
    constexpr int MARGIN = 10;
    const QRect camera(-MARGIN, -MARGIN, size.width() + MARGIN, size.height() + MARGIN);
    if (camera == camera_) return;  // idempotent: callers may re-assert the size
    camera_ = camera;
    emit viewChanged();
}

QSize MapView::viewportSize() const {
    constexpr int MARGIN = 10;
    return QSize(std::max(0, camera_.width() - MARGIN), std::max(0, camera_.height() - MARGIN));
}

void MapView::setScale(qreal scale, const QPointF& anchorViewPos) {
    if (!std::isfinite(scale) || scale <= 0.0) return;
    scale = std::clamp(scale, min_scale_, max_scale_);

    // Keep the world point under the anchor pinned while the scale changes.
    const QPointF world = world_to_view_.inverted().map(anchorViewPos);
    world_to_view_ = QTransform();
    world_to_view_.translate(anchorViewPos.x(), anchorViewPos.y());
    world_to_view_.scale(scale, scale);
    world_to_view_.translate(-world.x(), -world.y());
    emit viewChanged();
}

void MapView::zoomBy(qreal factor, const QPointF& anchorViewPos) {
    if (factor <= 0.0) return;
    setScale(scale() * factor, anchorViewPos);
}

void MapView::zoomToAdjacentLevel(int direction, const QPointF& anchorViewPos) {
    if (direction == 0) return;

    const qreal current = scale();
    if (!std::isfinite(current) || current <= 0.0) return;

    const qreal octave = std::exp2(std::floor(std::log2(current)));
    constexpr qreal EPSILON = 1.0e-6;
    qreal target = current;

    if (direction > 0) {
        const qreal step = octave / ZOOM_SUBDIVISIONS_PER_OCTAVE;
        target = (std::floor(current / step + EPSILON) + 1.0) * step;
    } else if (std::abs(current - octave) <= octave * EPSILON) {
        // At an exact power of two, the preceding level belongs to the octave
        // below (e.g. 2 -> 1.75 rather than 1.5).
        const qreal previous_octave = octave * 0.5;
        target = previous_octave * (1.0 + static_cast<qreal>(ZOOM_SUBDIVISIONS_PER_OCTAVE - 1) / ZOOM_SUBDIVISIONS_PER_OCTAVE);
    } else {
        const qreal step = octave / ZOOM_SUBDIVISIONS_PER_OCTAVE;
        target = (std::ceil(current / step - EPSILON) - 1.0) * step;
    }

    setScale(target, anchorViewPos);
}

void MapView::translate(const QPointF& delta) {
    world_to_view_.translate(delta.x(), delta.y());
    emit viewChanged();
}

void MapView::setDim(int dim) {
    if (options_.dim == dim) return;
    options_.setDim(dim);
    emit dimChanged(dim);
    emit viewChanged();
}

void MapView::setLayer(RenderOption::LayerType layer) {
    if (options_.layer == layer) return;
    options_.setLayer(layer);
    emit viewChanged();
}

void MapView::setOther(RenderOption::OtherType other, bool value) {
    if (options_.getOther(other) == value) return;
    options_.setOther(other, value);
    emit viewChanged();
}

std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> MapView::renderRange() const { return renderRange(camera_); }

std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> MapView::renderRange(const QRect& viewRect) const {
    const QTransform view_to_world = world_to_view_.inverted();
    const QPointF top_left = view_to_world.map(QPointF(viewRect.x(), viewRect.y()));
    const QPointF bottom_right = view_to_world.map(QPointF(viewRect.x() + viewRect.width(), viewRect.y() + viewRect.height()));
    const bl::chunk_pos min_chunk(static_cast<int>(std::floor(top_left.x())) - 1, static_cast<int>(std::floor(top_left.y())) - 1,
                                  options_.dim);
    const bl::chunk_pos max_chunk(static_cast<int>(std::floor(bottom_right.x())), static_cast<int>(std::floor(bottom_right.y())),
                                  options_.dim);
    return {min_chunk, max_chunk, viewRect};
}

QPointF MapView::chunkPosToViewPos(const bl::chunk_pos& cp) const {
    return world_to_view_.map(QPointF(static_cast<qreal>(cp.x), static_cast<qreal>(cp.z)));
}

QPointF MapView::blockPosToViewPos(const bl::block_pos& bp) const {
    return world_to_view_.map(QPointF(static_cast<qreal>(bp.x) / 16.0, static_cast<qreal>(bp.z) / 16.0));
}

bl::chunk_pos MapView::viewPosToChunkPos(const QPointF& vp) const {
    const QPointF world = world_to_view_.inverted().map(vp);
    return bl::chunk_pos(static_cast<int>(std::floor(world.x())), static_cast<int>(std::floor(world.y())), options_.dim);
}

QPoint MapView::viewPosToBlockPos(const QPointF& vp) const {
    const QPointF world = world_to_view_.inverted().map(vp);
    return QPoint(static_cast<int>(std::floor(world.x() * 16.0)), static_cast<int>(std::floor(world.y() * 16.0)));
}

bl::chunk_pos MapView::chunkPosAt(const QPointF& view_pos, const QSize& viewport) const {
    const QPointF world = transformForViewport(viewport).inverted().map(view_pos);
    return bl::chunk_pos(static_cast<int>(std::floor(world.x())), static_cast<int>(std::floor(world.y())), options_.dim);
}

QPoint MapView::blockPosAt(const QPointF& view_pos, const QSize& viewport) const {
    const QPointF world = transformForViewport(viewport).inverted().map(view_pos);
    return QPoint(static_cast<int>(std::floor(world.x() * 16.0)), static_cast<int>(std::floor(world.y() * 16.0)));
}

QPointF MapView::centerWorldPos() const {
    const QSize size = viewportSize();
    return world_to_view_.inverted().map(QPointF(size.width() / 2.0, size.height() / 2.0));
}

QTransform MapView::transformForViewport(const QSize& size) const {
    // Rebuild worldToView() for a different viewport size.
    const QPointF world_center = centerWorldPos();
    QTransform result;
    result.translate(size.width() / 2.0, size.height() / 2.0);
    result.scale(scale(), scale());
    result.translate(-world_center.x(), -world_center.y());
    return result;
}

void MapView::setOpenedChunk(const bl::chunk_pos& pos) {
    opened_chunk_ = pos;
    emit viewChanged();
}

void MapView::clearOpenedChunk() {
    opened_chunk_.reset();
    emit viewChanged();
}

void MapView::beginSelectionDrag(const bl::chunk_pos& pos) {
    selection_.startDrag(pos);
    emit viewChanged();
}

void MapView::updateSelectionDrag(const bl::chunk_pos& pos) {
    selection_.updateDrag(pos);
    emit viewChanged();
}

void MapView::finishSelectionDrag() {
    selection_.finishDrag();
    emit viewChanged();
    emit selectionChanged();
}

void MapView::clearSelection() {
    selection_.clear();
    emit viewChanged();
    emit selectionChanged();
}

void MapView::setSelectionMode(SelectionController::Mode mode) {
    if (selection_.mode() == mode) return;
    selection_.setMode(mode);
}

void MapView::setSelectionVisible(bool visible) {
    if (selection_visible_ == visible) return;
    selection_visible_ = visible;
    emit viewChanged();
}
