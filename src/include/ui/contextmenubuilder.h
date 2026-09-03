#ifndef BEDROCKMAP_CONTEXTMENUBUILDER_H
#define BEDROCKMAP_CONTEXTMENUBUILDER_H

#include <qtmetamacros.h>
#include <qwidget.h>

#include <QMenu>

class MapWidget;

/// Friend of MapWidget — builds the right-click context menu with all actions.
class ContextMenuBuilder : public QWidget {
    Q_OBJECT;

   public:
    ContextMenuBuilder() = delete;
    /// Build and exec the context menu at global position p.
    static void show(QWidget *parent, MapWidget *mapWidget, const QPoint &p);
};

#endif  // BEDROCKMAP_CONTEXTMENUBUILDER_H
