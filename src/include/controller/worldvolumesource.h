#ifndef BEDROCKMAP_WORLDVOLUMESOURCE_H
#define BEDROCKMAP_WORLDVOLUMESOURCE_H

#include "controller/blockvolumesource.h"

class AsyncLevelLoader;

/// Loads and edits block volumes backed by an open world. Non-area requests
/// load mcstructure data for import previews.
class WorldVolumeSource final : public BlockVolumeSource {
   public:
    [[nodiscard]] bool blockEditingEnabled() const override { return true; }
    void setLevelLoader(AsyncLevelLoader* loader) override { loader_ = loader; }
    [[nodiscard]] BlockVolumeLoadResult load(const BlockVolumeRequest& request,
                                             const std::function<void(int)>& reportProgress) const override;
    [[nodiscard]] QByteArray exportStructure(const BlockVolume& volume, const bl::block_box& localBounds, bool exportEntities,
                                             bool useNewFormat) const override;
    [[nodiscard]] BlockVolumeMutationResult commitImport(const BlockVolume& target, const BlockVolume& import,
                                                         const bl::block_box& localBounds, bool importEntities) override;
    [[nodiscard]] BlockVolumeMutationResult deleteBlocks(const BlockVolume& volume, const bl::block_box& localBounds,
                                                         bool deleteEntities) override;

   private:
    AsyncLevelLoader* loader_{nullptr};
};

#endif  // BEDROCKMAP_WORLDVOLUMESOURCE_H
