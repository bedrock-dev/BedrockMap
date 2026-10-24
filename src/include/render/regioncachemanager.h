#ifndef BEDROCKMAP_REGIONCACHEMANAGER_H
#define BEDROCKMAP_REGIONCACHEMANAGER_H

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QThread>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "base/config.h"
#include "loader/regionbake.h"

class RegionCacheManager {
   public:
    using RegionLease = std::shared_ptr<const ChunkRegion>;

    RegionCacheManager();

    // Region/image caches are UI-thread owned. Only the height-map cache is
    // shared with worker threads and is protected by height_map_mutex_.
    ChunkRegion* findRegion(const region_pos& pos, bool& empty);
    ChunkRegion* peekRegion(const region_pos& pos, bool& empty);
    /// A lease keeps the region alive while a worker samples it, even if the
    /// LRU cache evicts or replaces the entry in the meantime.
    [[nodiscard]] RegionLease findRegionLease(const region_pos& pos, bool& empty);
    void insertRegion(const region_pos& pos, std::shared_ptr<ChunkRegion> region);
    void insertEmpty(const region_pos& pos);
    void removeRegion(const region_pos& pos);

    void clear();
    /// Shrink render caches while a world is in the background. Edit caches,
    /// coordinate indexes and the open database are retained.
    void setActive(bool active);

    /// Occupancy of the region caches, summed over dimensions, plus the
    /// zero-cost "known empty" markers and the height map cache. Debug/stat
    /// surfaces use these; they are owner-thread only, like the caches.
    [[nodiscard]] int regionCount() const;
    [[nodiscard]] qint64 regionCacheBytes() const { return region_cache_.totalCost(); }
    [[nodiscard]] int regionCacheBudgetBytes() const { return region_cache_.maxCost(); }
    [[nodiscard]] quint64 cacheHits() const { return cache_hits_; }
    [[nodiscard]] quint64 cacheMisses() const { return cache_misses_; }
    [[nodiscard]] quint64 inserts() const { return inserts_; }
    [[nodiscard]] int emptyCount() const;
    [[nodiscard]] int heightMapCount() const;

    QImage slimeChunkImage(const region_pos& pos);

    std::optional<std::array<int16_t, 256>> heightMap(const bl::chunk_pos& pos);
    void putHeightMap(const bl::chunk_pos& pos, const std::array<int16_t, 256>& heightMap);
    /// Drop the cached heights of an edited chunk; thread-safe like the other two.
    void removeHeightMap(const bl::chunk_pos& pos);

    std::vector<QString> debugInfo() const;

   private:
    void assertOwnerThread() const;
    [[nodiscard]] int regionCacheLimitBytes() const;
    [[nodiscard]] int heightMapCacheLimit() const;
    [[nodiscard]] int slimeCacheLimit() const;

    struct CachedRegion {
        std::shared_ptr<ChunkRegion> region;
    };

    QCache<region_pos, CachedRegion> region_cache_;
    QCache<region_pos, char> invalid_cache_;
    std::unique_ptr<QCache<region_pos, QImage>> slime_chunk_cache_;

    std::unique_ptr<QCache<bl::chunk_pos, std::array<int16_t, 256>>> height_map_cache_;
    mutable QMutex height_map_mutex_;
    QThread* owner_thread_{nullptr};
    bool active_{true};
    quint64 cache_hits_{0};
    quint64 cache_misses_{0};
    quint64 inserts_{0};
};

#endif  // BEDROCKMAP_REGIONCACHEMANAGER_H
