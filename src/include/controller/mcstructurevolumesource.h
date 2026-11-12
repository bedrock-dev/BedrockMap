#ifndef BEDROCKMAP_MCSTRUCTUREVOLUMESOURCE_H
#define BEDROCKMAP_MCSTRUCTUREVOLUMESOURCE_H

#include "controller/blockvolumesource.h"

/// Loads block volumes from mcstructure data. Editing is intentionally unsupported.
class McstructureVolumeSource final : public BlockVolumeSource {
   public:
    [[nodiscard]] BlockVolumeLoadResult load(const BlockVolumeRequest& request,
                                             const std::function<void(int)>& reportProgress) const override;
    [[nodiscard]] QByteArray exportStructure(const BlockVolume& volume, const bl::block_box& localBounds, bool exportEntities,
                                             bool useNewFormat) const override;

    /// Load a file, raw bytes or a parsed root. Chunk area requests are rejected.
    [[nodiscard]] static BlockVolumeLoadResult loadStructure(const BlockVolumeRequest& request,
                                                             const std::function<void(int)>& reportProgress);
};

#endif  // BEDROCKMAP_MCSTRUCTUREVOLUMESOURCE_H
