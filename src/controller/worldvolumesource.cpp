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
#include "render/voxelmesh.h"

namespace {
    std::shared_ptr<VoxelSceneData> buildVolumeScene(AsyncLevelLoader* loader, const ChunkArea& area,
                                                     const std::function<void(int)>& reportProgress) {
        if (!loader || !area.valid()) return {};
        const int columns = area.width();
        const int rows = area.height();
        const int totalChunks = std::max(1, columns * rows);
        std::vector<std::vector<bl::chunk*>> chunks(static_cast<size_t>(columns),
                                                    std::vector<bl::chunk*>(static_cast<size_t>(rows), nullptr));
        std::vector<std::unique_ptr<bl::chunk>> ownedChunks;
        ownedChunks.reserve(static_cast<size_t>(totalChunks));
        int completed = 0;
        for (int chunkZ = area.minChunkZ; chunkZ <= area.maxChunkZ; ++chunkZ) {
            for (int chunkX = area.minChunkX; chunkX <= area.maxChunkX; ++chunkX) {
                auto chunk = std::unique_ptr<bl::chunk>(loader->getChunk(bl::chunk_pos(chunkX, chunkZ, area.dimension),
                                                                         bl::chunk_load_policy::Terrain | bl::chunk_load_policy::Others));
                chunks[chunkX - area.minChunkX][chunkZ - area.minChunkZ] = chunk.get();
                ownedChunks.push_back(std::move(chunk));
                ++completed;
                reportProgress(5 + completed * 30 / totalChunks);
            }
        }

        int firstWorldY = 0;
        auto grid = VoxelGrid::fromChunks(chunks, [&](int count) { reportProgress(35 + count * 30 / totalChunks); }, &firstWorldY);
        ownedChunks.clear();
        chunks.clear();
        reportProgress(70);
        const QVector3D origin(area.minChunkX * 16, firstWorldY, area.minChunkZ * 16);
        auto scene = VoxelSceneData::fromGrid(std::move(grid), origin);
        reportProgress(100);
        return scene;
    }
}  // namespace

BlockVolumeLoadResult WorldVolumeSource::load(const BlockVolumeRequest& request, const std::function<void(int)>& reportProgress) const {
    const auto* area = std::get_if<ChunkArea>(&request);
    if (!area) return McstructureVolumeSource::loadStructure(request, reportProgress);

    BlockVolumeLoadResult result;
    if (!loader_ || !loader_->isOpen() || !area->valid()) {
        result.error = QCoreApplication::translate("BlockVolumeController", "The world is not open.");
        return result;
    }
    auto scene = buildVolumeScene(loader_, *area, reportProgress);
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

QByteArray WorldVolumeSource::exportStructure(const BlockVolume& volume, const bl::block_box& localBounds,
                                              const StructureExportOptions& options) const {
    if (!loader_ || !loader_->isOpen() || !volume.scene) return {};
    const auto raw =
        BlockVolumeOperator::exportWorldMcstructureData(*loader_, volume.dimension, localBounds.translated(volume.blockOrigin()),
                                                        options.useNewFormat ? 2 : 1, options.includeEntities, options.includeAir);
    return QByteArray(raw.data(), static_cast<qsizetype>(raw.size()));
}

BlockVolumeMutationResult WorldVolumeSource::commitImport(const BlockVolume& target, const BlockVolume& import,
                                                          const bl::block_box& localBounds, const StructureImportOptions& options) {
    BlockVolumeMutationResult result;
    if (!loader_ || !loader_->isOpen() || !target.scene || !import.structure || !localBounds.is_valid()) {
        result.error = QCoreApplication::translate("WorldVolumeSource", "The target world or import structure is invalid.");
        return result;
    }
    if (localBounds.min_pos.x < 0 || localBounds.max_pos.x > target.scene->modelSize.x() || localBounds.min_pos.z < 0 ||
        localBounds.max_pos.z > target.scene->modelSize.z()) {
        result.error = QCoreApplication::translate("WorldVolumeSource", "The import range exceeds the current 3D preview X/Z bounds.");
        return result;
    }
    const auto worldPosition = target.blockOrigin() + localBounds.min_pos;
    return BlockVolumeOperator::importMcstructureIntoWorld(*import.structure, worldPosition, *loader_, target.dimension,
                                                           options.includeEntities, options.replaceNonAirBlocks);
}

BlockVolumeMutationResult WorldVolumeSource::deleteBlocks(const BlockVolume& volume, const bl::block_box& localBounds,
                                                          bool deleteEntities) {
    BlockVolumeMutationResult result;
    if (!loader_ || !loader_->isOpen() || !volume.scene || !localBounds.is_valid()) return result;
    const auto worldBounds = localBounds.translated(volume.blockOrigin());
    result.region = BlockVolumeOperator::regionForBounds(worldBounds);
    result.dimension = volume.dimension;
    result.success =
        !result.region.isEmpty() && BlockVolumeOperator::deleteWorldBlocks(*loader_, volume.dimension, worldBounds, deleteEntities);
    return result;
}
