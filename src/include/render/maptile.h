#ifndef BEDROCKMAP_MAPTILE_H
#define BEDROCKMAP_MAPTILE_H

#include "chunk.h"
#include "loader/mapfilter.h"

struct ChunkRegion;

/// Extracts terrain columns and actors from a chunk into a region snapshot.
class MapTile {
   public:
    MapTile() = delete;

    static void bakeChunkTerrain(bl::chunk* chunk, const MapFilter& filter, int regionX, int regionZ, ChunkRegion* region);
    static void bakeChunkActors(bl::chunk* chunk, const MapFilter& filter, ChunkRegion* region);

   private:
    static void renderTerrainColumn(ChunkRegion* region, bl::chunk* chunk, const MapFilter& filter, int regionX, int regionZ,
                                    int chunkX, int chunkZ, int y, int solidY);
};

#endif  // BEDROCKMAP_MAPTILE_H
