#include "mapview.h"

#include <algorithm>
#include <cmath>

#include "config.h"

MapView::MapView(QObject* parent) : QObject(parent) { world_to_view_.scale(kDefaultScale, kDefaultScale); }

void MapView::applyConfiguredZoomLimits(bool chunk_index_available) {
    setMinScale(chunk_index_available ? static_cast<qreal>(constant::MINIMUM_ZOOM_SCALE)
                                      : static_cast<qreal>(setting::current().MINIMUM_SCALE_LEVEL));
    setMaxScale(static_cast<qreal>(setting::current().MAXIMUM_SCALE_LEVEL));
}

void MapView::setViewportSize(const QSize& size) {
    // The margin keeps a ring of terrain around the viewport so panning reveals
    // already-rendered pixels instead of a gap.
    constexpr int kMargin = 10;
    const QRect camera(-kMargin, -kMargin, size.width() + kMargin, size.height() + kMargin);
    if (camera == camera_) return;  // idempotent: callers may re-assert the size
    camera_ = camera;
    emit viewChanged();
}

QSize MapView::viewportSize() const {
    constexpr int kMargin = 10;
    return QSize(std::max(0, camera_.width() - kMargin), std::max(0, camera_.height() - kMargin));
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
    // Rebuild the same mapping worldToView() expresses, but anchored on the
    // centre of the requested viewport instead of the one this view was sized
    // for. For the matching size the result is bit-identical, so renderers do
    // not diverge merely by being different widgets.
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
