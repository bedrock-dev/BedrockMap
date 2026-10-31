#ifndef BEDROCKMAP_WORLDVOLUMESOURCE_H
#define BEDROCKMAP_WORLDVOLUMESOURCE_H

#include <QUrl>

#include "controller/blockvolumesource.h"
#include "controller/mcstructurevolumesource.h"

class AsyncLevelLoader;

/// Loads and edits block volumes backed by an open world.
class WorldVolumeSource final : public BlockVolumeSource {
   public:
    [[nodiscard]] bool blockEditingEnabled() const override { return true; }
    bool configureArea(int minChunkX, int minChunkZ, int maxChunkX, int maxChunkZ, int dimension) override;
    bool configureImport(const QUrl& url) override;
    bool configureImportData(const QByteArray& raw) override;
    void setLevelLoader(AsyncLevelLoader* loader) override;
    std::shared_ptr<BlockVolumeLoadResult> load(const std::function<void(int)>& reportProgress) override;
    std::shared_ptr<BlockVolumeLoadResult> loadImport(const std::function<void(int)>& reportProgress) override;
    [[nodiscard]] QByteArray exportStructure(const VoxelSceneData& scene, const bl::block_box& localBounds, bool exportEntities,
                                             bool useNewFormat) const override;
    [[nodiscard]] BlockVolumeMutationResult commitImport(const VoxelSceneData& scene, const bl::block_box& localBounds,
                                                         bool importEntities) override;
    [[nodiscard]] BlockVolumeMutationResult deleteBlocks(const VoxelSceneData& scene, const bl::block_box& localBounds,
                                                         bool deleteEntities) override;

   private:
    McstructureVolumeSource import_source_;
    AsyncLevelLoader* loader_{nullptr};
    int min_chunk_x_{0};
    int min_chunk_z_{0};
    int max_chunk_x_{-1};
    int max_chunk_z_{-1};
    int dimension_{0};
};

#endif  // BEDROCKMAP_WORLDVOLUMESOURCE_H
