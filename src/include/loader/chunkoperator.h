#ifndef BEDROCKMAP_CHUNKOPERATOR_H
#define BEDROCKMAP_CHUNKOPERATOR_H

#include <qregion.h>

#include <QRegion>
#include <QUrl>
#include <vector>

#include "bedrock_key.h"
#include "loader/exportedregion.h"

class AsyncLevelLoader;

/// Helper that operates on selected chunk regions (export, import, delete, etc.).
class ChunkOperator {
   public:
    ChunkOperator() = delete;

    /// Export all chunks inside the given QRegion to a binary file.
    static bool exportRegion(const QRegion& chunkRegion, const QUrl& url, AsyncLevelLoader& loader, int dim);

    /// Import all chunks from an ExportedRegion into the level.
    static bool importRegion(const ExportedRegion& region, AsyncLevelLoader& loader, bool invalidate_tiles = true);

    static bool deleteRegion(const QRegion& chunkRegion, AsyncLevelLoader& loader, int dim, bool invalidate_tiles = true);

    /// Mark every indexed chunk in the dimension for deletion except those in
    /// `keepRegion`. `affected_regions` receives each 8x8 chunk region that
    /// contained at least one deleted chunk.
    static bool deleteOutsideRegion(const QRegion& keepRegion, AsyncLevelLoader& loader, int dim,
                                    std::vector<bl::chunk_pos>* affected_regions, bool invalidate_tiles = true);

    static bool createVoid(const QRegion& chunkRegion, AsyncLevelLoader& loader, int dim, bool invalidate_tiles = true);

    static bool setRegionBiome(const QRegion& chunkRegion, AsyncLevelLoader& loader, bl::biome biome, int dim,
                               bool invalidate_tiles = true);
};

#endif  // BEDROCKMAP_CHUNKOPERATOR_H
