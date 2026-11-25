#ifndef BEDROCKMAP_BLOCKVOLUMEOPERATOR_H
#define BEDROCKMAP_BLOCKVOLUMEOPERATOR_H

#include <QRegion>
#include <QString>
#include <string>

#include "bedrock_key.h"
#include "loader/blockvolumemutation.h"
#include "nbt.h"

namespace bl {
    class mcstructure;
}

class AsyncLevelLoader;

class BlockVolumeOperator {
   public:
    BlockVolumeOperator() = delete;

    /// Render regions intersecting a world-space block volume.
    [[nodiscard]] static QRegion regionForBounds(const bl::block_box& blockBounds);

    /// Serialize selected chunks as an mcstructure payload.
    static std::string exportWorldMcstructureData(AsyncLevelLoader& loader, int dim, const bl::block_box& blockBounds, int32_t version = 1,
                                                  bool exportEntities = false, bool includeAir = true,
                                                  bool includeStructureVoid = false);

    /// Import an mcstructure at a world position. Air in the structure replaces world blocks.
    static BlockVolumeMutationResult importMcstructureIntoWorld(const bl::mcstructure& structure, const bl::block_pos& position,
                                                                AsyncLevelLoader& loader, int dim, bool importEntities,
                                                                bool replaceNonAirBlocks);

    /// Clear blocks inside a world-space box. Entity removal is reserved for a future chunk API.
    static bool deleteWorldBlocks(AsyncLevelLoader& loader, int dim, const bl::block_box& blockBounds, bool deleteEntities = false);
};

#endif  // BEDROCKMAP_BLOCKVOLUMEOPERATOR_H
