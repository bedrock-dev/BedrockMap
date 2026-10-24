#include "render/regioncachemanager.h"

#include <QColor>
#include <algorithm>
#include <limits>

#include "render/maptile.h"
#include "render/terraintiles.h"

namespace {
    constexpr double INACTIVE_CACHE_FRACTION = 0.25;
    // regionCacheSize remains the user's entry-count knob. Convert it to a byte
    // budget so very large regions, CPU images and actor payloads are accounted for
    // instead of every entry sharing the same cost.
    constexpr qint64 NOMINAL_BYTES_PER_REGION = 64 * 1024;
}  // namespace

RegionCacheManager::RegionCacheManager()
    : region_cache_(regionCacheLimitBytes()),
      invalid_cache_(setting::current().emptyRegionCacheSize),
      owner_thread_(QThread::currentThread()) {
    slime_chunk_cache_ = std::make_unique<QCache<region_pos, QImage>>(slimeCacheLimit());
    height_map_cache_ = std::make_unique<QCache<bl::chunk_pos, std::array<int16_t, 256>>>(heightMapCacheLimit());
}

void RegionCacheManager::assertOwnerThread() const {
    Assert(QThread::currentThread() == owner_thread_, "RegionCacheManager",
           "region and image caches must be accessed from the owning thread");
}

ChunkRegion* RegionCacheManager::findRegion(const region_pos& pos, bool& empty) {
    assertOwnerThread();
    empty = false;
    auto* invalid = invalid_cache_.object(pos);
    if (invalid) {
        ++cache_hits_;
        empty = true;
        return nullptr;
    }
    auto* cached = region_cache_.object(pos);
    if (cached)
        ++cache_hits_;
    else
        ++cache_misses_;
    return cached ? cached->region.get() : nullptr;
}

ChunkRegion* RegionCacheManager::peekRegion(const region_pos& pos, bool& empty) { return findRegion(pos, empty); }

RegionCacheManager::RegionLease RegionCacheManager::findRegionLease(const region_pos& pos, bool& empty) {
    assertOwnerThread();
    empty = false;
    if (invalid_cache_.object(pos)) {
        empty = true;
        return {};
    }
    auto* cached = region_cache_.object(pos);
    return cached ? cached->region : RegionLease{};
}

void RegionCacheManager::insertRegion(const region_pos& pos, std::shared_ptr<ChunkRegion> region) {
    assertOwnerThread();
    const int cost = static_cast<int>(std::min<qint64>(region->memoryCost(), std::numeric_limits<int>::max()));
    ++inserts_;
    region_cache_.insert(pos, new CachedRegion{std::move(region)}, cost);
}

void RegionCacheManager::insertEmpty(const region_pos& pos) {
    assertOwnerThread();
    invalid_cache_.insert(pos, new char(0));
}

void RegionCacheManager::removeRegion(const region_pos& pos) {
    assertOwnerThread();
    region_cache_.remove(pos);
    invalid_cache_.remove(pos);
}

int RegionCacheManager::regionCount() const { return region_cache_.size(); }

int RegionCacheManager::emptyCount() const { return invalid_cache_.size(); }

int RegionCacheManager::heightMapCount() const { return height_map_cache_ ? static_cast<int>(height_map_cache_->size()) : 0; }

void RegionCacheManager::clear() {
    assertOwnerThread();
    region_cache_.clear();
    invalid_cache_.clear();
    slime_chunk_cache_->clear();
    QMutexLocker lock(&height_map_mutex_);
    height_map_cache_->clear();
}

void RegionCacheManager::setActive(bool active) {
    assertOwnerThread();
    if (active_ == active) return;
    active_ = active;
    region_cache_.setMaxCost(regionCacheLimitBytes());
    slime_chunk_cache_->setMaxCost(slimeCacheLimit());
    QMutexLocker lock(&height_map_mutex_);
    height_map_cache_->setMaxCost(heightMapCacheLimit());
}

int RegionCacheManager::regionCacheLimitBytes() const {
    const int configured = std::max(1, setting::current().regionCacheSize);
    const qint64 active_budget = static_cast<qint64>(configured) * NOMINAL_BYTES_PER_REGION;
    const qint64 budget = active_ ? active_budget : static_cast<qint64>(active_budget * INACTIVE_CACHE_FRACTION);
    return static_cast<int>(std::clamp<qint64>(budget, 64 * 1024 * 1024, std::numeric_limits<int>::max()));
}

int RegionCacheManager::heightMapCacheLimit() const {
    const int configured = std::max(1, setting::current().heightMapCacheSize);
    return active_ ? configured : std::clamp(static_cast<int>(configured * INACTIVE_CACHE_FRACTION), 1024, 32768);
}

int RegionCacheManager::slimeCacheLimit() const { return active_ ? 8192 : 512; }

std::optional<std::array<int16_t, 256>> RegionCacheManager::heightMap(const bl::chunk_pos& pos) {
    QMutexLocker lock(&height_map_mutex_);
    auto* cached = height_map_cache_->object(pos);
    if (cached) return *cached;
    return std::nullopt;
}

void RegionCacheManager::putHeightMap(const bl::chunk_pos& pos, const std::array<int16_t, 256>& heightMap) {
    QMutexLocker lock(&height_map_mutex_);
    if (!height_map_cache_->object(pos)) height_map_cache_->insert(pos, new std::array<int16_t, 256>(heightMap));
}

void RegionCacheManager::removeHeightMap(const bl::chunk_pos& pos) {
    QMutexLocker lock(&height_map_mutex_);
    height_map_cache_->remove(pos);
}

QImage RegionCacheManager::slimeChunkImage(const region_pos& pos) {
    assertOwnerThread();
    if (pos.dim != 0) return {};
    auto* image = slime_chunk_cache_->operator[](pos);
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
    result.emplace_back(QString("Region cache: %1 entries, %2/%3 MiB")
                            .arg(region_cache_.size())
                            .arg(region_cache_.totalCost() / (1024.0 * 1024.0), 0, 'f', 1)
                            .arg(region_cache_.maxCost() / (1024.0 * 1024.0), 0, 'f', 1));
    const quint64 lookups = cache_hits_ + cache_misses_;
    const double hit_rate = lookups ? 100.0 * static_cast<double>(cache_hits_) / static_cast<double>(lookups) : 0.0;
    result.emplace_back(
        QString("Region cache lookups: hit %1/%2 (%3%), inserts %4").arg(cache_hits_).arg(lookups).arg(hit_rate, 0, 'f', 1).arg(inserts_));
    result.emplace_back(QString("Null region cache: %1/%2 entries").arg(invalid_cache_.size()).arg(invalid_cache_.maxCost()));
    result.push_back(QString("Slime Chunk cache: %1/%2")
                         .arg(QString::number(slime_chunk_cache_->totalCost()), QString::number(slime_chunk_cache_->maxCost())));
    return result;
}
