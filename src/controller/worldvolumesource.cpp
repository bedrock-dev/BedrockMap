#include "controller/worldvolumesource.h"

#include <QCoreApplication>
#include <algorithm>
#include <exception>
#include <memory>
#include <vector>

#include "chunk.h"
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
        const auto bounds = voxel_mesh::fullBounds(grid);
        if (!bounds) return {};
        reportProgress(70);
        auto mesh = voxel_mesh::build(grid, *bounds);

        auto scene = std::make_shared<VoxelSceneData>();
        scene->origin = QVector3D(minChunk.x * 16, firstWorldY, minChunk.z * 16);
        scene->modelSize = QVector3D(static_cast<float>(columns * 16), static_cast<float>(grid.size()), static_cast<float>(rows * 16));
        scene->opaqueVertices = std::move(mesh.opaqueVertices);
        scene->opaqueIndices = std::move(mesh.opaqueIndices);
        scene->transparentVertices = std::move(mesh.transparentVertices);
        scene->transparentIndices = std::move(mesh.transparentIndices);
        scene->voxels = std::make_shared<const VoxelGrid>(std::move(grid));
        scene->revision = 1;
        reportProgress(100);
        return scene;
    }
}  // namespace

bool WorldVolumeSource::configureArea(int minChunkX, int minChunkZ, int maxChunkX, int maxChunkZ, int dimension) {
    if (maxChunkX < minChunkX || maxChunkZ < minChunkZ) return false;
    min_chunk_x_ = minChunkX;
    min_chunk_z_ = minChunkZ;
    max_chunk_x_ = maxChunkX;
    max_chunk_z_ = maxChunkZ;
    dimension_ = dimension;
    return true;
}

bool WorldVolumeSource::configureImport(const QUrl& url) { return import_source_.configureStructure(url); }

bool WorldVolumeSource::configureImportData(const QByteArray& raw) { return import_source_.configureBytes(raw); }

void WorldVolumeSource::setLevelLoader(AsyncLevelLoader* loader) { loader_ = loader; }

std::shared_ptr<BlockVolumeLoadResult> WorldVolumeSource::load(const std::function<void(int)>& reportProgress) {
    auto result = std::make_shared<BlockVolumeLoadResult>();
    if (!loader_ || !loader_->isOpen() || max_chunk_x_ < min_chunk_x_ || max_chunk_z_ < min_chunk_z_) {
        result->error = QCoreApplication::translate("BlockVolumeController", "The world is not open.");
        return result;
    }
    result->scene =
        buildVolumeScene(loader_, {min_chunk_x_, min_chunk_z_, dimension_}, {max_chunk_x_, max_chunk_z_, dimension_}, reportProgress);
    if (!result->scene) {
        result->error = QCoreApplication::translate("BlockVolumeController", "No terrain was found in the current area.");
        return result;
    }
    result->chunkX = min_chunk_x_;
    result->chunkZ = min_chunk_z_;
    result->dimension = dimension_;
    return result;
}

std::shared_ptr<BlockVolumeLoadResult> WorldVolumeSource::loadImport(const std::function<void(int)>& reportProgress) {
    return import_source_.load(reportProgress);
}

QByteArray WorldVolumeSource::exportStructure(const VoxelSceneData& scene, const bl::block_box& localBounds, bool exportEntities,
                                              bool useNewFormat) const {
    if (!loader_ || !loader_->isOpen()) return {};
    const auto bounds = localBounds.translated({qRound(scene.origin.x()), qRound(scene.origin.y()), qRound(scene.origin.z())});
    const auto raw =
        BlockVolumeOperator::exportWorldMcstructureData(*loader_, dimension_, bounds, false, useNewFormat ? 2 : 1, exportEntities);
    return QByteArray(raw.data(), static_cast<qsizetype>(raw.size()));
}

BlockVolumeMutationResult WorldVolumeSource::commitImport(const VoxelSceneData& scene, const bl::block_box& localBounds,
                                                          bool importEntities) {
    BlockVolumeMutationResult result;
    const auto* structure = import_source_.volume();
    if (!loader_ || !loader_->isOpen() || !structure || !localBounds.is_valid()) return result;
    const auto worldPosition =
        bl::block_pos{qRound(scene.origin.x()) + localBounds.min_pos.x, qRound(scene.origin.y()) + localBounds.min_pos.y,
                      qRound(scene.origin.z()) + localBounds.min_pos.z};
    const auto worldBounds =
        bl::block_box::from_min_and_size(worldPosition, localBounds.size_x(), localBounds.size_y(), localBounds.size_z());
    result.region = BlockVolumeOperator::regionForBounds(worldBounds);
    result.dimension = dimension_;
    result.success = !result.region.isEmpty() &&
                     BlockVolumeOperator::importMcstructureIntoWorld(*structure, worldPosition, *loader_, dimension_, true, importEntities);
    return result;
}

BlockVolumeMutationResult WorldVolumeSource::deleteBlocks(const VoxelSceneData& scene, const bl::block_box& localBounds,
                                                          bool deleteEntities) {
    BlockVolumeMutationResult result;
    if (!loader_ || !loader_->isOpen() || !localBounds.is_valid()) return result;
    const auto worldBounds = localBounds.translated({qRound(scene.origin.x()), qRound(scene.origin.y()), qRound(scene.origin.z())});
    result.region = BlockVolumeOperator::regionForBounds(worldBounds);
    result.dimension = dimension_;
    result.success = !result.region.isEmpty() && BlockVolumeOperator::deleteWorldBlocks(*loader_, dimension_, worldBounds, deleteEntities);
    return result;
}
