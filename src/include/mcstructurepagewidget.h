#ifndef MCSTRUCTURE_PAGE_WIDGET_H
#define MCSTRUCTURE_PAGE_WIDGET_H

#include <QByteArray>
#include <QString>
#include <QWidget>
#include <memory>

#include "nbtwidget.h"
#include "palette.h"
#include "tabpagewidget.h"
#include "voxelwidget.h"

class QLabel;

// Shows a parsed .mcstructure file: NBT/details panel (left) + voxel preview / export UI (right).
class McstructurePageWidget : public TabPageWidget {
    Q_OBJECT

   public:
    explicit McstructurePageWidget(QWidget *parent = nullptr);
    ~McstructurePageWidget() override;

    bool loadStructure(const QString &path);
    [[nodiscard]] QString getStructureName() const { return structure_name_; }

   private:
    void setupUI();
    void exportMcstructure(const VoxelSelection &selection, bool hasSelection, bool compress, bool exportEntities, bool useNewFormat);

    // data
    std::shared_ptr<bl::mcstructure> structure_;
    QByteArray original_raw_;
    QString structure_name_;

    // gui
    NbtWidget *nbt_editor_{nullptr};
    QWidget *structure_info_widget_{nullptr};
    QLabel *structure_info_label_{nullptr};
    VoxelPreviewWidget *voxel_preview_widget_{nullptr};
};

#endif  // MCSTRUCTURE_PAGE_WIDGET_H
