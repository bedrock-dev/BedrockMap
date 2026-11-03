#include "controller/mcstructurevolumesource.h"

#include <QCoreApplication>
#include <QUrl>
#include <cmath>
#include <exception>
#include <optional>
#include <utility>
#include <vector>

#include "chunk_data_position.h"
#include "io/documentio.h"
#include "mcstructure.h"
#include "nbt.h"
#include "render/voxel_mesh.h"

namespace {
    struct McstructureData {
        std::shared_ptr<bl::mcstructure> structure;
        std::unique_ptr<bl::nbt::compound_tag> root;
        QString error;

        [[nodiscard]] bool valid() const { return structure && error.isEmpty(); }
    };

    std::unique_ptr<bl::nbt::compound_tag> blockEntityAtWorldPosition(const bl::nbt::compound_tag* source,
                                                                      const bl::block_pos& worldPosition) {
        if (!source) return {};
        auto copy = std::unique_ptr<bl::nbt::compound_tag>(static_cast<bl::nbt::compound_tag*>(source->copy()));
        bl::set_block_entity_pos(copy.get(), worldPosition);
        return copy;
    }

    std::optional<bl::vec3> entityWorldPosition(const bl::nbt::compound_tag* entity) {
        if (!entity) return std::nullopt;
        const auto* tag = entity->get("Pos");
        const auto* list = tag ? tag->as<const bl::nbt::list_tag*>() : nullptr;
        if (!list || list->value.size() < 3) return std::nullopt;
        const auto read = [](const bl::nbt::abstract_tag* value, float& result) {
            if (const auto* v = value ? value->as<const bl::nbt::float_tag*>() : nullptr) {
                result = v->value;
                return true;
            }
            if (const auto* v = value ? value->as<const bl::nbt::double_tag*>() : nullptr) {
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

    std::shared_ptr<bl::mcstructure> buildExportStructure(const bl::mcstructure& structure, const bl::block_box& bounds, int32_t version,
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
                        const auto* entry = structure.palette_entry_at(static_cast<size_t>(idx));
                        if (!entry || !entry->tag) continue;
                        builder.set_block(layer, bl::block_pos{x, y, z} - bounds.min_pos, entry->tag);
                    }
                }
            }
        }

        const auto& blockEntities = structure.block_entities();
        for (size_t i = 0; i < blockEntities.size(); ++i) {
            const auto* entity = blockEntities[i];
            if (!entity) continue;
            const auto pos = structure.block_entity_local_position(i);
            if (!bounds.contains(pos)) continue;
            const auto newLocalPos = pos - bounds.min_pos;
            const auto newWorldPos = newOrigin + newLocalPos;
            auto adjustedEntity = blockEntityAtWorldPosition(entity, newWorldPos);
            builder.set_block_entity(newLocalPos, adjustedEntity.get());
        }

        if (exportEntities) {
            for (const auto* entity : structure.entities()) {
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

    McstructureData parseMcstructure(QByteArray raw) {
        McstructureData result;
        try {
            if (!raw.isEmpty()) {
                const auto* bytes = reinterpret_cast<const byte_t*>(raw.constData());
                const auto size = static_cast<size_t>(raw.size());
                result.structure = std::make_shared<bl::mcstructure>(bl::parse_mcstructure(bytes, size));
                if (result.structure->size_x() > 0 && result.structure->size_y() > 0 && result.structure->size_z() > 0) {
                    auto parsed = bl::nbt::parse_one(std::span<const byte_t>(bytes, size));
                    result.root.reset(parsed.root);
                    if (!parsed || !result.root) {
                        result.error = QCoreApplication::translate("BlockVolumeController",
                                                                     "Could not read the structure NBT: %1")
                                           .arg(bl::nbt::parse_error_to_string(parsed.error_code));
                        return result;
                    }
                    return result;
                }
            }
            result.error = QCoreApplication::translate("BlockVolumeController", "Invalid or unsupported mcstructure file.");
        } catch (const std::exception& error) {
            result.error = QCoreApplication::translate("BlockVolumeController", "Could not read the structure: %1")
                               .arg(QString::fromUtf8(error.what()));
        }
        result.structure.reset();
        return result;
    }

    McstructureData loadMcstructure(const QUrl& url) {
        const auto read = io::DocumentIo::readAll(url);
        if (!read.ok()) {
            McstructureData result;
            result.error = QCoreApplication::translate("BlockVolumeController", "Could not read the structure: %1").arg(read.error);
            return result;
        }
        return parseMcstructure(read.data);
    }

    std::shared_ptr<VoxelSceneData> buildScene(const bl::mcstructure& structure) {
        auto grid = voxel_mesh::createVoxelDataFromMcstructure(structure);
        auto scene = std::make_shared<VoxelSceneData>();
        if (const auto bounds = voxel_mesh::fullBounds(grid)) {
            auto mesh = voxel_mesh::build(grid, *bounds);
            scene->opaqueVertices = std::move(mesh.opaqueVertices);
            scene->opaqueIndices = std::move(mesh.opaqueIndices);
            scene->transparentVertices = std::move(mesh.transparentVertices);
            scene->transparentIndices = std::move(mesh.transparentIndices);
        }
        scene->modelSize = QVector3D(structure.size_x(), structure.size_y(), structure.size_z());
        scene->voxels = std::make_shared<const VoxelGrid>(std::move(grid));
        const auto origin = structure.origin();
        scene->origin = QVector3D(origin.x, origin.y, origin.z);
        scene->revision = 1;
        return scene;
    }
}  // namespace

bool McstructureVolumeSource::configureStructure(const QUrl& url) {
    if (url.isEmpty()) return false;
    url_ = url;
    raw_.clear();
    volume_.reset();
    return true;
}

bool McstructureVolumeSource::configureBytes(const QByteArray& raw) {
    if (raw.isEmpty()) return false;
    url_.clear();
    raw_ = raw;
    volume_.reset();
    return true;
}

std::shared_ptr<BlockVolumeLoadResult> McstructureVolumeSource::load(const std::function<void(int)>& reportProgress) {
    auto result = std::make_shared<BlockVolumeLoadResult>();
    reportProgress(5);
    auto data = raw_.isEmpty() ? loadMcstructure(url_) : parseMcstructure(raw_);
    if (!data.valid()) {
        result->error = data.error;
        return result;
    }
    reportProgress(45);
    volume_ = std::move(data.structure);
    result->scene = buildScene(*volume_);
    result->documentRoot = std::move(data.root);
    result->documentTitle =
        url_.isEmpty() ? QCoreApplication::translate("BlockVolumeController", "Clipboard") : io::DocumentIo::displayName(url_);
    reportProgress(100);
    return result;
}

QByteArray McstructureVolumeSource::exportStructure(const VoxelSceneData&, const bl::block_box& selection, bool exportEntities,
                                                    bool useNewFormat) const {
    if (!volume_ || !selection.is_valid()) return {};
    const auto fullBounds = bl::block_box::from_min_and_size({0, 0, 0}, volume_->size_x(), volume_->size_y(), volume_->size_z());
    const auto bounds = selection.normalized().intersected(fullBounds);
    if (!bounds.is_valid()) return {};
    const auto raw = buildExportStructure(*volume_, bounds, useNewFormat ? 2 : 1, exportEntities)->to_raw();
    return QByteArray(raw.data(), static_cast<qsizetype>(raw.size()));
}
