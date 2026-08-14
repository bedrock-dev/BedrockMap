#ifndef MCSTRUCTURE_PAGE_WIDGET_H
#define MCSTRUCTURE_PAGE_WIDGET_H

#include <QString>
#include <QWidget>

#include "nbtwidget.h"
#include "palette.h"
#include "tabpagewidget.h"
#include "voxelwidget.h"

class QHBoxLayout;

// Shows a parsed .mcstructure file: NBT editor (left) + 3D voxel view (right),
// with a (currently empty) button bar at the bottom.
class McstructurePageWidget : public TabPageWidget {
    Q_OBJECT

   public:
    explicit McstructurePageWidget(QWidget *parent = nullptr);
    ~McstructurePageWidget() override;

    bool loadStructure(const QString &path);
    [[nodiscard]] QString getStructureName() const { return structure_name_; }

   private:
    void setupUI();
    void buildVoxelData();

    // data
    bl::mcstructure structure_;
    QString structure_name_;

    // gui
    NbtWidget *nbt_editor_{nullptr};
    VoxelWidget *voxel_widget_{nullptr};
    QHBoxLayout *button_bar_{nullptr};
};

#endif  // MCSTRUCTURE_PAGE_WIDGET_H
