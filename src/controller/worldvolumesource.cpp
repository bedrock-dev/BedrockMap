#include "controller/worldvolumesource.h"

#include <QCoreApplication>
#include <algorithm>
#include <exception>
#include <memory>
#include <variant>
#include <vector>

#include "chunk.h"
#include "controller/mcstructurevolumesource.h"
#include "loader/asynclevelloader.h"
#include "loader/blockvolumeoperator.h"
#include "render/voxel_mesh.h"

namespace {
    std::shared_ptr<VoxelSceneData> buildVolumeScene(AsyncLevelLoader* loader, const bl::chunk_pos& minChunk, const bl::chunk_pos& maxChunk,
                                                     const std::function<void(int)>& reportProgress) {
        if (!loader || minChunk.dim != maxChunk.dim || maxChunk.x < minChunk.x || maxChunk.z < minChunk.z) return {};
        const int columns = maxChunk.x - minChunk.x + 1;
        const int rows = maxChunk.z - minChunk.z + 1;
        const int totalChunks = std::max(1, columns * rows);
        std::vector<std::vector<bl::chunk*>> chunks(static_cast<size_t>(columns),
                                                    std::vector<bl::chunk*>(static_cast<size_t>(rows), nullptr));
        std::vector<std::unique_ptr<bl::chunk>> ownedChunks;
        ownedChunks.reserve(static_cast<size_t>(totalChunks));
        int completed = 0;
        for (int chunkZ = minChunk.z; chunkZ <= maxChunk.z; ++chunkZ) {
            for (int chunkX = minChunk.x; chunkX <= maxChunk.x; ++chunkX) {
                auto chunk = std::unique_ptr<bl::chunk>(loader->getChunk(bl::chunk_pos(chunkX, chunkZ, minChunk.dim),
                                                                         bl::chunk_load_policy::Terrain | bl::chunk_load_policy::Others));
                chunks[chunkX - minChunk.x][chunkZ - minChunk.z] = chunk.get();
                ownedChunks.push_back(std::move(chunk));
                ++completed;
                reportProgress(5 + completed * 30 / totalChunks);
            }
        }

        int firstWorldY = 0;
        auto grid =
            voxel_mesh::createVoxelDataFromChunks(chunks, [&](int count) { reportProgress(35 + count * 30 / totalChunks); }, &firstWorldY);
        ownedChunks.clear();
        chunks.clear();
        reportProgress(70);
        const QVector3D origin(minChunk.x * 16, firstWorldY, minChunk.z * 16);
        const QVector3D modelSize(static_cast<float>(columns * 16), static_cast<float>(grid.size()), static_cast<float>(rows * 16));
        auto scene = makeVoxelScene(std::move(grid), origin, modelSize);
        reportProgress(100);
        return scene;
    }
}  // namespace

namespace {
    bl::block_pos originOf(const BlockVolume& volume) {
        const auto& origin = volume.scene->origin;
        return {qRound(origin.x()), qRound(origin.y()), qRound(origin.z())};
    }
}  // namespace

BlockVolumeLoadResult WorldVolumeSource::load(const BlockVolumeRequest& request, const std::function<void(int)>& reportProgress) const {
    const auto* area = std::get_if<ChunkArea>(&request);
    if (!area) return McstructureVolumeSource::loadStructure(request, reportProgress);

    BlockVolumeLoadResult result;
    if (!loader_ || !loader_->isOpen() || area->maxChunkX < area->minChunkX || area->maxChunkZ < area->minChunkZ) {
        result.error = QCoreApplication::translate("BlockVolumeController", "The world is not open.");
        return result;
    }
    auto scene = buildVolumeScene(loader_, {area->minChunkX, area->minChunkZ, area->dimension},
                                  {area->maxChunkX, area->maxChunkZ, area->dimension}, reportProgress);
    if (!scene) {
        result.error = QCoreApplication::translate("BlockVolumeController", "No terrain was found in the current area.");
        return result;
    }
    auto volume = std::make_shared<BlockVolume>();
    volume->scene = std::move(scene);
    volume->dimension = area->dimension;
    result.volume = std::move(volume);
    return result;
}

QByteArray WorldVolumeSource::exportStructure(const BlockVolume& volume, const bl::block_box& localBounds, bool exportEntities,
                                              bool useNewFormat) const {
    if (!loader_ || !loader_->isOpen() || !volume.scene) return {};
    const auto raw = BlockVolumeOperator::exportWorldMcstructureData(*loader_, volume.dimension, localBounds.translated(originOf(volume)),
                                                                     false, useNewFormat ? 2 : 1, exportEntities);
    return QByteArray(raw.data(), static_cast<qsizetype>(raw.size()));
}

BlockVolumeMutationResult WorldVolumeSource::commitImport(const BlockVolume& target, const BlockVolume& import,
                                                          const bl::block_box& localBounds, bool importEntities) {
    BlockVolumeMutationResult result;
    if (!loader_ || !loader_->isOpen() || !target.scene || !import.structure || !localBounds.is_valid()) return result;
    const auto worldPosition = originOf(target) + localBounds.min_pos;
    const auto worldBounds =
        bl::block_box::from_min_and_size(worldPosition, localBounds.size_x(), localBounds.size_y(), localBounds.size_z());
    result.region = BlockVolumeOperator::regionForBounds(worldBounds);
    result.dimension = target.dimension;
    result.success = !result.region.isEmpty() && BlockVolumeOperator::importMcstructureIntoWorld(*import.structure, worldPosition, *loader_,
                                                                                                target.dimension, true, importEntities);
    return result;
}

BlockVolumeMutationResult WorldVolumeSource::deleteBlocks(const BlockVolume& volume, const bl::block_box& localBounds,
                                                          bool deleteEntities) {
    BlockVolumeMutationResult result;
    if (!loader_ || !loader_->isOpen() || !volume.scene || !localBounds.is_valid()) return result;
    const auto worldBounds = localBounds.translated(originOf(volume));
    result.region = BlockVolumeOperator::regionForBounds(worldBounds);
    result.dimension = volume.dimension;
    result.success =
        !result.region.isEmpty() && BlockVolumeOperator::deleteWorldBlocks(*loader_, volume.dimension, worldBounds, deleteEntities);
    return result;
}
