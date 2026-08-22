#include "chunkoperator.h"

#include <qfile.h>
#include <qlogging.h>

#include <QFile>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <tuple>

#include "asynclevelloader.h"
#include "chunkio.h"
#include "loguru/loguru.hpp"

namespace {

    using LoadedChunks = std::map<std::pair<int, int>, std::unique_ptr<bl::chunk>>;

    void forEachChunkInRegion(const QRegion &chunkRegion, int dim, const std::function<void(const bl::chunk_pos &)> &fn) {
        for (const auto &r : chunkRegion) {
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

    bool isAirBlock(const bl::nbt::compound_tag *block) {
        if (!block) return true;
        const auto *nameTag = block->get("name");
        const auto *name = nameTag ? nameTag->as<const bl::nbt::string_tag *>() : nullptr;
        if (!name) return true;
        return name->value == "minecraft:air" || name->value == "minecraft:cave_air" || name->value == "minecraft:void_air" ||
               name->value == "minecraft:unknown";
    }

    bool readBlockEntityPosition(const bl::nbt::compound_tag *entity, bl::block_pos &position) {
        if (!entity) return false;
        const auto read = [entity](const char *key, int &value) {
            const auto *tag = entity->get(key);
            const auto *intTag = tag ? tag->as<const bl::nbt::int_tag *>() : nullptr;
            if (!intTag) return false;
            value = intTag->value;
            return true;
        };
        return read("x", position.x) && read("y", position.y) && read("z", position.z);
    }

    LoadedChunks loadChunks(const QRegion &chunkRegion, AsyncLevelLoader &loader, int dim) {
        LoadedChunks chunks;
        forEachChunkInRegion(chunkRegion, dim, [&](const bl::chunk_pos &position) {
            auto *chunk = loader.getChunk(position, bl::chunk_load_policy::All);
            if (!chunk) return;
            chunks[{position.x, position.z}] = std::unique_ptr<bl::chunk>(chunk);
        });
        return chunks;
    }

}  // namespace

void ChunkOperator::exportRegion(const QRegion &chunkRegion, const QString &filePath, AsyncLevelLoader &loader, int dim) {
    ExportedRegion region;
    forEachChunkInRegion(chunkRegion, dim, [&](const bl::chunk_pos &cp) {
        auto *ref = loader.getChunk(cp);
        if (!ref) return;
        bl::raw_chunk raw(cp);
        if (!raw.read(loader.level())) return;
        region.addChunk(raw);
        delete ref;
    });

    auto data = region.serialize();
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_F(WARNING, "ChunkExporter: cannot write to %s", filePath.toStdString().c_str());
        return;
    }
    file.write(data.data(), data.size());
    file.close();
    LOG_F(INFO, "ChunkOperator: exported %d chunks to %s", static_cast<int>(region.chunkCount()), filePath.toStdString().c_str());
}

bool BlockRegionOperator::exportMcstructure(const QRegion &chunkRegion, const QString &filePath, AsyncLevelLoader &loader, int dim,
                                             bool /*compress*/, const std::optional<bl::block_box> &blockBounds, int32_t version,
                                             bool exportEntities) {
    if (chunkRegion.isEmpty()) return false;

    const auto chunkBounds = chunkRegion.boundingRect();
    const auto chunks = loadChunks(chunkRegion, loader, dim);
    if (chunks.empty()) return false;

    int minY = std::numeric_limits<int>::max();
    int maxY = std::numeric_limits<int>::min();
    for (const auto &[key, chunk] : chunks) {
        if (!chunk) continue;
        const auto [chunkMinY, chunkMaxY] = chunk->get_pos().get_y_range(chunk->get_version());
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

    for (const auto &[key, chunk] : chunks) {
        if (!chunk) continue;
        const int baseX = key.first * 16;
        const int baseZ = key.second * 16;
        const auto [chunkMinY, chunkMaxY] = chunk->get_pos().get_y_range(chunk->get_version());
        const auto chunkBounds = bl::block_box::from_min_and_size({baseX, chunkMinY, baseZ}, 16, chunkMaxY - chunkMinY + 1, 16);
        const auto intersection = chunkBounds.intersected(exportBounds);
        for (int y = intersection.min_pos.y; y < intersection.max_pos.y; ++y) {
            for (int x = intersection.min_pos.x; x < intersection.max_pos.x; ++x) {
                for (int z = intersection.min_pos.z; z < intersection.max_pos.z; ++z) {
                    const auto *block = chunk->get_block_raw(x - baseX, y, z - baseZ);
                    if (isAirBlock(block)) continue;
                    builder.set_block({x - origin.x, y - origin.y, z - origin.z}, block);
                }
            }
        }

        for (const auto *entity : chunk->block_entities()) {
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
            for (const auto *entity : chunk->entities()) {
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

void ChunkOperator::importRegion(const ExportedRegion &region, AsyncLevelLoader &loader) {
    for (const auto &chunk : region.chunks()) {
        loader.putRawChunk(chunk);
    }
    LOG_F(INFO, "ChunkOperator: imported %d chunks", static_cast<int>(region.chunkCount()));
}

void ChunkOperator::deleteRegion(const QRegion &chunkRegion, AsyncLevelLoader &loader, int dim) {
    forEachChunkInRegion(chunkRegion, dim, [&](const bl::chunk_pos &cp) { loader.deleteChunk(cp); });
}

void ChunkOperator::createVoid(const QRegion &chunkRegion, AsyncLevelLoader &loader, int dim) {
    LOG_F(INFO, "Create void!");
    forEachChunkInRegion(chunkRegion, dim, [&](const bl::chunk_pos &cp) { loader.createVoid(cp); });
}

void ChunkOperator::setRegionBiome(const QRegion &chunkRegion, AsyncLevelLoader &loader, bl::biome biome, int dim) {
    forEachChunkInRegion(chunkRegion, dim, [&](const bl::chunk_pos &cp) { loader.setRawChunkBiome(cp, biome); });
}
