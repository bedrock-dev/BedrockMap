#ifndef BEDROCKMAP_BLOCKREGIONOPERATOR_H
#define BEDROCKMAP_BLOCKREGIONOPERATOR_H

#include <QRegion>
#include <QString>
#include <optional>

#include "bedrock_key.h"
#include "nbt.h"

namespace bl {
    class mcstructure;
}

class AsyncLevelLoader;

/// Operations that work on block-level regions extracted from chunks.
class BlockRegionOperator {
   public:
    BlockRegionOperator() = delete;

    /// Export selected chunks as an mcstructure file. When blockBounds is set,
    /// only blocks and block entities inside that world-space box are emitted.
    /// Entities are emitted when exportEntities is true and are filtered by their world position.
    static bool exportMcstructure(const QRegion& chunkRegion, const QString& filePath, AsyncLevelLoader& loader, int dim,
                                  bool compress = false, const std::optional<bl::block_box>& blockBounds = std::nullopt,
                                  int32_t version = 1, bool exportEntities = false);

    /// Write an mcstructure into the level with its minimum corner at position. Only the
    /// chunks the structure covers are loaded, edited and written back; chunks that do not
    /// exist in the level are skipped. When replaceAir is true the air blocks of the
    /// structure are written as well, clearing whatever they cover; otherwise they are
    /// skipped and the blocks already there stay, along with the block entities sitting on
    /// them. Block entities are placed at their structure position offset by position, and
    /// entities at their world position offset by position - structure.origin(), each getting
    /// a freshly generated id because the id in the file is already taken by the source world.
    static bool importMcstructure(const bl::mcstructure& structure, const bl::block_pos& position, AsyncLevelLoader& loader, int dim,
                                  bool replaceAir = true);

    /// Replace every block inside blockBounds with the given block data. When blockBounds
    /// is nullopt the whole chunk region is filled.
    static bool setBlocks(const QRegion& chunkRegion, AsyncLevelLoader& loader, int dim, const bl::nbt::compound_tag* block,
                          const std::optional<bl::block_box>& blockBounds = std::nullopt);

    /// Clear every block inside blockBounds (replace with air). When blockBounds is nullopt
    /// the whole chunk region is cleared.
    static bool deleteBlocks(const QRegion& chunkRegion, AsyncLevelLoader& loader, int dim,
                             const std::optional<bl::block_box>& blockBounds = std::nullopt);
};

#endif  // BEDROCKMAP_BLOCKREGIONOPERATOR_H
