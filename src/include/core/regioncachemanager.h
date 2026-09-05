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
#include <unordered_map>
#include <vector>

#include "chunk_task.h"
#include "config.h"

class RegionCacheManager {
   public:
    RegionCacheManager();

    // Region/image caches are UI-thread owned. Only the height-map cache is
    // shared with worker threads and is protected by height_map_mutex_.
    ChunkRegion *findRegion(const region_pos &pos, bool &empty);
    ChunkRegion *peekRegion(const region_pos &pos, bool &empty);
    void insertRegion(const region_pos &pos, ChunkRegion *region);
    void insertEmpty(const region_pos &pos);
    void removeRegion(const region_pos &pos);

    void clear();

    QImage slimeChunkImage(const region_pos &pos);

    std::optional<std::array<int16_t, 256>> heightMap(const bl::chunk_pos &pos);
    void putHeightMap(const bl::chunk_pos &pos, const std::array<int16_t, 256> &heightMap);

    std::vector<QString> debugInfo() const;

   private:
    void assertOwnerThread() const;

    template <typename T>
    QCache<region_pos, T> *ensureDimCache(std::unordered_map<int, std::unique_ptr<QCache<region_pos, T>>> &caches, int dim, int maxCost);

    std::unordered_map<int, std::unique_ptr<QCache<region_pos, ChunkRegion>>> region_cache_;
    std::unordered_map<int, std::unique_ptr<QCache<region_pos, char>>> invalid_cache_;
    std::unique_ptr<QCache<region_pos, QImage>> slime_chunk_cache_;

    std::unique_ptr<QCache<bl::chunk_pos, std::array<int16_t, 256>>> height_map_cache_;
    mutable QMutex height_map_mutex_;
    QThread *owner_thread_{nullptr};
};

template <typename T>
QCache<region_pos, T> *RegionCacheManager::ensureDimCache(std::unordered_map<int, std::unique_ptr<QCache<region_pos, T>>> &caches, int dim,
                                                          int maxCost) {
    auto it = caches.find(dim);
    if (it != caches.end()) return it->second.get();
    auto cache = std::make_unique<QCache<region_pos, T>>(maxCost);
    auto *result = cache.get();
    caches.emplace(dim, std::move(cache));
    return result;
}

#endif  // BEDROCKMAP_REGIONCACHEMANAGER_H
