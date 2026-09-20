#ifndef BEDROCKMAP_MAPVIEW_H
#define BEDROCKMAP_MAPVIEW_H

#include <qpoint.h>
#include <qrect.h>
#include <qsize.h>
#include <qtmetamacros.h>
#include <qtransform.h>
#include <qtypes.h>

#include <QObject>
#include <optional>
#include <tuple>

#include "bedrock_key.h"
#include "render_options.h"
#include "selectioncontroller.h"

/// Everything about *what* the map shows, with no widget involved: the
/// world-to-pixel transform, the viewport, the dimension and the selection.
///
/// The map is drawn by more than one renderer (the CPU painter and the GPU
/// widget), and they have to agree exactly - same centre, same zoom, same
/// selection. Rather than keeping two copies in sync, they share one instance:
/// a change emits viewChanged() and every attached renderer repaints from the
/// same numbers. Zoom clamping lives here too, so no renderer can clamp
/// differently from the others.
class MapView : public QObject {
    Q_OBJECT

   public:
    static constexpr qreal kDefaultScale = 64.0;  // pixels per chunk

    explicit MapView(QObject* parent = nullptr);

    // --- viewport ---

    /// Widget size in logical pixels. The camera keeps a small margin around the
    /// viewport so rendering has data for the pixels just off screen.
    void setViewportSize(const QSize& size);

    [[nodiscard]] QSize viewportSize() const;

    /// Drawable range, i.e. the viewport plus its margin.
    [[nodiscard]] const QRect& camera() const { return camera_; }

    // --- transform ---

    [[nodiscard]] const QTransform& worldToView() const { return world_to_view_; }

    /// Pixels per chunk.
    [[nodiscard]] qreal scale() const { return world_to_view_.m11(); }

    /// Pixels per chunk, the unit the drawing code and CpuMapWidget's public API use.
    [[nodiscard]] qreal scaleLevel() const { return scale(); }

    /// Zoom to an absolute scale about a point in view coordinates, clamped to
    /// the limits below.
    void setScale(qreal scale, const QPointF& anchorViewPos);

    /// Multiply the current scale by a factor about a point in view coordinates.
    void zoomBy(qreal factor, const QPointF& anchorViewPos);

    void translate(const QPointF& delta);

    void setMinScale(qreal scale) { min_scale_ = scale; }
    void setMaxScale(qreal scale) { max_scale_ = scale; }
    [[nodiscard]] qreal minScale() const { return min_scale_; }
    [[nodiscard]] qreal maxScale() const { return max_scale_; }

    /// The configured zoom limits. The floor is the whole-world overview scale
    /// while the chunk coordinate index is available (everything is mapped, so
    /// zooming out further still shows something), otherwise the configured
    /// minimum. Renderers call this so they all clamp identically.
    void applyConfiguredZoomLimits(bool chunk_index_available);

    /// Transform mapping chunk coordinates to the pixels of a viewport of the
    /// given size that shows this view. For the viewport the view was sized for
    /// this is identical to worldToView(); a renderer drawing into a differently
    /// sized widget needs its own, and this is how it gets one without having to
    /// redo the centre/scale maths.
    [[nodiscard]] QTransform transformForViewport(const QSize& size) const;

    // --- overlay state shared by every renderer ---

    /// Chunk highlighted as "open in the chunk editor".
    void setOpenedChunk(const bl::chunk_pos& pos);
    void clearOpenedChunk();
    [[nodiscard]] const std::optional<bl::chunk_pos>& openedChunk() const { return opened_chunk_; }

    // --- content ---

    /// Dimension, layer and overlay toggles. Renderers read this so they cannot
    /// disagree about what is being shown.
    [[nodiscard]] RenderOption& options() { return options_; }
    [[nodiscard]] const RenderOption& options() const { return options_; }

    void setDim(int dim);

    /// Layer and overlay toggles. These go through the view rather than the
    /// RenderOption getter so that every renderer repaints: the chrome of one
    /// renderer can then change what the other one shows.
    void setLayer(RenderOption::LayerType layer);
    void setOther(RenderOption::OtherType other, bool value);

    [[nodiscard]] int dim() const { return options_.dim; }

    // --- selection ---
    //
    // The selection is shared, but the drag that changes it is driven by
    // whichever renderer received the mouse event, so the mutations go through
    // the view: that way every renderer repaints, not just the one clicked on.

    [[nodiscard]] SelectionController& selection() { return selection_; }
    [[nodiscard]] const SelectionController& selection() const { return selection_; }

    void beginSelectionDrag(const bl::chunk_pos& pos);
    void updateSelectionDrag(const bl::chunk_pos& pos);
    void finishSelectionDrag();
    void clearSelection();
    void setSelectionMode(SelectionController::Mode mode);

    /// Keep the selection rectangle out of a screenshot. Shared state, because
    /// the capture can be taken from either renderer.
    void setSelectionVisible(bool visible);
    [[nodiscard]] bool selectionVisible() const { return selection_visible_; }

    /// Something a renderer draws from changed but the view cannot describe -
    /// an overlay toggle, a re-baked cache - so renderers repaint. The state
    /// itself lives with its owner; this is only the notification.
    void notifyChanged() { emit viewChanged(); }

    // --- coordinate queries ---

    [[nodiscard]] std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> renderRange() const;

    /// Same as renderRange() but for an explicit rectangle in view coordinates.
    [[nodiscard]] std::tuple<bl::chunk_pos, bl::chunk_pos, QRect> renderRange(const QRect& viewRect) const;

    [[nodiscard]] QPointF chunkPosToViewPos(const bl::chunk_pos& cp) const;

    [[nodiscard]] QPointF blockPosToViewPos(const bl::block_pos& bp) const;

    [[nodiscard]] bl::chunk_pos viewPosToChunkPos(const QPointF& vp) const;

    [[nodiscard]] QPoint viewPosToBlockPos(const QPointF& vp) const;

    /// Same queries for an explicit viewport, so a renderer that is not the
    /// widget the view was sized for resolves clicks against its own pixels.
    [[nodiscard]] bl::chunk_pos chunkPosAt(const QPointF& view_pos, const QSize& viewport) const;
    [[nodiscard]] QPoint blockPosAt(const QPointF& view_pos, const QSize& viewport) const;

    /// World position (chunk units) shown at the centre of the viewport.
    [[nodiscard]] QPointF centerWorldPos() const;

   signals:
    /// The transform, viewport or anything else a renderer draws from changed;
    /// every renderer repaints.
    void viewChanged();

    void dimChanged(int dim);

    /// The selection changed in a way the chrome reports (status bar, toolbars).
    void selectionChanged();

   private:
    QTransform world_to_view_;
    QRect camera_;
    RenderOption options_;
    SelectionController selection_;
    std::optional<bl::chunk_pos> opened_chunk_;
    bool selection_visible_{true};
    qreal min_scale_{1.0};
    qreal max_scale_{1024.0};
};

#endif  // BEDROCKMAP_MAPVIEW_H
