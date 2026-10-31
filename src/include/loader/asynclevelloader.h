#ifndef BEDROCKMAP_ASYNCLEVELLOADER_H
#define BEDROCKMAP_ASYNCLEVELLOADER_H

#include <qimage.h>

#include <QRegion>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/config.h"
#include "bedrock_key.h"
#include "chunk.h"
#include "loader/chunkcoordsservice.h"
#include "loader/chunkeditservice.h"
#include "loader/chunkregion.h"
#include "loader/chunkstorage.h"
#include "loader/mapfilter.h"
#include "nbt.h"
#include "render/regioncachemanager.h"
#include "render/regionrenderscheduler.h"

class RenderTimings;
class ResourceManager;

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
    /// A specific region tile finished baking and is ready for a renderer.
    void regionTileReady(region_pos region);
    /// First tile generated in the same worker task as the region bake.
    void regionSnapshotReady(region_pos region, int blocksPerTexel, int layer, const QImage& color, const QImage& surface, bool hasTerrain);
    /// Initial coordinate scan progress, forwarded from ChunkCoordsService.
    void chunkCoordsPreloadProgress(qulonglong scannedKeys, qulonglong chunks);
    /// The initial coordinate scan completed; the map can enter the
    /// interactive update phase.
    void chunkCoordsPreloadFinished();

   public:
    // AsyncLevelLoader and its region scheduler/cache are UI-thread owned.
    // Chunk tasks run on the scheduler pool while the loader remains open.
    explicit AsyncLevelLoader(ResourceManager* resources = nullptr);

    ~AsyncLevelLoader() override;

    void setTimings(std::shared_ptr<RenderTimings> timings) {
        const std::lock_guard lock(timings_mutex_);
        timings_ = std::move(timings);
    }
    [[nodiscard]] std::shared_ptr<RenderTimings> timings() const {
        const std::lock_guard lock(timings_mutex_);
        return timings_;
    }

    /// Cache and scheduler occupancy for debug surfaces: populated region tiles,
    /// known-empty markers and the bakes the shared scheduler has queued or running.
    [[nodiscard]] int regionCacheCount() const { return cache_manager_.regionCount(); }
    [[nodiscard]] int emptyRegionCount() const { return cache_manager_.emptyCount(); }
    [[nodiscard]] int pendingBakeCount() const { return region_scheduler_.pendingCount(); }
    [[nodiscard]] int activeBakeCount() const { return region_scheduler_.activeCount(); }

    bool open(const std::string& path, const std::string& xor_key, std::shared_ptr<leveldb::Env> env = {});

    void close();

    bl::bedrock_level& level() { return storage_.level(); }

    inline bool isOpen() const { return this->loaded_; }

    inline bool isDirty() const { return storage_.isDirty(); }

    void setFilter(const MapFilter& filter);

    const MapFilter& filter() const { return this->map_filter_; }
    /// Drop all baked regions and restart work with the current filter.
    void reloadFilteredRegions();

    void setTransparentVoid(bool v) { transparent_void_.store(v); }

    bool transparentVoid() const { return transparent_void_.load(); }

    /// Enable or disable indexing all chunk coordinates after opening a level.
    /// This setting must be applied before open().
    void setPreloadAllChunkCoords(bool enabled) { preload_all_chunk_coords_ = enabled; }

    bool preloadAllChunkCoords() const { return preload_all_chunk_coords_; }
    void setChunkOverviewEnabled(bool enabled) { chunk_overview_enabled_ = enabled; }
    bool chunkOverviewEnabled() const { return preload_all_chunk_coords_ && chunk_coords_service_.overviewEnabled(); }

    void setRenderViewport(const region_pos& minRegion, const region_pos& maxRegion);
    /// Pause render-driven bakes for a session that is no longer visible.
    /// Running work is allowed to finish; queued work is dropped.
    void setRenderActive(bool active);
    void setPendingTileRequest(const TerrainTileRequest& request) { pending_tile_request_ = request; }
    /// Safe reference for sampling a region on a worker thread.
    [[nodiscard]] std::shared_ptr<const ChunkRegion> regionLease(const region_pos& pos);

    bool chunkCoordsReady() const { return chunk_coords_service_.ready(); }

    const ChunkCoordsIndex& chunkCoords() const { return chunk_coords_service_.index(); }

    void loadGlobalData(GlobalNBTLoadResult& result, std::atomic_bool& stop);

   public:
    /*region cache*/
    /// Region bake state: unloaded, known empty, or ready.
    enum class RegionState { Unloaded, Empty, Ready };
    RegionState regionState(const region_pos& rp, const ChunkRegion** region);

    /// True when the indexed database has no uncommitted chunk at `pos`.
    [[nodiscard]] bool isChunkAbsent(const bl::chunk_pos& pos) const;

    /// Return the slime chunk mask for a region.
    QImage bakedSlimeChunkImage(const region_pos& rp);

    BlockTipsInfo getBlockTips(const bl::block_pos& p, int dim);

    std::string getBlockName(const bl::block_pos& p, int dim);

    std::unordered_map<const QImage*, std::vector<bl::vec3>> getActorList(const region_pos& rp);

    std::map<bl::chunk_pos, std::map<const QImage*, ChunkRegion::ActorCount>> getActorCountList(const region_pos& rp);

    std::vector<bl::hardcoded_spawn_area> getHSAs(const region_pos& rp);

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
    void clearAllCache();
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
    mutable std::mutex timings_mutex_;
    std::shared_ptr<RenderTimings> timings_;
    RegionRenderScheduler region_scheduler_;
    MapFilter map_filter_;
    std::atomic_bool transparent_void_{false};
    bool render_active_{false};
    TerrainTileRequest pending_tile_request_;
    quint64 next_region_revision_{0};
    bool preload_all_chunk_coords_{false};
    bool chunk_overview_enabled_{false};
    ChunkCoordsService chunk_coords_service_;
    ChunkEditService edit_service_;

    RegionTimer region_load_timer_;
    RegionTimer region_render_timer_;
};

#endif  // BEDROCKMAP_ASYNCLEVELLOADER_H
