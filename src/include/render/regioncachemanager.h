#ifndef BEDROCKMAP_REGIONCACHEMANAGER_H
#define BEDROCKMAP_REGIONCACHEMANAGER_H

#include <QCache>
#include <QImage>
#include <QThread>
#include <cstdint>
#include <memory>
#include <vector>

#include "base/config.h"
#include "loader/chunkregion.h"

class RegionCacheManager {
   public:
    using RegionLease = std::shared_ptr<const ChunkRegion>;

    RegionCacheManager();

    // Region/image caches are UI-thread owned.
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
    /// zero-cost "known empty" markers. Debug/stat surfaces use these.
    [[nodiscard]] int regionCount() const;
    [[nodiscard]] int emptyCount() const;
    QImage slimeChunkImage(const region_pos& pos);

    std::vector<QString> debugInfo() const;

   private:
    void assertOwnerThread() const;
    [[nodiscard]] int regionCacheLimitBytes() const;
    [[nodiscard]] int slimeCacheLimit() const;

    struct CachedRegion {
        std::shared_ptr<ChunkRegion> region;
    };

    QCache<region_pos, CachedRegion> region_cache_;
    QCache<region_pos, char> invalid_cache_;
    std::unique_ptr<QCache<region_pos, QImage>> slime_chunk_cache_;

    QThread* owner_thread_{nullptr};
    bool active_{true};
    quint64 cache_hits_{0};
    quint64 cache_misses_{0};
    quint64 inserts_{0};
};

#endif  // BEDROCKMAP_REGIONCACHEMANAGER_H
