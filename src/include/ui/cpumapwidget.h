#ifndef BEDROCKMAP_CPUMAPWIDGET_H
#define BEDROCKMAP_CPUMAPWIDGET_H

#include <qpoint.h>
#include <qsize.h>
#include <qtmetamacros.h>

#include <QPaintEvent>
#include <QTimer>
#include <QWidget>
#include <functional>

#include "bedrock_key.h"

class AsyncLevelLoader;
class ImportOverlay;
class MapHost;
class MapInteraction;
class MapOverlays;
class MapView;
class QPainter;

/// The CPU renderer for the 2D map: baked terrain or biome tiles painted with
/// QPainter, with the shared overlay layers drawn on top.
///
/// It is one of two interchangeable renderers of a single MapView, the other
/// being GpuMapWidget, so it owns nothing but its own painting. The view, the
/// overlays, the import overlay and the level operations all come from the
/// MapHost it is handed - a peer, not an owner - and both renderers take the
/// same set, which is what keeps them from drifting apart.
class CpuMapWidget : public QWidget {
    Q_OBJECT

   public:
    CpuMapWidget(QWidget* parent, AsyncLevelLoader* loader, MapView* view, MapOverlays* overlays, ImportOverlay* import, MapHost* host);

    ~CpuMapWidget() override;

    [[nodiscard]] QSize sizeHint() const override { return {640, 480}; }

   signals:
    /// Cursor moved over the map: what the status bar shows.
    void mouseMove(int x, int z, int dim);  // NOLINT

   protected:
    void resizeEvent(QResizeEvent* event) override;

    void paintEvent(QPaintEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;

    void mouseMoveEvent(QMouseEvent* event) override;

    void mouseReleaseEvent(QMouseEvent* event) override;

    void wheelEvent(QWheelEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;

    void keyReleaseEvent(QKeyEvent* event) override;

    void focusOutEvent(QFocusEvent* event) override;

   private:
    /// The context menu is the host's, but only this widget knows which chunk
    /// the click landed on: it resolves that with its own transform and hands
    /// the result over.
    void showContextMenu(const QPoint& p);

    [[nodiscard]] bool coordsOverviewMode() const;

    /// Refresh the zoom limits the shared view clamps against; the floor
    /// depends on whether the coordinate index is loaded, which only the loader
    /// knows.
    void syncScaleLimits();

    // base layers, drawn by this widget (the overlays are in overlays_)
    void drawBiome(QPainter* p);

    void drawTerrain(QPainter* p);

    void drawImageInRegion(QPainter* p, const region_pos& pos, const QImage& image) const;

    void foreachRegionInCamera(const std::function<void(const region_pos& p)>& f) const;

    AsyncLevelLoader* loader_{nullptr};
    MapView* view_{nullptr};
    /// The overlay layers, shared with the other renderer of this view.
    MapOverlays* overlays_{nullptr};
    /// The placed-import state, shared with the other renderer of this view.
    ImportOverlay* import_{nullptr};
    /// Owns the map state and the editing actions the context menu needs.
    MapHost* host_{nullptr};

    MapInteraction* interaction_{nullptr};
    /// Repaints while the debug window is on, so its memory counters keep up on
    /// a still view. The GPU renderer has the same timer.
    QTimer* debug_refresh_timer_{nullptr};
};

#endif  // BEDROCKMAP_CPUMAPWIDGET_H
