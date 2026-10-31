#ifndef BEDROCKMAP_BLOCKVOLUMEOPERATOR_H
#define BEDROCKMAP_BLOCKVOLUMEOPERATOR_H

#include <QRegion>
#include <QUrl>
#include <optional>
#include <string>

#include "bedrock_key.h"
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

    /// Export selected chunks as an mcstructure file.
    static bool exportWorldMcstructure(const QUrl& url, AsyncLevelLoader& loader, int dim, const bl::block_box& blockBounds,
                                       bool compress = false, int32_t version = 1, bool exportEntities = false);

    /// Serialize selected chunks as an mcstructure payload.
    static std::string exportWorldMcstructureData(AsyncLevelLoader& loader, int dim, const bl::block_box& blockBounds,
                                                  bool compress = false, int32_t version = 1, bool exportEntities = false);

    /// Import an mcstructure at a world position.
    static bool importMcstructureIntoWorld(const bl::mcstructure& structure, const bl::block_pos& position, AsyncLevelLoader& loader,
                                           int dim, bool replaceAir = true, bool importEntities = true);

    /// Clear blocks inside a world-space box. Entity removal is reserved for a future chunk API.
    static bool deleteWorldBlocks(AsyncLevelLoader& loader, int dim, const bl::block_box& blockBounds, bool deleteEntities = false);

    /// Replace blocks in the region or the optional block bounds.
    static bool setWorldBlocks(const QRegion& chunkRegion, AsyncLevelLoader& loader, int dim, const bl::nbt::compound_tag* block,
                               const std::optional<bl::block_box>& blockBounds = std::nullopt);
};

#endif  // BEDROCKMAP_BLOCKVOLUMEOPERATOR_H
