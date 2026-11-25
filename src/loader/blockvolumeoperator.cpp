#include "loader/blockvolumeoperator.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "actor.h"
#include "loader/asynclevelloader.h"
#include "loguru/loguru.hpp"
#include "mcstructure.h"
#include "palette.h"

namespace {

    using LoadedChunks = std::map<std::pair<int, int>, std::unique_ptr<bl::chunk>>;

    void forEachChunkInRegion(const QRegion& chunkRegion, int dim, const std::function<void(const bl::chunk_pos&)>& fn) {
        for (const auto& r : chunkRegion) {
            int x1 = r.x();
            int z1 = r.y();
            int x2 = r.x() + r.width() - 1;
            int z2 = r.y() + r.height() - 1;
            for (int x = x1; x <= x2; x++) {
                for (int z = z1; z <= z2; z++) {
                    fn(bl::chunk_pos(x, z, dim));
                }
            }
        }
    }

    std::string_view blockName(const bl::nbt::compound_tag* block) {
        static constexpr std::string_view unknown = "minecraft:unknown";
        if (!block) return unknown;
        const auto* nameTag = block->get("name");
        const auto* name = nameTag ? nameTag->as<const bl::nbt::string_tag*>() : nullptr;
        return name ? std::string_view(name->value) : unknown;
    }

    bool isAirBlockName(std::string_view name) {
        return bl::is_air_block_name(name) || name == "minecraft:unknown";
    }

    bool readBlockEntityPosition(const bl::nbt::compound_tag* entity, bl::block_pos& position) {
        if (!entity) return false;
        const auto read = [entity](const char* key, int& value) {
            const auto* tag = entity->get(key);
            const auto* intTag = tag ? tag->as<const bl::nbt::int_tag*>() : nullptr;
            if (!intTag) return false;
            value = intTag->value;
            return true;
        };
        return read("x", position.x) && read("y", position.y) && read("z", position.z);
    }

    LoadedChunks loadChunks(const QRegion& chunkRegion, AsyncLevelLoader& loader, int dim) {
        LoadedChunks chunks;
        forEachChunkInRegion(chunkRegion, dim, [&](const bl::chunk_pos& position) {
            auto* chunk = loader.getChunk(position, bl::chunk_load_policy::All);
            if (!chunk) return;
            chunks[{position.x, position.z}] = std::unique_ptr<bl::chunk>(chunk);
        });
        return chunks;
    }

    QRegion chunkRegionForBounds(const bl::block_box& bounds) {
        if (!bounds.is_valid()) return {};
        const auto first = bl::block_pos{bounds.min_pos.x, 0, bounds.min_pos.z}.to_chunk_pos();
        const auto last = bl::block_pos{bounds.max_pos.x - 1, 0, bounds.max_pos.z - 1}.to_chunk_pos();
        return QRegion(QRect(first.x, first.z, last.x - first.x + 1, last.z - first.z + 1));
    }

    std::unique_ptr<bl::nbt::compound_tag> makeAirBlock() {
        auto air = std::make_unique<bl::nbt::compound_tag>("");
        air->put(new bl::nbt::string_tag("name", "minecraft:air"));
        return air;
    }

    bool deleteBlocksInRegion(const QRegion& chunkRegion, AsyncLevelLoader& loader, int dim,
                              const std::optional<bl::block_box>& blockBounds, bool deleteEntities) {
        if (chunkRegion.isEmpty()) return false;

        const auto chunks = loadChunks(chunkRegion, loader, dim);
        if (chunks.empty()) return false;

        const auto air = makeAirBlock();
        std::vector<bl::chunk_pos> edited;
        edited.reserve(chunks.size());
        for (const auto& [key, source] : chunks) {
            if (!source) continue;

            const int baseX = key.first * 16;
            const int baseZ = key.second * 16;
            const auto [chunkMinY, chunkMaxY] = source->get_y_range();
            if (chunkMaxY < chunkMinY) continue;

            const bl::block_box chunkBounds{{baseX, chunkMinY, baseZ}, {baseX + 16, chunkMaxY + 1, baseZ + 16}};
            const auto intersection = blockBounds ? chunkBounds.intersected(*blockBounds) : chunkBounds;
            if (!intersection.is_valid()) continue;

            const bl::block_box local{{intersection.min_pos.x - baseX, intersection.min_pos.y, intersection.min_pos.z - baseZ},
                                      {intersection.max_pos.x - baseX, intersection.max_pos.y, intersection.max_pos.z - baseZ}};
            source->fill_blocks(local, air.get());
            source->compact();

            if (deleteEntities) {
                // Reserved for the chunk actor-removal API; do not alter entity data yet.
            }

            loader.putRawChunk(source->to_raw_chunk());
            edited.emplace_back(key.first, key.second, dim);
        }

        if (edited.empty()) return false;
        loader.invalidateRegionTiles(edited);
        return true;
    }

}  // namespace

QRegion BlockVolumeOperator::regionForBounds(const bl::block_box& blockBounds) { return chunkRegionForBounds(blockBounds); }

std::string BlockVolumeOperator::exportWorldMcstructureData(AsyncLevelLoader& loader, int dim, const bl::block_box& exportBounds,
                                                            int32_t version, bool exportEntities, bool includeAir,
                                                            bool includeStructureVoid) {
    if (!exportBounds.is_valid()) return {};

    const QRegion chunkRegion = chunkRegionForBounds(exportBounds);
    const auto chunks = loadChunks(chunkRegion, loader, dim);

    const bl::block_pos origin = exportBounds.min_pos;
    const bl::block_pos size{exportBounds.size_x(), exportBounds.size_y(), exportBounds.size_z()};
    bl::mcstructure_builder builder(size, origin, version);
    if (includeAir) {
        const auto air = makeAirBlock();
        builder.fill_blocks(bl::block_box::from_min_and_size({0, 0, 0}, size.x, size.y, size.z), air.get());
    }

    for (const auto& [key, chunk] : chunks) {
        if (!chunk) continue;
        const int baseX = key.first * 16;
        const int baseZ = key.second * 16;
        const auto [chunkMinY, chunkMaxY] = chunk->get_y_range();
        const auto chunkBounds = bl::block_box::from_min_and_size({baseX, chunkMinY, baseZ}, 16, chunkMaxY - chunkMinY + 1, 16);
        const auto intersection = chunkBounds.intersected(exportBounds);
        for (int y = intersection.min_pos.y; y < intersection.max_pos.y; ++y) {
            for (int x = intersection.min_pos.x; x < intersection.max_pos.x; ++x) {
                for (int z = intersection.min_pos.z; z < intersection.max_pos.z; ++z) {
                    for (int layer = 0;; ++layer) {
                        const auto* block = chunk->get_block_raw(x - baseX, y, z - baseZ, layer);
                        if (!block) break;
                        const auto name = blockName(block);
                        if (isAirBlockName(name) ||
                            (!includeStructureVoid && bl::is_structure_void_block_name(name))) {
                            continue;
                        }
                        builder.set_block(layer, {x - origin.x, y - origin.y, z - origin.z}, block);
                    }
                }
            }
        }

        // Block entities belong to their blocks and are independent of the actor option.
        for (const auto* entity : chunk->block_entities()) {
            bl::block_pos worldPosition;
            if (!readBlockEntityPosition(entity, worldPosition)) continue;
            const bl::block_pos localPosition = worldPosition - origin;
            if (!exportBounds.contains(worldPosition)) {
                continue;
            }
            if (localPosition.x < 0 || localPosition.y < 0 || localPosition.z < 0 || localPosition.x >= size.x ||
                localPosition.y >= size.y || localPosition.z >= size.z) {
                continue;
            }
            builder.set_block_entity(localPosition, entity);
        }

        if (exportEntities) {
            for (const auto* entity : chunk->entities()) {
                if (!entity || !entity->root()) continue;
                const auto position = entity->pos();
                const auto blockPosition = bl::block_pos{static_cast<int>(std::floor(position.x)), static_cast<int>(std::floor(position.y)),
                                                         static_cast<int>(std::floor(position.z))};
                if (!exportBounds.contains(blockPosition)) continue;
                builder.add_entity(entity->root());
            }
        }
    }

    return builder.build().to_raw();
}

BlockVolumeMutationResult BlockVolumeOperator::importMcstructureIntoWorld(const bl::mcstructure& structure, const bl::block_pos& position,
                                                                          AsyncLevelLoader& loader, int dim, bool importEntities,
                                                                          bool replaceNonAirBlocks) {
    BlockVolumeMutationResult result;
    result.dimension = dim;
    const auto fail = [&result](const QString& message) {
        result.success = false;
        result.error = message;
        return result;
    };
    const bl::block_pos size = structure.size();
    if (size.x <= 0 || size.y <= 0 || size.z <= 0) {
        return fail(QCoreApplication::translate("BlockVolumeOperator", "The structure has an invalid size."));
    }

    const auto firstChunk = bl::block_pos{position.x, 0, position.z}.to_chunk_pos();
    const auto lastChunk = bl::block_pos{position.x + size.x - 1, 0, position.z + size.z - 1}.to_chunk_pos();
    const auto placedBounds = bl::block_box::from_min_and_size(position, size.x, size.y, size.z);
    result.region = regionForBounds(placedBounds);
    if (result.region.isEmpty()) {
        return fail(QCoreApplication::translate("BlockVolumeOperator", "The import range does not cover a valid chunk region."));
    }
    const bl::block_pos delta = position - structure.origin();
    const auto air = makeAirBlock();

    std::map<bl::chunk_pos, std::unique_ptr<bl::chunk>> targets;
    for (int cx = firstChunk.x; cx <= lastChunk.x; ++cx) {
        for (int cz = firstChunk.z; cz <= lastChunk.z; ++cz) {
            const bl::chunk_pos cp(cx, cz, dim);
            auto raw = loader.getRawChunk(cp);
            if (!raw.has_value()) {
                if (!loader.isChunkAbsent(cp)) {
                    LOG_F(WARNING, "BlockVolumeOperator: chunk (%d, %d) could not be read, aborting import", cx, cz);
                    return fail(
                        QCoreApplication::translate("BlockVolumeOperator", "Target chunk (%1, %2) could not be read.").arg(cx).arg(cz));
                }
                raw = loader.makeVoidRawChunk(cp);
                LOG_F(INFO, "BlockVolumeOperator: creating void chunk (%d, %d) for import", cx, cz);
            }
            auto target = std::make_unique<bl::chunk>(cp);
            if (!target->load_from_raw_chunk(raw.value())) {
                LOG_F(WARNING, "BlockVolumeOperator: chunk (%d, %d) could not be parsed, aborting import", cx, cz);
                return fail(
                    QCoreApplication::translate("BlockVolumeOperator", "Target chunk (%1, %2) could not be parsed.").arg(cx).arg(cz));
            }
            targets.emplace(cp, std::move(target));
        }
    }

    std::set<std::tuple<int, int, int>> acceptedColumns;
    std::set<std::tuple<int, int, int>> importedBlockEntityPositions;
    std::vector<bl::chunk_pos> edited;
    for (auto& [cp, target] : targets) {
        const int baseX = cp.x * 16;
        const int baseZ = cp.z * 16;
        const int x0 = std::max(position.x, baseX);
        const int x1 = std::min(position.x + size.x, baseX + 16);
        const int z0 = std::max(position.z, baseZ);
        const int z1 = std::min(position.z + size.z, baseZ + 16);

        bool changed = false;
        for (int x = x0; x < x1; ++x) {
            for (int z = z0; z < z1; ++z) {
                for (int y = 0; y < size.y; ++y) {
                    std::vector<const bl::nbt::compound_tag*> layers;
                    layers.reserve(structure.layer_count());
                    bool hasEntry = false;
                    for (int layer = 0; layer < static_cast<int>(structure.layer_count()); ++layer) {
                        const auto* entry = structure.block_at(layer, x - position.x, y, z - position.z);
                        layers.push_back(entry ? entry->tag : nullptr);
                        hasEntry = hasEntry || (entry && entry->tag);
                    }
                    if (!hasEntry) continue;

                    const int worldY = position.y + y;
                    if (!replaceNonAirBlocks && target->is_block_column_non_air(x - baseX, worldY, z - baseZ)) continue;
                    if (!target->replace_block_column(x - baseX, worldY, z - baseZ, layers, air.get())) {
                        return fail(QCoreApplication::translate("BlockVolumeOperator", "Could not replace a target block column."));
                    }
                    acceptedColumns.emplace(x, worldY, z);
                    changed = true;
                }
            }
        }

        for (size_t i = 0; i < structure.block_entity_count(); ++i) {
            const auto* entity = structure.block_entities()[i];
            if (!entity) continue;
            const auto local = structure.block_entity_local_position(i);
            const int worldX = position.x + local.x;
            const int worldY = position.y + local.y;
            const int worldZ = position.z + local.z;
            if (worldX < x0 || worldX >= x1 || worldZ < z0 || worldZ >= z1) continue;
            if (!acceptedColumns.contains({worldX, worldY, worldZ})) continue;
            target->set_block_entity(worldX - baseX, worldY, worldZ - baseZ, entity);
            importedBlockEntityPositions.emplace(worldX, worldY, worldZ);
            changed = true;
        }

        if (importEntities) {
            for (const auto* entity : structure.entities()) {
                const auto entityPosition = bl::actor::position_from_nbt(entity);
                if (!entityPosition) continue;
                const bl::vec3 source = *entityPosition;
                const bl::vec3 world{source.x + delta.x, source.y + delta.y, source.z + delta.z};
                const bl::block_pos block{static_cast<int>(std::floor(world.x)), static_cast<int>(std::floor(world.y)),
                                          static_cast<int>(std::floor(world.z))};
                if (!placedBounds.contains(block)) continue;
                const auto entityChunk = block.to_chunk_pos();
                if (entityChunk.x != cp.x || entityChunk.z != cp.z) continue;
                if (!target->add_actor(loader.level(), entity, world)) {
                    LOG_F(WARNING, "BlockVolumeOperator: skipping a structure entity with no usable id");
                    continue;
                }
                changed = true;
            }
        }

        if (!changed) continue;

        edited.push_back(cp);
    }

    for (const auto& [worldX, worldY, worldZ] : acceptedColumns) {
        if (importedBlockEntityPositions.contains({worldX, worldY, worldZ})) continue;
        const auto chunkOffset = bl::block_pos{worldX, worldY, worldZ}.to_chunk_pos();
        const bl::chunk_pos cp(chunkOffset.x, chunkOffset.z, dim);
        const auto target = targets.find(cp);
        if (target == targets.end()) continue;
        target->second->remove_block_entity(worldX - cp.x * 16, worldY, worldZ - cp.z * 16);
    }

    if (edited.empty()) {
        return fail(QCoreApplication::translate("BlockVolumeOperator", "The structure did not change any target blocks."));
    }

    std::vector<bl::raw_chunk> staged;
    staged.reserve(edited.size());
    for (const auto& cp : edited) staged.push_back(targets.at(cp)->to_raw_chunk());
    if (!loader.putRawChunks(staged)) {
        return fail(QCoreApplication::translate("BlockVolumeOperator", "Could not update the in-memory chunk cache."));
    }

    loader.invalidateRegionTiles(edited);
    LOG_F(INFO, "BlockVolumeOperator: imported mcstructure (%d x %d x %d) at (%d, %d, %d) into %llu chunks", size.x, size.y, size.z,
          position.x, position.y, position.z, static_cast<unsigned long long>(edited.size()));
    result.success = true;
    result.error.clear();
    return result;
}

bool BlockVolumeOperator::deleteWorldBlocks(AsyncLevelLoader& loader, int dim, const bl::block_box& blockBounds, bool deleteEntities) {
    if (!blockBounds.is_valid()) return false;
    return deleteBlocksInRegion(chunkRegionForBounds(blockBounds), loader, dim, blockBounds, deleteEntities);
}
