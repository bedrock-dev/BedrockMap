#include "mcstructurepagewidget.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <memory>
#include <optional>

#include "loguru/loguru.hpp"

namespace {
    constexpr qsizetype kNbtTreeDisplayLimit = 4 * 1024 * 1024;

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

    std::optional<bl::vec3> entityWorldPosition(const bl::nbt::compound_tag *entity) {
        if (!entity) return std::nullopt;
        const auto *tag = entity->get("Pos");
        const auto *list = tag ? tag->as<const bl::nbt::list_tag *>() : nullptr;
        if (!list || list->value.size() < 3) return std::nullopt;
        const auto read = [](const bl::nbt::abstract_tag *value, float &result) {
            if (const auto *v = value ? value->as<const bl::nbt::float_tag *>() : nullptr) {
                result = v->value;
                return true;
            }
            if (const auto *v = value ? value->as<const bl::nbt::double_tag *>() : nullptr) {
                result = static_cast<float>(v->value);
                return true;
            }
            return false;
        };
        bl::vec3 position{0.0f, 0.0f, 0.0f};
        return read(list->value[0], position.x) && read(list->value[1], position.y) && read(list->value[2], position.z)
                   ? std::optional<bl::vec3>(position)
                   : std::nullopt;
    }

    std::shared_ptr<bl::mcstructure> buildExportStructure(const bl::mcstructure &structure, const bl::block_box &bounds, int32_t version,
                                                          bool exportEntities) {
        const auto origin = structure.origin();
        const bl::block_pos newOrigin = origin + bounds.min_pos;
        bl::mcstructure_builder builder({bounds.size_x(), bounds.size_y(), bounds.size_z()}, newOrigin, version);

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

        if (exportEntities) {
            for (const auto *entity : structure.entities()) {
                const auto worldPosition = entityWorldPosition(entity);
                if (!worldPosition) continue;
                const auto localPosition = bl::block_pos{static_cast<int>(std::floor(worldPosition->x - origin.x)),
                                                         static_cast<int>(std::floor(worldPosition->y - origin.y)),
                                                         static_cast<int>(std::floor(worldPosition->z - origin.z))};
                if (!bounds.contains(localPosition)) continue;
                builder.add_entity(entity);
            }
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

    // top: vertically stacked info/NBT panel | voxel view
    auto *topSplitter = new QSplitter(Qt::Horizontal, this);
    auto *leftPanel = new QWidget(topSplitter);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);

    structure_info_widget_ = new QWidget(leftPanel);
    auto *infoLayout = new QVBoxLayout(structure_info_widget_);
    infoLayout->setContentsMargins(12, 12, 12, 12);
    structure_info_label_ = new QLabel(structure_info_widget_);
    structure_info_label_->setWordWrap(true);
    structure_info_label_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    QFont infoFont;
    infoFont.setFamily(QStringLiteral("JetBrains Mono"));
    structure_info_label_->setFont(infoFont);
    infoLayout->addWidget(structure_info_label_);
    infoLayout->addStretch();

    nbt_editor_ = new NbtWidget(leftPanel);
    nbt_editor_->setMode(NbtMode::Memory);  // data comes from the parsed file, not the load button
    nbt_editor_->setReadOnly(true);         // structure files are viewed, not edited
    nbt_editor_->setListVisible(false);     // single document: no left item list

    leftLayout->addWidget(structure_info_widget_, 1);
    leftLayout->addWidget(nbt_editor_, 3);

    voxel_preview_widget_ = new VoxelPreviewWidget(topSplitter);
    connect(voxel_preview_widget_, &VoxelPreviewWidget::exportMcstructureRequested, this,
            [this](VoxelSelection selection, bool hasSelection, bool compress, bool exportEntities, bool useNewFormat) {
                exportMcstructure(selection, hasSelection, compress, exportEntities, useNewFormat);
            });
    topSplitter->addWidget(leftPanel);
    topSplitter->addWidget(voxel_preview_widget_);
    topSplitter->setStretchFactor(0, 1);
    topSplitter->setStretchFactor(1, 1);
    topSplitter->setChildrenCollapsible(false);

    root->addWidget(topSplitter, 1);
    QTimer::singleShot(0, topSplitter, [topSplitter]() {
        const int totalWidth = topSplitter->width();
        if (totalWidth > 1) topSplitter->setSizes({totalWidth / 2, totalWidth - totalWidth / 2});
    });
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

    const auto origin = structure_->origin();
    structure_info_label_->setText(QStringLiteral("File size: %1 MiB\n"
                                                  "Dimensions: %2 x %3 x %4\n"
                                                  "Origin: (%5, %6, %7)\n"
                                                  "Palette entries: %8\n"
                                                  "Block entities: %9\n"
                                                  "Entities: %10")
                                       .arg(static_cast<double>(original_raw_.size()) / (1024.0 * 1024.0), 0, 'f', 2)
                                       .arg(structure_->size_x())
                                       .arg(structure_->size_y())
                                       .arg(structure_->size_z())
                                       .arg(origin.x)
                                       .arg(origin.y)
                                       .arg(origin.z)
                                       .arg(static_cast<qulonglong>(structure_->palette_size()))
                                       .arg(static_cast<qulonglong>(structure_->block_entities().size()))
                                       .arg(static_cast<qulonglong>(structure_->entities().size())));

    const bool showNbtTree = original_raw_.size() <= kNbtTreeDisplayLimit;
    nbt_editor_->setVisible(showNbtTree);
    if (showNbtTree) {
        // Small files can be expanded in the regular NBT tree without blocking the UI.
        int read = 0;
        if (auto *root = bl::nbt::read_one_palette(data, len, read)) {
            nbt_editor_->loadNewData({NBTListItem::from(root, QFileInfo(path).fileName())});
            // The item list is hidden in this view, so open the first item directly
            // to populate the NBT tree.
            nbt_editor_->openItem(0);
        }
    } else {
        structure_info_label_->setText(structure_info_label_->text() +
                                       QStringLiteral("\n\nNBT tree hidden for files larger than 4 MiB to keep the interface responsive."));
        LOG_F(INFO, "Skipping NBT tree for large mcstructure (%lld bytes)", static_cast<long long>(original_raw_.size()));
    }

    structure_name_ = QFileInfo(path).baseName();
    voxel_preview_widget_->loadMcstructureAsync(structure_);
    return true;
}

void McstructurePageWidget::exportMcstructure(const VoxelSelection &selection, bool hasSelection, bool /*compress*/, bool exportEntities,
                                              bool useNewFormat) {
    if (!structure_) return;

    const auto bounds = hasSelection ? selectedBounds(*structure_, selection) : fullBounds(*structure_);
    if (!bounds.is_valid()) return;

    QString filePath = QFileDialog::getSaveFileName(this, tr("Export mcstructure"), structure_name_ + ".mcstructure",
                                                    tr("MCStructure files (*.mcstructure)"));
    if (filePath.isEmpty()) return;
    if (QFileInfo(filePath).suffix().isEmpty()) {
        filePath += QStringLiteral(".mcstructure");
    }

    const int32_t version = useNewFormat ? 2 : 1;
    const bool ok = buildExportStructure(*structure_, bounds, version, exportEntities)->save_to_file(filePath.toStdString());
    if (!ok) {
        LOG_F(WARNING, "Can not save mcstructure file: %s", filePath.toStdString().c_str());
    }
}
