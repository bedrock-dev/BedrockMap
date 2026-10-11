#ifndef BEDROCKMAP_CONTEXTMENUBUILDER_H
#define BEDROCKMAP_CONTEXTMENUBUILDER_H

#include <qpoint.h>
#include <qwidget.h>

#include <QMenu>

#include "bedrock_key.h"

class MapHost;

/// Where a context menu was opened, resolved by the renderer that was clicked.
///
/// The pixel-to-world mapping differs per widget (they have different sizes), so
/// the widget resolves it and passes the result; the menu itself never has to
/// know which renderer it is acting on.
struct MapMenuRequest {
    QPoint global_pos;    ///< where to pop the menu up
    bl::chunk_pos chunk;  ///< chunk under the cursor
    bl::block_pos block;  ///< block under the cursor (y = 0)
    int dim{0};
    /// The pane the menu was opened on. A screenshot captures this widget, so a
    /// capture from the GPU renderer is of the GPU pixels.
    QWidget* source{nullptr};
};

/// Builds and shows the map's right-click menu.
///
/// It acts on the MapHost, which is where the map's editing operations live, so
/// every renderer offers the same menu and no action has to exist twice.
class ContextMenuBuilder {
   public:
    ContextMenuBuilder() = delete;

    static void show(MapHost* host, const MapMenuRequest& request);

    /// Fills `menu` for `request`. Split out of show() so the contents can be
    /// inspected without running the menu (the bench checks that both renderers
    /// get the same entries for the same click).
    static void build(QMenu& menu, MapHost* host, const MapMenuRequest& request);

    /// The widget the menu acts on: the pane it was opened on, falling back to
    /// the host's own pane for callers that do not name one.
    [[nodiscard]] static QWidget* mapPane(MapHost* host, const MapMenuRequest& request);
};

#endif  // BEDROCKMAP_CONTEXTMENUBUILDER_H
