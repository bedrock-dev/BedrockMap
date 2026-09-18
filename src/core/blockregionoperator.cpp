#include "blockregionoperator.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "asynclevelloader.h"
#include "loguru/loguru.hpp"
#include "mcstructure.h"

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

    bool isAirBlock(const bl::nbt::compound_tag* block) {
        if (!block) return true;
        const auto* nameTag = block->get("name");
        const auto* name = nameTag ? nameTag->as<const bl::nbt::string_tag*>() : nullptr;
        if (!name) return true;
        // "minecraft:unknown" means there is no usable palette entry, so treat it as empty.
        return name->value == "minecraft:air" || name->value == "minecraft:unknown";
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

    /// Entity position from the "Pos" tag; false when it is missing or malformed. Entities carry
    /// their position as floats (doubles in older files), unlike block entities' int x/y/z.
    bool readEntityPosition(const bl::nbt::compound_tag* entity, bl::vec3& out) {
        if (!entity) return false;
        const auto* tag = entity->get("Pos");
        const auto* list = tag ? tag->as<const bl::nbt::list_tag*>() : nullptr;
        if (!list || list->value.size() < 3) return false;
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
        if (!read(list->value[0], position.x) || !read(list->value[1], position.y) || !read(list->value[2], position.z)) {
            return false;
        }
        out = position;
        return true;
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

}  // namespace

bool BlockRegionOperator::exportMcstructure(const QRegion& chunkRegion, const QString& filePath, AsyncLevelLoader& loader, int dim,
                                            bool /*compress*/, const std::optional<bl::block_box>& blockBounds, int32_t version,
                                            bool exportEntities) {
    if (chunkRegion.isEmpty()) return false;

    const auto chunkBounds = chunkRegion.boundingRect();
    const auto chunks = loadChunks(chunkRegion, loader, dim);
    if (chunks.empty()) return false;

    int minY = std::numeric_limits<int>::max();
    int maxY = std::numeric_limits<int>::min();
    for (const auto& [key, chunk] : chunks) {
        if (!chunk) continue;
        const auto [chunkMinY, chunkMaxY] = chunk->get_y_range();
        minY = std::min(minY, chunkMinY);
        maxY = std::max(maxY, chunkMaxY);
    }
    if (minY > maxY) return false;

    const bl::block_box exportBounds = blockBounds.value_or(bl::block_box::from_min_and_size(
        {chunkBounds.x() * 16, minY, chunkBounds.y() * 16}, chunkBounds.width() * 16, maxY - minY + 1, chunkBounds.height() * 16));
    if (!exportBounds.is_valid()) return false;

    const bl::block_pos origin = exportBounds.min_pos;
    const bl::block_pos size{exportBounds.size_x(), exportBounds.size_y(), exportBounds.size_z()};
    bl::mcstructure_builder builder(size, origin, version);

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
                    // Write every block layer (0..n); stop when the layer runs out.
                    for (int layer = 0;; ++layer) {
                        const auto* block = chunk->get_block_raw(x - baseX, y, z - baseZ, layer);
                        if (!block) break;
                        if (isAirBlock(block)) continue;
                        builder.set_block(layer, {x - origin.x, y - origin.y, z - origin.z}, block);
                    }
                }
            }
        }

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

    const bool saved = builder.build().save_to_file(filePath.toStdString());
    if (saved) {
        LOG_F(INFO, "ChunkOperator: exported mcstructure to %s", filePath.toStdString().c_str());
    } else {
        LOG_F(WARNING, "ChunkOperator: cannot write mcstructure to %s", filePath.toStdString().c_str());
    }
    return saved;
}

bool BlockRegionOperator::importMcstructure(const bl::mcstructure& structure, const bl::block_pos& position, AsyncLevelLoader& loader,
                                            int dim, bool replaceAir) {
    const bl::block_pos size = structure.size();
    if (size.x <= 0 || size.y <= 0 || size.z <= 0) return false;

    // Chunk range the placed box covers. The box is right-open, so the last covered
    // block sits one short of the far corner.
    const auto firstChunk = bl::block_pos{position.x, 0, position.z}.to_chunk_pos();
    const auto lastChunk = bl::block_pos{position.x + size.x - 1, 0, position.z + size.z - 1}.to_chunk_pos();
    const auto placedBounds = bl::block_box::from_min_and_size(position, size.x, size.y, size.z);
    // Entities carry absolute world positions, so they travel by the same delta the blocks do.
    const bl::block_pos delta = position - structure.origin();

    std::vector<bl::chunk_pos> edited;
    for (int cx = firstChunk.x; cx <= lastChunk.x; ++cx) {
        for (int cz = firstChunk.z; cz <= lastChunk.z; ++cz) {
            const bl::chunk_pos cp(cx, cz, dim);
            auto raw = loader.getRawChunk(cp);
            if (!raw.has_value()) {
                LOG_F(WARNING, "BlockRegionOperator: chunk (%d, %d) is not in the level, skipping import", cx, cz);
                continue;
            }
            bl::chunk target(cp);
            if (!target.load_from_raw_chunk(raw.value())) continue;

            const int baseX = cx * 16;
            const int baseZ = cz * 16;
            const int x0 = std::max(position.x, baseX);
            const int x1 = std::min(position.x + size.x, baseX + 16);
            const int z0 = std::max(position.z, baseZ);
            const int z1 = std::min(position.z + size.z, baseZ + 16);

            bool changed = false;
            for (int x = x0; x < x1; ++x) {
                for (int z = z0; z < z1; ++z) {
                    for (int y = 0; y < size.y; ++y) {
                        for (int layer = 0; layer < static_cast<int>(structure.layer_count()); ++layer) {
                            const auto* entry = structure.block_at(layer, x - position.x, y, z - position.z);
                            if (!entry || !entry->tag) continue;
                            if (!replaceAir && isAirBlock(entry->tag)) continue;
                            target.set_block(x - baseX, position.y + y, z - baseZ, entry->tag, layer);
                            changed = true;
                        }
                    }
                }
            }

            for (size_t i = 0; i < structure.block_entity_count(); ++i) {
                const auto* entity = structure.block_entities()[i];
                if (!entity) continue;
                // Local position -> world position; only this chunk's share is written here.
                const auto local = structure.block_entity_local_position(i);
                const int worldX = position.x + local.x;
                const int worldZ = position.z + local.z;
                if (worldX < x0 || worldX >= x1 || worldZ < z0 || worldZ >= z1) continue;
                // Blocks skipped by replaceAir have no block to attach to.
                if (!replaceAir && !structure.block_at(local.x, local.y, local.z)) continue;
                target.set_block_entity(worldX - baseX, position.y + local.y, worldZ - baseZ, entity);
                changed = true;
            }

            for (const auto* entity : structure.entities()) {
                bl::vec3 source{0.0f, 0.0f, 0.0f};
                if (!readEntityPosition(entity, source)) continue;
                const bl::vec3 world{source.x + delta.x, source.y + delta.y, source.z + delta.z};
                const bl::block_pos block{static_cast<int>(std::floor(world.x)), static_cast<int>(std::floor(world.y)),
                                          static_cast<int>(std::floor(world.z))};
                if (!placedBounds.contains(block)) continue;
                const auto entityChunk = block.to_chunk_pos();
                if (entityChunk.x != cx || entityChunk.z != cz) continue;
                // add_actor rejects a tag without Pos/identifier/UniqueID.
                if (!target.add_actor(loader.level(), entity, world)) {
                    LOG_F(WARNING, "BlockRegionOperator: skipping a structure entity with no usable id");
                    continue;
                }
                changed = true;
            }

            if (!changed) continue;

            loader.putRawChunk(target.to_raw_chunk());
            edited.push_back(cp);
        }
    }

    if (edited.empty()) return false;
    loader.invalidateRegionTiles(edited);
    LOG_F(INFO, "BlockRegionOperator: imported mcstructure (%d x %d x %d) at (%d, %d, %d) into %zu chunks", size.x, size.y, size.z,
          position.x, position.y, position.z, edited.size());
    return true;
}
