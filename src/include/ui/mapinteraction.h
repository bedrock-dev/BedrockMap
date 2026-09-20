#ifndef BEDROCKMAP_MAPINTERACTION_H
#define BEDROCKMAP_MAPINTERACTION_H

#include <qpoint.h>
#include <qsize.h>
#include <qtmetamacros.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QWheelEvent>

class MapView;

/// Mouse, wheel and keyboard handling for a map view: drag to pan, middle-drag
/// to select, wheel to zoom about the cursor, arrow keys to pan continuously.
///
/// It owns no widget, so every renderer can use the same behaviour - which is
/// the point: the CPU renderer and the GPU one accept input identically, and all
/// of their changes go *through* the shared MapView, so every renderer of that
/// view repaints and none of them has to know about the others.
///
/// Events the controller does not consume (context menu, overlay interaction,
/// anything renderer-specific) are reported as signals for the host widget.
class MapInteraction : public QObject {
    Q_OBJECT

   public:
    explicit MapInteraction(MapView* view, QObject* parent = nullptr);

    [[nodiscard]] MapView* view() const { return view_; }
    void setView(MapView* view);

    /// Each returns true when the controller acted on the event.
    bool mousePress(QMouseEvent* event);
    bool mouseMove(QMouseEvent* event);
    bool mouseRelease(QMouseEvent* event);
    bool wheel(QWheelEvent* event);
    bool keyPress(QKeyEvent* event);
    bool keyRelease(QKeyEvent* event);

    [[nodiscard]] bool isPanning() const { return panning_; }
    [[nodiscard]] bool isPanningWithKeys() const;

    /// Drop all transient input state, e.g. when the widget loses focus or a
    /// modal takes over. A key held while focus is lost never gets its release.
    void reset();

   signals:
    /// Cursor moved over the map without a button held.
    void cursorBlockChanged(int x, int z, int dim);

   private:
    void onPanTick();

    /// The size to resolve event positions against. Hosts pass their own widget
    /// as the parent, and two maps of different widths share one MapView, so the
    /// widget's size - not the view's - is what a click maps through.
    [[nodiscard]] QSize viewportSize() const;

    MapView* view_{nullptr};
    QPointF last_drag_pos_;
    bool panning_{false};
    QTimer* pan_timer_{nullptr};
    QSet<int> pressed_keys_;
};

#endif  // BEDROCKMAP_MAPINTERACTION_H
