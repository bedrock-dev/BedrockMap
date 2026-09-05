#include "regioncachemanager.h"

#include <QColor>

#include "maptile.h"
#include "utils.h"

RegionCacheManager::RegionCacheManager() : owner_thread_(QThread::currentThread()) {
    for (int dim : {0, 1, 2}) {
        region_cache_[dim] = std::make_unique<QCache<region_pos, ChunkRegion>>(setting::current().REGION_CACHE_SIZE);
        invalid_cache_[dim] = std::make_unique<QCache<region_pos, char>>(setting::current().EMPTY_REGION_CACHE_SIZE);
    }
    slime_chunk_cache_ = std::make_unique<QCache<region_pos, QImage>>(8192);
    height_map_cache_ = std::make_unique<QCache<bl::chunk_pos, std::array<int16_t, 256>>>(setting::current().HEIGHT_MAP_CACHE_SIZE);
}

void RegionCacheManager::assertOwnerThread() const {
    Assert(QThread::currentThread() == owner_thread_, "RegionCacheManager",
           "region and image caches must be accessed from the owning thread");
}

ChunkRegion *RegionCacheManager::findRegion(const region_pos &pos, bool &empty) {
    assertOwnerThread();
    empty = false;
    auto *invalid = ensureDimCache(invalid_cache_, pos.dim, setting::current().EMPTY_REGION_CACHE_SIZE)->operator[](pos);
    if (invalid) {
        empty = true;
        return nullptr;
    }
    return ensureDimCache(region_cache_, pos.dim, setting::current().REGION_CACHE_SIZE)->operator[](pos);
}

ChunkRegion *RegionCacheManager::peekRegion(const region_pos &pos, bool &empty) { return findRegion(pos, empty); }

void RegionCacheManager::insertRegion(const region_pos &pos, ChunkRegion *region) {
    assertOwnerThread();
    ensureDimCache(region_cache_, pos.dim, setting::current().REGION_CACHE_SIZE)->insert(pos, region);
}

void RegionCacheManager::insertEmpty(const region_pos &pos) {
    assertOwnerThread();
    ensureDimCache(invalid_cache_, pos.dim, setting::current().EMPTY_REGION_CACHE_SIZE)->insert(pos, new char(0));
}

void RegionCacheManager::removeRegion(const region_pos &pos) {
    assertOwnerThread();
    auto regionIt = region_cache_.find(pos.dim);
    if (regionIt != region_cache_.end()) regionIt->second->remove(pos);
    auto invalidIt = invalid_cache_.find(pos.dim);
    if (invalidIt != invalid_cache_.end()) invalidIt->second->remove(pos);
}

void RegionCacheManager::clear() {
    assertOwnerThread();
    for (auto &[dim, cache] : region_cache_) cache->clear();
    for (auto &[dim, cache] : invalid_cache_) cache->clear();
    slime_chunk_cache_->clear();
    QMutexLocker lock(&height_map_mutex_);
    height_map_cache_->clear();
}

std::optional<std::array<int16_t, 256>> RegionCacheManager::heightMap(const bl::chunk_pos &pos) {
    QMutexLocker lock(&height_map_mutex_);
    auto *cached = height_map_cache_->object(pos);
    if (cached) return *cached;
    return std::nullopt;
}

void RegionCacheManager::putHeightMap(const bl::chunk_pos &pos, const std::array<int16_t, 256> &heightMap) {
    QMutexLocker lock(&height_map_mutex_);
    if (!height_map_cache_->object(pos)) height_map_cache_->insert(pos, new std::array<int16_t, 256>(heightMap));
}

QImage RegionCacheManager::slimeChunkImage(const region_pos &pos) {
    assertOwnerThread();
    if (pos.dim != 0) return {};
    auto *image = slime_chunk_cache_->operator[](pos);
    if (image) return *image;

    QImage result(constant::RW << 4, constant::RW << 4, QImage::Format_Indexed8);
    result.setColor(0, qRgba(0, 0, 0, 0));
    result.setColor(1, qRgba(29, 145, 44, 190));
    for (int rw = 0; rw < constant::RW; ++rw) {
        for (int rh = 0; rh < constant::RW; ++rh) {
            bl::chunk_pos chunkPos(pos.x + rw, pos.z + rh, pos.dim);
            const auto color = chunkPos.is_slime() ? 1 : 0;
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) result.setPixel((rw << 4) + x, (rh << 4) + z, color);
            }
        }
    }
    slime_chunk_cache_->insert(pos, new QImage(result));
    return result;
}

std::vector<QString> RegionCacheManager::debugInfo() const {
    assertOwnerThread();
    std::vector<QString> result;
    result.emplace_back("Region cache:");
    for (const auto &[dim, cache] : region_cache_) {
        result.push_back(
            QString(" - [%1]: %2/%3").arg(QString::number(dim), QString::number(cache->totalCost()), QString::number(cache->maxCost())));
    }
    result.emplace_back("Null region cache:");
    for (const auto &[dim, cache] : invalid_cache_) {
        result.push_back(
            QString(" - [%1]: %2/%3").arg(QString::number(dim), QString::number(cache->totalCost()), QString::number(cache->maxCost())));
    }
    result.push_back(QString("Slime Chunk cache: %1/%2")
                         .arg(QString::number(slime_chunk_cache_->totalCost()), QString::number(slime_chunk_cache_->maxCost())));
    return result;
}
