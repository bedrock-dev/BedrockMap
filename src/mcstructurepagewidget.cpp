#include "mcstructurepagewidget.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QSplitter>
#include <QVBoxLayout>
#include <cmath>
#include <memory>

#include "loguru/loguru.hpp"

namespace {
    bl::block_box fullBounds(const bl::mcstructure &structure) {
        return bl::block_box::from_min_and_size({0, 0, 0}, structure.size_x(), structure.size_y(), structure.size_z());
    }

    bl::block_box blockBoxFromSelection(const VoxelSelection &selection) {
        return {{static_cast<int>(std::floor(selection.minimum.x())), static_cast<int>(std::floor(selection.minimum.y())),
                 static_cast<int>(std::floor(selection.minimum.z()))},
                {static_cast<int>(std::ceil(selection.maximum.x())), static_cast<int>(std::ceil(selection.maximum.y())),
                 static_cast<int>(std::ceil(selection.maximum.z()))}};
    }

    bl::block_box selectedBounds(const bl::mcstructure &structure, const VoxelSelection &selection) {
        const auto bounds = fullBounds(structure);
        if (!selection.isValid()) return bounds;

        const auto selected = blockBoxFromSelection(selection).normalized().intersected(bounds);
        return selected.is_valid() ? selected : bounds;
    }

    std::unique_ptr<bl::nbt::compound_tag> blockEntityAtWorldPosition(const bl::nbt::compound_tag *source,
                                                                      const bl::block_pos &worldPosition) {
        if (!source) return {};
        auto copy = std::unique_ptr<bl::nbt::compound_tag>(static_cast<bl::nbt::compound_tag *>(source->copy()));
        copy->remove("x");
        copy->remove("y");
        copy->remove("z");
        copy->put(new bl::nbt::int_tag("x", worldPosition.x));
        copy->put(new bl::nbt::int_tag("y", worldPosition.y));
        copy->put(new bl::nbt::int_tag("z", worldPosition.z));
        return copy;
    }

    std::shared_ptr<bl::mcstructure> buildExportStructure(const bl::mcstructure &structure, const bl::block_box &bounds) {
        const auto origin = structure.origin();
        const bl::block_pos newOrigin = origin + bounds.min_pos;
        bl::mcstructure_builder builder({bounds.size_x(), bounds.size_y(), bounds.size_z()}, newOrigin);

        for (int layer = 0; layer < static_cast<int>(structure.layer_count()); ++layer) {
            for (int x = bounds.min_pos.x; x < bounds.max_pos.x; ++x) {
                for (int y = bounds.min_pos.y; y < bounds.max_pos.y; ++y) {
                    for (int z = bounds.min_pos.z; z < bounds.max_pos.z; ++z) {
                        const int32_t idx = structure.block_index(layer, x, y, z);
                        if (idx < 0) continue;
                        const auto *entry = structure.palette_entry_at(static_cast<size_t>(idx));
                        if (!entry || !entry->tag) continue;
                        builder.set_block(layer, bl::block_pos{x, y, z} - bounds.min_pos, entry->tag);
                    }
                }
            }
        }

        const auto &blockEntities = structure.block_entities();
        for (size_t i = 0; i < blockEntities.size(); ++i) {
            const auto *entity = blockEntities[i];
            if (!entity) continue;
            const auto pos = structure.block_entity_position(i);
            if (!bounds.contains(pos)) continue;
            const auto newLocalPos = pos - bounds.min_pos;
            const auto newWorldPos = newOrigin + newLocalPos;
            auto adjustedEntity = blockEntityAtWorldPosition(entity, newWorldPos);
            builder.set_block_entity(newLocalPos, adjustedEntity.get());
        }

        return std::make_shared<bl::mcstructure>(builder.build());
    }
}  // namespace
McstructurePageWidget::McstructurePageWidget(QWidget *parent) : TabPageWidget(parent) { setupUI(); }

McstructurePageWidget::~McstructurePageWidget() = default;

void McstructurePageWidget::setupUI() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(2, 2, 2, 2);
    root->setSpacing(2);

    // top: nbt editor | voxel view
    auto *topSplitter = new QSplitter(Qt::Horizontal, this);
    nbt_editor_ = new NbtWidget(topSplitter);
    nbt_editor_->setMode(NbtMode::Memory);  // data comes from the parsed file, not the load button
    nbt_editor_->setReadOnly(true);         // structure files are viewed, not edited
    nbt_editor_->setListVisible(false);     // single document: no left item list
    voxel_preview_widget_ = new VoxelPreviewWidget(topSplitter);
    connect(voxel_preview_widget_, &VoxelPreviewWidget::exportMcstructureRequested, this,
            [this](VoxelSelection selection, bool hasSelection, bool compress) { exportMcstructure(selection, hasSelection, compress); });
    topSplitter->addWidget(nbt_editor_);
    topSplitter->addWidget(voxel_preview_widget_);
    topSplitter->setStretchFactor(0, 0);
    topSplitter->setStretchFactor(1, 1);
    topSplitter->setChildrenCollapsible(false);

    root->addWidget(topSplitter, 1);
}

bool McstructurePageWidget::loadStructure(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_F(WARNING, "Can not open mcstructure file: %s", path.toStdString().c_str());
        return false;
    }
    original_raw_ = file.readAll();
    const auto *data = reinterpret_cast<const byte_t *>(original_raw_.constData());
    const auto len = static_cast<size_t>(original_raw_.size());

    structure_ = std::make_shared<bl::mcstructure>(bl::parse_mcstructure(data, len));
    if (structure_->size_x() <= 0 || structure_->size_y() <= 0 || structure_->size_z() <= 0) {
        LOG_F(WARNING, "Invalid mcstructure file: %s", path.toStdString().c_str());
        return false;
    }

    // left side: NBT editor showing the full structure tree
    int read = 0;
    if (auto *root = bl::nbt::read_one_palette(data, len, read)) {
        nbt_editor_->loadNewData({NBTListItem::from(root, QFileInfo(path).fileName())});
        // The item list is hidden in this view, so open the first item directly
        // to populate the NBT tree.
        nbt_editor_->openItem(0);
    }

    structure_name_ = QFileInfo(path).baseName();
    voxel_preview_widget_->loadMcstructureAsync(structure_);
    return true;
}

void McstructurePageWidget::exportMcstructure(const VoxelSelection &selection, bool hasSelection, bool /*compress*/) {
    if (!structure_) return;

    const auto bounds = hasSelection ? selectedBounds(*structure_, selection) : fullBounds(*structure_);
    if (!bounds.is_valid()) return;

    QString filePath = QFileDialog::getSaveFileName(this, tr("Export mcstructure"), structure_name_ + ".mcstructure",
                                                    tr("MCStructure files (*.mcstructure)"));
    if (filePath.isEmpty()) return;
    if (QFileInfo(filePath).suffix().isEmpty()) {
        filePath += QStringLiteral(".mcstructure");
    }

    bool ok = false;
    if (hasSelection) {
        ok = buildExportStructure(*structure_, bounds)->save_to_file(filePath.toStdString());
    } else {
        QFile output(filePath);
        ok = output.open(QIODevice::WriteOnly) && output.write(original_raw_) == original_raw_.size();
    }
    if (!ok) {
        LOG_F(WARNING, "Can not save mcstructure file: %s", filePath.toStdString().c_str());
    }
}
