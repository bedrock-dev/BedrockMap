#ifndef BEDROCKMAP_BLOCKREGIONOPERATOR_H
#define BEDROCKMAP_BLOCKREGIONOPERATOR_H

#include <QRegion>
#include <QString>
#include <optional>
#include <string>

#include "bedrock_key.h"
#include "nbt.h"

namespace bl {
    class mcstructure;
}

class AsyncLevelLoader;

class BlockRegionOperator {
   public:
    BlockRegionOperator() = delete;

    /// Export selected chunks as an mcstructure file.
    static bool exportMcstructure(const QString& filePath, AsyncLevelLoader& loader, int dim, const bl::block_box& blockBounds,
                                  bool compress = false, int32_t version = 1, bool exportEntities = false);

    /// Serialize selected chunks as an mcstructure payload.
    static std::string exportMcstructureData(AsyncLevelLoader& loader, int dim, const bl::block_box& blockBounds, bool compress = false,
                                             int32_t version = 1, bool exportEntities = false);

    /// Import an mcstructure at a world position.
    static bool importMcstructure(const bl::mcstructure& structure, const bl::block_pos& position, AsyncLevelLoader& loader, int dim,
                                  bool replaceAir = true);

    /// Replace blocks in the region or the optional block bounds.
    static bool setBlocks(const QRegion& chunkRegion, AsyncLevelLoader& loader, int dim, const bl::nbt::compound_tag* block,
                          const std::optional<bl::block_box>& blockBounds = std::nullopt);

    /// Clear blocks in the region or the optional block bounds.
    static bool deleteBlocks(const QRegion& chunkRegion, AsyncLevelLoader& loader, int dim,
                             const std::optional<bl::block_box>& blockBounds = std::nullopt);
};

#endif  // BEDROCKMAP_BLOCKREGIONOPERATOR_H
