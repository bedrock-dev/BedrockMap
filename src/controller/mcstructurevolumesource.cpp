#include "controller/mcstructurevolumesource.h"

#include <QCoreApplication>
#include <QUrl>
#include <cmath>
#include <exception>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "chunk_data_position.h"
#include "io/documentio.h"
#include "mcstructure.h"
#include "nbt.h"
#include "render/voxel_mesh.h"

namespace {
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

    /// Parse raw bytes into an NBT root, or set error.
    std::shared_ptr<const bl::nbt::compound_tag> parseRoot(const QByteArray& raw, QString& error) {
        if (raw.isEmpty()) {
            error = QCoreApplication::translate("BlockVolumeController", "Invalid or unsupported mcstructure file.");
            return {};
        }
        auto parsed = bl::nbt::parse_one(std::span(reinterpret_cast<const byte_t*>(raw.constData()), static_cast<size_t>(raw.size())));
        std::shared_ptr<const bl::nbt::compound_tag> root(parsed.root);
        if (!parsed || !root) {
            error = QCoreApplication::translate("BlockVolumeController", "Could not read the structure NBT: %1").arg(bl::nbt::parse_error_to_string(parsed.error_code));
            return {};
        }
        return root;
    }

    std::shared_ptr<const bl::nbt::compound_tag> requestRoot(const BlockVolumeRequest& request, QString& error) {
        if (const auto* root = std::get_if<std::shared_ptr<const bl::nbt::compound_tag>>(&request)) {
            if (!*root) error = QCoreApplication::translate("BlockVolumeController", "Invalid or unsupported mcstructure file.");
            return *root;
        }
        if (const auto* raw = std::get_if<QByteArray>(&request)) return parseRoot(*raw, error);
        if (const auto* url = std::get_if<QUrl>(&request)) {
            const auto read = io::DocumentIo::readAll(*url);
            if (!read.ok()) {
                error = QCoreApplication::translate("BlockVolumeController", "Could not read the structure: %1").arg(read.error);
                return {};
            }
            return parseRoot(read.data, error);
        }
        error = QCoreApplication::translate("BlockVolumeController", "Invalid or unsupported mcstructure file.");
        return {};
    }
}  // namespace

BlockVolumeLoadResult McstructureVolumeSource::load(const BlockVolumeRequest& request, const std::function<void(int)>& reportProgress) const {
    return loadStructure(request, reportProgress);
}

BlockVolumeLoadResult McstructureVolumeSource::loadStructure(const BlockVolumeRequest& request,
                                                            const std::function<void(int)>& reportProgress) {
    BlockVolumeLoadResult result;
    try {
        reportProgress(5);
        auto root = requestRoot(request, result.error);
        if (!root) return result;
        reportProgress(30);
        auto structure = std::make_shared<const bl::mcstructure>(bl::parse_mcstructure(*root));
        if (structure->size_x() <= 0 || structure->size_y() <= 0 || structure->size_z() <= 0) {
            result.error = QCoreApplication::translate("BlockVolumeController", "Invalid or unsupported mcstructure file.");
            return result;
        }
        reportProgress(45);
        const auto origin = structure->origin();
        auto scene = makeVoxelScene(voxel_mesh::createVoxelDataFromMcstructure(*structure), QVector3D(origin.x, origin.y, origin.z),
                                    QVector3D(structure->size_x(), structure->size_y(), structure->size_z()));
        if (!scene) {
            result.error = QCoreApplication::translate("BlockVolumeController", "Invalid or unsupported mcstructure file.");
            return result;
        }
        auto volume = std::make_shared<BlockVolume>();
        volume->scene = std::move(scene);
        volume->structure = std::move(structure);
        volume->root = std::move(root);
        result.volume = std::move(volume);
        reportProgress(100);
    } catch (const std::exception& error) {
        result.volume.reset();
        result.error = QCoreApplication::translate("BlockVolumeController", "Could not read the structure: %1").arg(QString::fromUtf8(error.what()));
    }
    return result;
}

QByteArray McstructureVolumeSource::exportStructure(const BlockVolume& volume, const bl::block_box& selection, bool exportEntities,
                                                    bool useNewFormat) const {
    const auto& structure = volume.structure;
    if (!structure || !selection.is_valid()) return {};
    const auto fullBounds = bl::block_box::from_min_and_size({0, 0, 0}, structure->size_x(), structure->size_y(), structure->size_z());
    const auto bounds = selection.normalized().intersected(fullBounds);
    if (!bounds.is_valid()) return {};
    const auto raw = buildExportStructure(*structure, bounds, useNewFormat ? 2 : 1, exportEntities)->to_raw();
    return QByteArray(raw.data(), static_cast<qsizetype>(raw.size()));
}
