#ifndef BEDROCKMAP_MAPOVERLAYS_H
#define BEDROCKMAP_MAPOVERLAYS_H

#include <qcolor.h>
#include <qfont.h>
#include <qrect.h>
#include <qtransform.h>
#include <qtypes.h>

#include <QMap>
#include <QString>
#include <functional>

#include "bedrock_key.h"
#include "render_options.h"

class AsyncLevelLoader;
class MapView;
class QPainter;

/// Bounding rect of one village, used as map overlay draw data.
struct VillageDrawInfo {
    bl::block_pos p1;
    bl::block_pos p2;
    int dim{0};
};

/// Draws the map's overlays - chunk grid, block coordinates, slime chunks,
/// actors, villages, hardcoded spawn areas, the selection, the chunk-editor
/// highlight, the coordinate minimap and the debug panel.
///
/// These are all QPainter work against a shared MapView, so they belong to no
/// particular renderer: the CPU painter draws the terrain and then calls these,
/// and the GPU widget does the same after its GL pass. Sharing them is what keeps
/// the two maps looking identical; duplicating them would mean every overlay
/// tweak has to be made twice and would drift.
///
/// Each draw method uses the transform set by setTransform(), which is *not* the
/// painter's own transform: the world-space layers expect the caller to have set
/// that on the painter (so cosmetic pens and line widths work in screen units),
/// while the layers that place things by hand (coordinates, actors) map through
/// this transform themselves.
class MapOverlays {
   public:
    static QFont CHUNK_TEXT_FONT;

    MapOverlays(MapView* view, AsyncLevelLoader* loader);

    void setView(MapView* view) { view_ = view; }

    /// The transform from chunk coordinates to the target widget's pixels.
    /// Renderers pass view()->transformForViewport(widget->size()).
    void setTransform(const QTransform& world_to_view) { world_to_view_ = world_to_view; }

    [[nodiscard]] const QTransform& transform() const { return world_to_view_; }

    // --- state pushed in by the owning page ---

    void setVillages(const QMap<QString, VillageDrawInfo>& villages) { villages_ = villages; }
    [[nodiscard]] const QMap<QString, VillageDrawInfo>& villages() const { return villages_; }

    void setDrawDebug(bool enable) { draw_debug_ = enable; }
    [[nodiscard]] bool drawDebug() const { return draw_debug_; }

    void setCoordsMiniMap(bool enable) { coords_minimap_ = enable; }
    [[nodiscard]] bool coordsMiniMap() const { return coords_minimap_; }

    /// Space at the bottom of the widget that screen-space overlays must stay out
    /// of. The GPU renderer draws its own stats bar there.
    void setScreenInset(int bottom) { screen_inset_ = std::max(0, bottom); }
    [[nodiscard]] int screenInset() const { return screen_inset_; }

    // --- iteration ---

    /// Every chunk touched by the camera (the viewport plus its margin).
    void forEachChunkInCamera(const std::function<void(const bl::chunk_pos&)>& f) const;

    /// Every 8x8-chunk render region touched by the camera.
    void foreachRegionInCamera(const std::function<void(const bl::chunk_pos&)>& f) const;

    // --- world-space layers ---
    // The painter must already carry the chunk->pixel transform.

    /// The zoomed-out base layer: the archive's chunk presence map, one tile per
    /// COORDS_REGION_SIZE chunks, clipped to the world. Both renderers draw this
    /// instead of the terrain layers while coordsOverviewMode() holds.
    void drawCoordsOverview(QPainter* painter) const;

    void drawCoordsBoundingBox(QPainter* painter) const;
    void drawHSAs(QPainter* painter) const;
    void drawVillages(QPainter* painter) const;
    void drawSlimeChunks(QPainter* painter) const;
    void drawGrid(QPainter* painter) const;
    void drawSelection(QPainter* painter) const;
    void drawOpenedChunk(QPainter* painter) const;

    // --- widget-space layers ---
    // Drawn with an identity transform; they place themselves.

    void drawActors(QPainter* painter) const;
    void drawChunkPosText(QPainter* painter) const;
    void drawCoordsMiniMap(QPainter* painter) const;
    void drawDebugWindow(QPainter* painter) const;

    /// True when the view is zoomed out far enough for the coordinate overview to
    /// replace the terrain layers. The detail layers below check this themselves.
    [[nodiscard]] bool coordsOverviewMode() const;

   private:
    [[nodiscard]] QPointF chunkPosToViewPos(const bl::chunk_pos& cp) const;
    [[nodiscard]] QPointF blockPosToViewPos(const bl::block_pos& bp) const;
    [[nodiscard]] QSize targetSize(const QPainter* painter) const;

    MapView* view_{nullptr};
    AsyncLevelLoader* level_loader_{nullptr};
    QTransform world_to_view_;
    QMap<QString, VillageDrawInfo> villages_;
    bool draw_debug_{false};
    bool coords_minimap_{false};
    int screen_inset_{0};
};

#endif  // BEDROCKMAP_MAPOVERLAYS_H
