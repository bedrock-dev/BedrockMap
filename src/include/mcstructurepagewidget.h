#ifndef MCSTRUCTURE_PAGE_WIDGET_H
#define MCSTRUCTURE_PAGE_WIDGET_H

#include <QString>
#include <memory>
#include <QWidget>

#include "nbtwidget.h"
#include "palette.h"
#include "tabpagewidget.h"
#include "voxelwidget.h"

// Shows a parsed .mcstructure file: NBT editor (left) + voxel preview / export UI (right).
class McstructurePageWidget : public TabPageWidget {
    Q_OBJECT

   public:
    explicit McstructurePageWidget(QWidget *parent = nullptr);
    ~McstructurePageWidget() override;

    bool loadStructure(const QString &path);
    [[nodiscard]] QString getStructureName() const { return structure_name_; }

   private:
    void setupUI();

    // data
    std::shared_ptr<bl::mcstructure> structure_;
    QString structure_name_;

    // gui
    NbtWidget *nbt_editor_{nullptr};
    VoxelPreviewWidget *voxel_preview_widget_{nullptr};
};

#endif  // MCSTRUCTURE_PAGE_WIDGET_H
