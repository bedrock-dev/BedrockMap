#ifndef CHUNKOPERATOR_H
#define CHUNKOPERATOR_H

#include <qregion.h>

#include <QRegion>
#include <QString>
#include <optional>

#include "bedrock_key.h"
#include "chunkio.h"

class AsyncLevelLoader;

/// Helper that operates on selected chunk regions (export, import, delete, etc.).
class ChunkOperator {
   public:
    ChunkOperator() = delete;

    /// Export all chunks inside the given QRegion to a binary file.
    static void exportRegion(const QRegion &chunkRegion, const QString &filePath, AsyncLevelLoader &loader, int dim);

    /// Import all chunks from an ExportedRegion into the level.
    static void importRegion(const ExportedRegion &region, AsyncLevelLoader &loader);

    static void deleteRegion(const QRegion &chunkRegion, AsyncLevelLoader &loader, int dim);

    static void createVoid(const QRegion &chunkRegion, AsyncLevelLoader &loader, int dim);

    static void setRegionBiome(const QRegion &chunkRegion, AsyncLevelLoader &loader, bl::biome biome, int dim);
};

/// Operations that work on block-level regions extracted from chunks.
class BlockRegionOperator {
   public:
    BlockRegionOperator() = delete;

    /// Export selected chunks as an mcstructure file. When blockBounds is set,
    /// only blocks and block entities inside that world-space box are emitted.
    /// Entities are emitted when exportEntities is true and are filtered by their world position.
    static bool exportMcstructure(const QRegion &chunkRegion, const QString &filePath, AsyncLevelLoader &loader, int dim,
                                  bool compress = false, const std::optional<bl::block_box> &blockBounds = std::nullopt,
                                  int32_t version = 1, bool exportEntities = false);
};

#endif  // CHUNKOPERATOR_H
