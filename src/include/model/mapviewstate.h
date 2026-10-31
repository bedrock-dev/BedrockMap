#ifndef BEDROCKMAP_MAPVIEWSTATE_H
#define BEDROCKMAP_MAPVIEWSTATE_H

#include <qpoint.h>
#include <qrect.h>
#include <qsize.h>
#include <qtmetamacros.h>
#include <qtransform.h>
#include <qtypes.h>

#include <QObject>
#include <algorithm>
#include <array>
#include <optional>
#include <tuple>

#include "bedrock_key.h"
#include "model/chunkselection.h"

/// View state (dimension, layer and overlays) for the 2D map.
struct MapRenderOptions {
    enum LayerType { Terrain = 0, Biome = 1 };
    enum OtherType { Grid = 0, Coords = 1, SlimeChunk = 2, Actors = 3, Village = 4, HSA = 5, Portal = 6, OtherLen = 7 };

    static constexpr int OverWorld = 0;
    static constexpr int Nether = 1;
    static constexpr int TheEnd = 2;

    MapRenderOptions() { reset(); }

    int dim{OverWorld};
    LayerType layer{Terrain};
    std::array<bool, OtherType::OtherLen> others{};

    void reset() {
        dim = OverWorld;
        layer = Terrain;
        std::fill(others.begin(), others.end(), false);
        setOther(Grid, true);
    }

    void setDim(int new_dimension) { dim = new_dimension; }
    void setLayer(LayerType new_layer) { layer = new_layer; }

    bool toggleOther(OtherType type) {
        if (type < 0 || type >= OtherType::OtherLen) return true;
        others[type] = !others[type];
        return others[type];
    }

    void setOther(OtherType type, bool value) {
        if (type < 0 || type >= OtherType::OtherLen) return;
        others[type] = value;
    }

    bool getOther(OtherType type) const {
        if (type < 0 || type >= OtherType::OtherLen) return false;
        return others[type];
    }
};

struct MapCameraState {
    QPointF center;
    qreal pixelsPerChunk{64.0};
    int dimension{0};
    int layer{0};

    [[nodiscard]] bool operator==(const MapCameraState& other) const {
        return center == other.center && pixelsPerChunk == other.pixelsPerChunk && dimension == other.dimension && layer == other.layer;
    }
    [[nodiscard]] bool operator!=(const MapCameraState& other) const { return !(*this == other); }
};

/// Shared map transform, viewport, dimension, layers, and selection state.
class MapViewState : public QObject {
    Q_OBJECT

   public:
    static constexpr qreal DEFAULT_SCALE = 64.0;  // pixels per chunk

    explicit MapViewState(QObject* parent = nullptr);

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

    /// Pixels per chunk in the current map transform.
    [[nodiscard]] qreal scaleLevel() const { return scale(); }

    /// Zoom to an absolute scale about a point in view coordinates, clamped to
    /// the limits below.
    void setScale(qreal scale, const QPointF& anchorViewPos);

    /// Multiply the current scale by a factor about a point in view coordinates.
    void zoomBy(qreal factor, const QPointF& anchorViewPos);

    /// Move to the adjacent discrete zoom level.
    void zoomToAdjacentLevel(int direction, const QPointF& anchorViewPos);

    void translate(const QPointF& delta, bool notify = true);

    void setMinScale(qreal scale) { min_scale_ = scale; }
    void setMaxScale(qreal scale) { max_scale_ = scale; }
    [[nodiscard]] qreal minScale() const { return min_scale_; }
    [[nodiscard]] qreal maxScale() const { return max_scale_; }

    /// Apply configured zoom limits, including the overview floor.
    void applyConfiguredZoomLimits(bool chunk_index_available);
    void applyTerrainZoomLimits(bool overview_available);

    /// Return the world transform for an explicit viewport size.
    [[nodiscard]] QTransform transformForViewport(const QSize& size) const;

    // --- overlay state shared by every renderer ---

    /// Chunk highlighted as "open in the chunk editor".
    void setOpenedChunk(const bl::chunk_pos& pos);
    void clearOpenedChunk();
    [[nodiscard]] const std::optional<bl::chunk_pos>& openedChunk() const { return opened_chunk_; }

    // --- content ---

    /// Shared dimension, layer, and overlay options.
    [[nodiscard]] MapRenderOptions& options() { return options_; }
    [[nodiscard]] const MapRenderOptions& options() const { return options_; }

    void setDim(int dim);

    /// Change a layer or overlay and notify all renderers.
    void setLayer(MapRenderOptions::LayerType layer);
    void setOther(MapRenderOptions::OtherType other, bool value);

    [[nodiscard]] int dim() const { return options_.dim; }

    // --- selection ---
    //
    // Selection mutations are shared by all renderers.

    [[nodiscard]] ChunkSelection& selection() { return selection_; }
    [[nodiscard]] const ChunkSelection& selection() const { return selection_; }

    void beginSelectionDrag(const bl::chunk_pos& pos);
    void updateSelectionDrag(const bl::chunk_pos& pos);
    void finishSelectionDrag();
    void cancelSelectionDrag();
    void clearSelection();
    void setSelectionRegion(const QRegion& region);
    void setSelectionMode(ChunkSelection::Mode mode);

    /// Include or exclude the selection rectangle from captures.
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

    /// Same queries for an explicit viewport when the caller's size differs
    /// from the view's current size.
    [[nodiscard]] bl::chunk_pos chunkPosAt(const QPointF& view_pos, const QSize& viewport) const;
    [[nodiscard]] QPoint blockPosAt(const QPointF& view_pos, const QSize& viewport) const;

    /// World position (chunk units) shown at the centre of the viewport.
    [[nodiscard]] QPointF centerWorldPos() const;
    [[nodiscard]] MapCameraState cameraState() const;

   signals:
    /// The transform, viewport or anything else a renderer draws from changed;
    /// every renderer repaints.
    void viewChanged();

    void dimChanged(int dim);

    /// The selection changed in a way the chrome reports (status bar, toolbars).
    void selectionChanged();

   private:
    static constexpr int ZOOM_SUBDIVISIONS_PER_OCTAVE = 4;

    QTransform world_to_view_;
    QRect camera_;
    QSize viewport_size_;
    MapRenderOptions options_;
    ChunkSelection selection_;
    std::optional<bl::chunk_pos> opened_chunk_;
    bool selection_visible_{true};
    qreal min_scale_{1.0};
    qreal max_scale_{1024.0};
};

#endif  // BEDROCKMAP_MAPVIEWSTATE_H
