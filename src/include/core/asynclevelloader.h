#ifndef BEDROCKMAP_ASYNCLEVELLOADER_H
#define BEDROCKMAP_ASYNCLEVELLOADER_H

#include <qcache.h>
#include <qimage.h>

#include <QCache>
#include <QFuture>
#include <QMutex>
#include <QRunnable>
#include <QThreadPool>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bedrock_key.h"
#include "bedrock_level.h"
#include "chunk.h"
#include "chunk_task.h"
#include "chunkcoords.h"
#include "config.h"
#include "nbt.h"
#include "rawchunkcache.h"
#include "renderfilterdialog.h"

class AsyncLevelLoader;

struct GlobalNBTLoadResult {
    bl::village_data villageData;
    bl::general_kv_nbts playerData;
    bl::general_kv_nbts mapData;
    bl::general_kv_nbts otherData;

    void clear() {
        villageData.clear_data();
        playerData.clear_data();
        mapData.clear_data();
        otherData.clear_data();
    }
};

class AsyncLevelLoader : public QObject {
    Q_OBJECT

   signals:
    void dirtyChanged();
    /// Emitted when a new region tile finishes loading/rendering in the background.
    void regionReady();

   public:
    AsyncLevelLoader();

    ~AsyncLevelLoader() override;

    void clearAllCache();

    bool open(const std::string &path);

    void close();

    bl::bedrock_level &level() { return this->level_; }

    inline bool isOpen() const { return this->loaded_; }

    inline bool isDirty() const { return level_cache_.isDirty(); }

    void setFilter(const MapFilter &f) { this->map_filter_ = f; }

    const MapFilter &filter() const { return this->map_filter_; }

    void setTransparentVoid(bool v) { transparent_void_.store(v); }

    bool transparentVoid() const { return transparent_void_.load(); }

    /// Enable or disable indexing all chunk coordinates after opening a level.
    /// This setting must be applied before open().
    void setPreloadAllChunkCoords(bool enabled) { preload_all_chunk_coords_ = enabled; }

    bool preloadAllChunkCoords() const { return preload_all_chunk_coords_; }

    bool chunkCoordsReady() const { return chunk_coords_ready_.load(std::memory_order_acquire); }

    bool chunkCoordsLoading() const { return loaded_.load(std::memory_order_acquire) && preload_all_chunk_coords_ && !chunkCoordsReady(); }

    const ChunkCoordsIndex &chunkCoords() const { return chunk_coords_; }

    const QImage *chunkCoordsImage(const region_pos &rp) const;

    void loadGlobalData(GlobalNBTLoadResult &result, std::atomic_bool &stop);

   public:
    /*region cache*/
    QImage *bakedBiomeImage(const region_pos &rp);

    QImage *bakedTerrainImage(const region_pos &rp);

    QImage *bakedSlimeChunkImage(const region_pos &rp);

    BlockTipsInfo getBlockTips(const bl::block_pos &p, int dim);

    std::string getBlockName(const bl::block_pos &p, int dim);

    std::unordered_map<QImage *, std::vector<bl::vec3>> getActorList(const region_pos &rp);

    std::map<bl::chunk_pos, std::map<QImage *, ChunkRegion::ActorCount>> getActorCountList(const region_pos &rp);

    std::vector<bl::hardcoded_spawn_area> getHSAs(const region_pos &rp);

    /// Height-map cache for shadow rendering.
    /// Returns world-space heights (-128 = void, others = raw + chunk's min_y).
    /// Thread-safe — safe to call from worker threads during renderStyle2.
    std::optional<std::array<int16_t, 256>> getHeightMap(const bl::chunk_pos &pos);

    /// Preload a height map directly (avoids LevelDB read when data is already
    /// available from a loaded chunk). Thread-safe.
    void putHeightMap(const bl::chunk_pos &pos, const std::array<int16_t, 256> &hm);

    /*Modify*/
    // return a chunk from cache or loader; caller owns the returned pointer and must delete it
    bl::chunk *getChunk(const bl::chunk_pos &p, bl::chunk_load_policy policy = bl::chunk_load_policy::All);

    // return a raw chunk from cache or loader
    std::optional<bl::raw_chunk> getRawChunk(const bl::chunk_pos &p);

    // delete chunk
    bool deleteChunk(const bl::chunk_pos &p);
    // modify a chunk
    bool putRawChunk(const bl::raw_chunk &raw);

    bool createVoid(const bl::chunk_pos &p);

    bool setRawChunkBiome(const bl::chunk_pos &p, bl::biome biome);

    void clearChunkCache(const bl::chunk_pos &p);

    void commit();

    bool modifyDBGlobal(const std::unordered_map<std::string, std::string> &modifies);

    bool modifyLeveldat(bl::nbt::compound_tag *nbt);

    std::vector<QString> debugInfo();

    std::pair<int, int> chunkModifyCounts() const { return level_cache_.chunkCounts(); }

   private:
    void updateChunkCoords(const bl::chunk_pos &pos, bool present);

    ChunkRegion *tryGetRegion(const region_pos &p, bool &empty);

    // Look up an already-cached region without scheduling a load; empty=true for known-empty.
    ChunkRegion *peekRegion(const region_pos &p, bool &empty);

   private:
    template <typename T>
    QCache<region_pos, T> *ensureDimCache(std::unordered_map<int, QCache<region_pos, T> *> &caches, int dim, int maxCost) {
        auto it = caches.find(dim);
        if (it != caches.end()) return it->second;
        auto *cache = new QCache<region_pos, T>(maxCost);
        caches[dim] = cache;
        return cache;
    }

    std::atomic_bool loaded_{false};
    bl::bedrock_level level_{};
    RawChunkCache level_cache_;
    // map region cache
    TaskBuffer<region_pos> processing_;
    std::unordered_map<int, QCache<region_pos, ChunkRegion> *> region_cache_;
    std::unordered_map<int, QCache<region_pos, char> *> invalid_cache_;
    QCache<region_pos, QImage> *slime_chunk_cache_;
    QThreadPool pool_;
    MapFilter map_filter_;
    std::atomic_bool transparent_void_{false};
    bool preload_all_chunk_coords_{false};
    std::atomic_bool stop_chunk_coords_preload_{false};
    std::atomic_bool chunk_coords_ready_{false};
    ChunkCoordsIndex chunk_coords_;

    // Height-map cache for shadow rendering (keyed by chunk_pos).
    // Values are raw Data3D/Data2D height_map arrays (256 × int16_t, ~512 bytes each).
    // Populated on first access, survives across region loads.
    // Protected by height_map_mutex_ for worker-thread safety; uses QCache auto-LRU.
    QCache<bl::chunk_pos, std::array<int16_t, 256>> *height_map_cache_ = nullptr;
    mutable QMutex height_map_mutex_;

    RegionTimer region_load_timer_;
    RegionTimer region_render_timer_;
};

#endif  // BEDROCKMAP_ASYNCLEVELLOADER_H
