#ifndef BEDROCKMAP_ASYNCLEVELLOADER_H
#define BEDROCKMAP_ASYNCLEVELLOADER_H

#include <qimage.h>

#include <QFuture>
#include <QRegion>
#include <QRunnable>
#include <QThreadPool>
#include <atomic>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bedrock_key.h"
#include "chunk.h"
#include "chunk_task.h"
#include "chunkcoordsservice.h"
#include "chunkeditservice.h"
#include "chunkstorage.h"
#include "config.h"
#include "nbt.h"
#include "regioncachemanager.h"
#include "regionrenderscheduler.h"
#include "render_options.h"

class AsyncLevelLoader;

struct GlobalNBTLoadResult {
    bl::village_data villageData;
    bl::general_kv_nbts portalData;
    bl::general_kv_nbts playerData;
    bl::general_kv_nbts mapData;
    bl::general_kv_nbts structuresData;
    bl::general_kv_nbts otherData;

    void clear() {
        villageData.clear_data();
        portalData.clear_data();
        playerData.clear_data();
        mapData.clear_data();
        structuresData.clear_data();
        otherData.clear_data();
    }
};

class AsyncLevelLoader : public QObject {
    Q_OBJECT

   signals:
    void dirtyChanged();
    /// Map content changed (a region tile finished, chunk coordinates were
    /// updated, or a preload completed) — listeners should refresh the view.
    void regionReady();
    /// Initial coordinate scan progress, forwarded from ChunkCoordsService.
    void chunkCoordsPreloadProgress(qulonglong scannedKeys, qulonglong chunks);
    /// The initial coordinate scan completed; the map can replace its progress
    /// widget and enter the interactive update phase.
    void chunkCoordsPreloadFinished();

   public:
    // AsyncLevelLoader and its region scheduler/cache are UI-thread owned.
    // Chunk tasks run on the scheduler pool and may only use the explicitly
    // thread-safe height-map APIs while the loader remains open.
    AsyncLevelLoader();

    ~AsyncLevelLoader() override;

    void clearAllCache();

    bool open(const std::string& path);

    void close();

    bl::bedrock_level& level() { return storage_.level(); }

    inline bool isOpen() const { return this->loaded_; }

    inline bool isDirty() const { return storage_.isDirty(); }

    void setFilter(const MapFilter& f) { this->map_filter_ = f; }

    const MapFilter& filter() const { return this->map_filter_; }

    void setTransparentVoid(bool v) { transparent_void_.store(v); }

    bool transparentVoid() const { return transparent_void_.load(); }

    /// Enable or disable indexing all chunk coordinates after opening a level.
    /// This setting must be applied before open().
    void setPreloadAllChunkCoords(bool enabled) { preload_all_chunk_coords_ = enabled; }

    bool preloadAllChunkCoords() const { return preload_all_chunk_coords_; }

    void setRenderViewport(const region_pos& minRegion, const region_pos& maxRegion);

    bool chunkCoordsReady() const { return chunk_coords_service_.ready(); }

    const ChunkCoordsIndex& chunkCoords() const { return chunk_coords_service_.index(); }

    QImage chunkCoordsImage(const region_pos& rp) const;

    std::optional<ChunkCoordsBoundingBox> chunkCoordsBoundingBox(int dim) const;

    void loadGlobalData(GlobalNBTLoadResult& result, std::atomic_bool& stop);

   public:
    /*region cache*/
    QImage bakedBiomeImage(const region_pos& rp);

    /// Region bake state: unloaded, known empty, or ready.
    enum class RegionState { Unloaded, Empty, Ready };
    RegionState regionState(const region_pos& rp, const ChunkRegion** region);

    /// Number of queued or running region bakes.
    [[nodiscard]] int pendingRegionTasks() const;

    /// True when the indexed database has no uncommitted chunk at `pos`.
    [[nodiscard]] bool isChunkAbsent(const bl::chunk_pos& pos) const;

    QImage bakedTerrainImage(const region_pos& rp);

    QImage bakedSlimeChunkImage(const region_pos& rp);

    BlockTipsInfo getBlockTips(const bl::block_pos& p, int dim);

    std::string getBlockName(const bl::block_pos& p, int dim);

    std::unordered_map<QImage*, std::vector<bl::vec3>> getActorList(const region_pos& rp);

    std::map<bl::chunk_pos, std::map<QImage*, ChunkRegion::ActorCount>> getActorCountList(const region_pos& rp);

    std::vector<bl::hardcoded_spawn_area> getHSAs(const region_pos& rp);

    /// Thread-safe world-space height-map lookup (-128 means void).
    std::optional<std::array<int16_t, 256>> getHeightMap(const bl::chunk_pos& pos);

    /// Insert a height map already loaded from a chunk.
    void putHeightMap(const bl::chunk_pos& pos, const std::array<int16_t, 256>& hm);

    /*Modify*/
    // Caller owns the returned chunk.
    bl::chunk* getChunk(const bl::chunk_pos& p, bl::chunk_load_policy policy = bl::chunk_load_policy::All);

    // Return a raw chunk from cache or storage.
    std::optional<bl::raw_chunk> getRawChunk(const bl::chunk_pos& p);

    bool deleteChunk(const bl::chunk_pos& p);
    bool putRawChunk(const bl::raw_chunk& raw);

    bool createVoid(const bl::chunk_pos& p);

    bool setRawChunkBiome(const bl::chunk_pos& p, bl::biome biome);

    /// Drop cached region tiles covering edited chunks.
    void invalidateRegionTiles(const std::vector<bl::chunk_pos>& chunks);

    /// Convenience overload for a rectangular chunk selection.
    void invalidateRegionTiles(const QRegion& chunkRegion, int dim);

    void clearChunkCache(const bl::chunk_pos& p);

    bool commit();

    bool commitEdits(const std::unordered_map<std::string, std::string>& globalModifies, const bl::nbt::compound_tag* levelDat);

    ChunkStorage::CommitError lastCommitError() const { return edit_service_.lastCommitError(); }

    std::vector<QString> debugInfo();

    std::pair<int, int> chunkModifyCounts() const { return storage_.chunkModifyCounts(); }

   private:
    void closeImpl();

    ChunkRegion* tryGetRegion(const region_pos& p, bool& empty);

    /// True when the coordinate index holds no chunk at all in the 8x8-chunk render
    /// region, so it cannot bake to anything but an empty tile. Same preconditions
    /// as isChunkAbsent(), without its pending-edit guard: see the definition.
    [[nodiscard]] bool isRegionAbsent(const region_pos& p) const;

    // Look up an already-cached region without scheduling a load; empty=true for known-empty.
    ChunkRegion* peekRegion(const region_pos& p, bool& empty);

   private:
    std::atomic_bool loaded_{false};
    ChunkStorage storage_;
    RegionCacheManager cache_manager_;
    RegionRenderScheduler region_scheduler_;
    MapFilter map_filter_;
    std::atomic_bool transparent_void_{false};
    bool preload_all_chunk_coords_{false};
    ChunkCoordsService chunk_coords_service_;
    ChunkEditService edit_service_;

    void requestRefresh();

    RegionTimer region_load_timer_;
    RegionTimer region_render_timer_;
};

#endif  // BEDROCKMAP_ASYNCLEVELLOADER_H
