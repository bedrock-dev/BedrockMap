#ifndef BEDROCKMAP_CHUNKEDITSERVICE_H
#define BEDROCKMAP_CHUNKEDITSERVICE_H

#include <atomic>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>

#include "chunk.h"
#include "chunkcoordsservice.h"
#include "chunkstorage.h"

class RegionCacheManager;

/// Owns mutable chunk operations for one open level.
///
/// The loader remains the public facade, while this service keeps editing and
/// persistence concerns independent from region rendering and scheduling.
class ChunkEditService {
   public:
    using Callback = std::function<void()>;

    ChunkEditService(ChunkStorage &storage, RegionCacheManager &cacheManager, ChunkCoordsService &coordsService,
                     const std::atomic_bool &loaded, const bool &preloadAllChunkCoords, Callback dirtyChanged, Callback coordsChanged);

    bl::chunk *getChunk(const bl::chunk_pos &pos, bl::chunk_load_policy policy);
    std::optional<bl::raw_chunk> getRawChunk(const bl::chunk_pos &pos);

    bool deleteChunk(const bl::chunk_pos &pos);
    bool putRawChunk(const bl::raw_chunk &raw);
    bool createVoid(const bl::chunk_pos &pos);
    bool setRawChunkBiome(const bl::chunk_pos &pos, bl::biome biome);

    void clearChunkCache(const bl::chunk_pos &pos);

    bool commit();
    bool commitEdits(const std::unordered_map<std::string, std::string> &globalModifies, const bl::nbt::compound_tag *levelDat);

    ChunkStorage::CommitError lastCommitError() const { return storage_.lastCommitError(); }
    std::pair<int, int> chunkModifyCounts() const { return storage_.chunkModifyCounts(); }

   private:
    bool canEdit() const;
    void markChanged(const bl::chunk_pos &pos, bool present);
    void notifyDirty();

    ChunkStorage &storage_;
    RegionCacheManager &cache_manager_;
    ChunkCoordsService &coords_service_;
    const std::atomic_bool &loaded_;
    const bool &preload_all_chunk_coords_;
    Callback dirty_changed_;
    Callback coords_changed_;
};

#endif  // BEDROCKMAP_CHUNKEDITSERVICE_H
