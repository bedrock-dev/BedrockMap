#include "loader/asynclevelloader.h"

#include <qcontainerfwd.h>
#include <qglobal.h>
#include <qimage.h>
#include <qobject.h>

#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QtConcurrent>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/config.h"
#include "bedrock_key.h"
#include "chunk.h"
#include "data_3d.h"
#include "loader/chunkregion.h"
#include "loader/regionbake.h"
#include "loguru/loguru.hpp"

AsyncLevelLoader::AsyncLevelLoader()
    : region_scheduler_(this), edit_service_(storage_, cache_manager_, chunk_coords_service_, loaded_, preload_all_chunk_coords_) {
    connect(&chunk_coords_service_, &ChunkCoordsService::preloadProgress, this, &AsyncLevelLoader::chunkCoordsPreloadProgress,
            Qt::QueuedConnection);
    connect(&chunk_coords_service_, &ChunkCoordsService::preloadFinished, this, &AsyncLevelLoader::chunkCoordsPreloadFinished,
            Qt::QueuedConnection);
    connect(
        &region_scheduler_, &RegionRenderScheduler::regionFinished, this,
        [this](region_pos pos, ChunkRegion* region, long long loadTime, long long renderTime, int tileBlocksPerTexel, int tileLayer,
               const QImage& color, const QImage& height, const QImage& material) {
            std::shared_ptr<ChunkRegion> regionOwner(region);
            if (!region || !region->valid) {
                cache_manager_.insertEmpty(pos);
                regionOwner.reset();
            } else {
                region_load_timer_.push(loadTime);
                region_render_timer_.push(renderTime);
                regionOwner->revision = ++next_region_revision_;
                cache_manager_.insertRegion(pos, std::move(regionOwner));
            }
            if (!color.isNull() && !height.isNull() && !material.isNull()) {
                emit regionSnapshotReady(pos, tileBlocksPerTexel, tileLayer, color, height, material);
            }
            emit regionTileReady(pos);
            requestRefresh();
        },
        Qt::DirectConnection);
}

ChunkRegion* AsyncLevelLoader::tryGetRegion(const region_pos& p, bool& empty) {
    empty = false;
    if (!this->loaded_) return nullptr;
    auto* region = cache_manager_.findRegion(p, empty);
    if (empty) return nullptr;
    if (region) return region;
    // Skip known-empty regions before scheduling a bake.
    if (isRegionAbsent(p)) {
        cache_manager_.insertEmpty(p);
        empty = true;
        return nullptr;
    }
    if (region_scheduler_.contains(p)) return nullptr;
    region_scheduler_.request(p, map_filter_, pending_tile_request_);
    return nullptr;
}

ChunkRegion* AsyncLevelLoader::peekRegion(const region_pos& p, bool& empty) {
    empty = false;
    if (!this->loaded_) return nullptr;
    return cache_manager_.peekRegion(p, empty);
}

bool AsyncLevelLoader::open(const std::string& path, const std::string& xor_key) {
    this->loaded_ = this->storage_.open(path, xor_key);
    if (this->loaded_) {
        if (this->preload_all_chunk_coords_) {
            LOG_F(INFO, "Start loading all chunk cooords");
            chunk_coords_service_.start(
                storage_.level(), [this]() { QMetaObject::invokeMethod(this, [this]() { requestRefresh(); }, Qt::QueuedConnection); },
                chunk_overview_enabled_);
        }
    }
    return this->loaded_;
}

AsyncLevelLoader::~AsyncLevelLoader() { this->close(); }

void AsyncLevelLoader::close() {
    // Perform storage shutdown on the caller's worker thread.
    if (this->thread() != QThread::currentThread()) {
        if (!loaded_.exchange(false, std::memory_order_acq_rel)) return;
        LOG_F(INFO, "Try close level");
        region_scheduler_.clear();
        chunk_coords_service_.close();
        storage_.close();
        return;
    }
    closeImpl();
}

void AsyncLevelLoader::closeImpl() {
    if (!this->loaded_) return;
    LOG_F(INFO, "Try close level");
    this->loaded_ = false;
    region_scheduler_.clear();
    chunk_coords_service_.close();
    this->storage_.close();  // close the level
    this->clearAllCache();
}

bl::chunk* AsyncLevelLoader::getChunk(const bl::chunk_pos& p, bl::chunk_load_policy policy) { return edit_service_.getChunk(p, policy); }

std::optional<bl::raw_chunk> AsyncLevelLoader::getRawChunk(const bl::chunk_pos& p) { return edit_service_.getRawChunk(p); }

bool AsyncLevelLoader::deleteChunk(const bl::chunk_pos& p) {
    if (!edit_service_.deleteChunk(p)) return false;
    emit dirtyChanged();
    return true;
}

bool AsyncLevelLoader::putRawChunk(const bl::raw_chunk& raw) {
    if (!edit_service_.putRawChunk(raw)) return false;
    emit dirtyChanged();
    return true;
}

void AsyncLevelLoader::setRenderViewport(const region_pos& minRegion, const region_pos& maxRegion) {
    if (!loaded_.load(std::memory_order_acquire) || !render_active_) return;
    region_scheduler_.setViewport(minRegion, maxRegion);
}

void AsyncLevelLoader::setRenderActive(bool active) {
    if (render_active_ == active) return;
    render_active_ = active;
    cache_manager_.setActive(active);
    if (!loaded_.load(std::memory_order_acquire)) return;
    if (active) {
        region_scheduler_.resume();
    } else {
        region_scheduler_.pause();
    }
}

std::shared_ptr<const ChunkRegion> AsyncLevelLoader::regionLease(const region_pos& pos) {
    if (!loaded_.load(std::memory_order_acquire)) return {};
    bool empty = false;
    return cache_manager_.findRegionLease(pos, empty);
}

bool AsyncLevelLoader::createVoid(const bl::chunk_pos& p) {
    if (!edit_service_.createVoid(p)) return false;
    emit dirtyChanged();
    return true;
}

bool AsyncLevelLoader::setRawChunkBiome(const bl::chunk_pos& p, bl::biome biome) {
    if (!edit_service_.setRawChunkBiome(p, biome)) return false;
    emit dirtyChanged();
    return true;
}

void AsyncLevelLoader::requestRefresh() {
    // Drop late notifications once the level is being closed.
    if (!loaded_.load(std::memory_order_acquire)) return;
    emit regionReady();
}

void AsyncLevelLoader::invalidateRegionTiles(const std::vector<bl::chunk_pos>& chunks) {
    // Collapse the edited chunks onto the 8x8-chunk region tiles covering them.
    std::set<bl::chunk_pos> regions;
    for (const auto& c : chunks) regions.insert(constant::c2r(c));
    if (regions.empty()) return;

    // The index already reflects the edit; invalidate cached region tiles.
    const auto drop = [this, regions, chunks]() {
        for (const auto& r : regions) cache_manager_.removeRegion(r);
        // Edited terrain changes the surface heights the shadow pass reads.
        for (const auto& c : chunks) cache_manager_.removeHeightMap(c);
    };
    if (QThread::currentThread() == thread()) {
        drop();
    } else {
        // Edits run on a worker thread. Queue cache invalidation so a caller
        // waiting for that worker (for example while closing a QML session)
        // cannot deadlock the GUI event loop.
        QMetaObject::invokeMethod(this, drop, Qt::QueuedConnection);
    }
}

void AsyncLevelLoader::invalidateRegionTiles(const QRegion& chunkRegion, int dim) {
    std::vector<bl::chunk_pos> chunks;
    for (const auto& rect : chunkRegion) {
        for (int x = rect.x(); x <= rect.x() + rect.width() - 1; ++x) {
            for (int z = rect.y(); z <= rect.y() + rect.height() - 1; ++z) {
                chunks.emplace_back(x, z, dim);
            }
        }
    }
    invalidateRegionTiles(chunks);
}

void AsyncLevelLoader::clearChunkCache(const bl::chunk_pos& p) { edit_service_.clearChunkCache(p); }

bool AsyncLevelLoader::commit() {
    if (!edit_service_.commit()) return false;
    emit dirtyChanged();
    return true;
}

bool AsyncLevelLoader::commitEdits(const std::unordered_map<std::string, std::string>& globalModifies,
                                   const bl::nbt::compound_tag* levelDat) {
    if (!edit_service_.commitEdits(globalModifies, levelDat)) return false;
    emit dirtyChanged();
    return true;
}

void AsyncLevelLoader::clearAllCache() {
    LOG_F(INFO, "Clear cache");
    cache_manager_.clear();
}

std::optional<std::array<int16_t, 256>> AsyncLevelLoader::getHeightMap(const bl::chunk_pos& pos) {
    // Wait for workers before closing LevelDB; reject reads after the barrier.
    if (!loaded_.load(std::memory_order_acquire)) return std::nullopt;
    if (auto cached = cache_manager_.heightMap(pos)) return cached;

    if (!loaded_.load(std::memory_order_acquire)) return std::nullopt;

    // Cache miss — load from LevelDB (Data3D preferred, legacy Data2D fallback)
    std::string raw;
    bl::biome3d b3d;
    for (auto kt : {bl::chunk_key::Data3D, bl::chunk_key::Data2D, bl::chunk_key::Data2DLegacy}) {
        bl::chunk_key key{kt, pos};
        if (storage_.level().load_raw(key.to_raw(), raw) && !raw.empty()) {
            b3d.set_chunk_pos(pos);
            bool ok = (kt == bl::chunk_key::Data3D) ? b3d.load_from_d3d(raw.data(), raw.size()) : b3d.load_from_d2d(raw.data(), raw.size());
            if (ok) {
                std::array<int16_t, 256> hm{};
                std::copy(b3d.height_map().begin(), b3d.height_map().end(), hm.begin());
                // Data3D heights are relative to the dimension's minimum Y; preserve -128 as void.
                const int miny = kt == bl::chunk_key::Data3D ? bl::dimension_min_y(pos.dim) : 0;
                for (auto& h : hm)
                    if (h != -128) h += miny;
                cache_manager_.putHeightMap(pos, hm);
                return hm;
            }
        }
    }
    return std::nullopt;
}

void AsyncLevelLoader::putHeightMap(const bl::chunk_pos& pos, const std::array<int16_t, 256>& hm) { cache_manager_.putHeightMap(pos, hm); }

void AsyncLevelLoader::loadGlobalData(GlobalNBTLoadResult& result, std::atomic_bool& stop) {
    std::string value;
    LOG_F(INFO, "Loading Global Data(Others)");
    for (const auto key : bl::global_key::other_keys()) {
        const std::string key_string(key);
        if (storage_.level().load_raw(key_string, value)) {
            if (stop) break;
            result.otherData.append_nbt(key_string, value);
        }
    }

    LOG_F(INFO, "Loading Global Data(Portal)");
    if (storage_.level().load_raw("portals", value)) {
        result.portalData.append_nbt("portals", value);
    }

    LOG_F(INFO, "Loading Global Data(Village)");
    storage_.level().foreach_key_with_prefix(
        "VILLAGE_",
        [&result, &stop](const auto& key, const auto& value) {
            auto vk = bl::village_key::parse(key);
            if (vk.valid()) result.villageData.append_village(vk, value);
        },
        stop, setting::current().maxGlobalDataLoadCount);

    LOG_F(INFO, "Loading Global Data(Map)");
    storage_.level().foreach_key_with_prefix(
        "map_", [&result, &stop](const auto& key, const auto& value) { result.mapData.append_nbt(key, value); }, stop,
        setting::current().maxGlobalDataLoadCount);

    LOG_F(INFO, "Loading Global Data(Structures)");
    storage_.level().foreach_key_with_prefix(
        "structuretemplate_", [&result, &stop](const auto& key, const auto& value) { result.structuresData.append_nbt(key, value); }, stop,
        setting::current().maxGlobalDataLoadCount);

    LOG_F(INFO, "Loading Global Data(Player)");

    std::string local_player;
    if (storage_.level().load_raw("~local_player", local_player)) {
        result.playerData.append_nbt("~local_player", local_player);
    }
    storage_.level().foreach_key_with_prefix(
        "player_",
        [&result, &stop](std::string_view key, std::string_view value) {
            // 43:key with player_20f2a225-0aaa-3c7d-9e2d-c57b7ec01be5
            // 50:key with player_server_20f2a225-0aaa-3c7d-9e2d-c57b7ec01be5
            if (key.size() != 43 && key.size() != 50) return;
            result.playerData.append_nbt(key, value);
        },
        stop, setting::current().maxGlobalDataLoadCount);
}

std::vector<QString> AsyncLevelLoader::debugInfo() {
    std::vector<QString> res;
    res.emplace_back("Region cache:");
    auto cacheInfo = cache_manager_.debugInfo();
    res.insert(res.end(), cacheInfo.begin(), cacheInfo.end());

    res.emplace_back("Background thread pool:");
    res.push_back(QString(" - Total threads: %1").arg(QString::number(setting::current().threadCount)));

    res.emplace_back("Chunk Modify Cache");
    auto [e, ne] = edit_service_.chunkModifyCounts();
    res.push_back(QString(" - Modified: %1").arg(QString::number(ne)));
    res.push_back(QString(" - Delete: %1").arg(QString::number(e)));

    res.push_back(QString(" - Region tasks pending/active: %1/%2")
                      .arg(QString::number(region_scheduler_.pendingCount()), QString::number(region_scheduler_.activeCount())));
    res.push_back(
        QString("Region timer: %1 ms:")
            .arg(QString::number(static_cast<double>(this->region_render_timer_.mean() + this->region_load_timer_.mean()) / 1000.0)));
    res.push_back(QString(" - Region Load: %1 ms").arg(QString::number(static_cast<double>(this->region_load_timer_.mean()) / 1000.0)));
    res.push_back(QString(" - Region Render: %1 ms").arg(QString::number(static_cast<double>(this->region_render_timer_.mean()) / 1000.0)));
    return res;
}

AsyncLevelLoader::RegionState AsyncLevelLoader::regionState(const region_pos& rp, const ChunkRegion** region) {
    if (region) *region = nullptr;
    if (!this->loaded_) return RegionState::Unloaded;
    bool null_region{false};
    auto* cached = this->tryGetRegion(rp, null_region);
    if (null_region) return RegionState::Empty;  // no chunk key under this region at all
    // Cached but not drawable yet: still baking, so it is not known to be empty.
    if (!cached || !cached->valid) return RegionState::Unloaded;
    if (region) *region = cached;
    return RegionState::Ready;
}

int AsyncLevelLoader::pendingRegionTasks() const { return region_scheduler_.pendingCount() + region_scheduler_.activeCount(); }

bool AsyncLevelLoader::isChunkAbsent(const bl::chunk_pos& pos) const {
    // Only the completed scan makes the index safe for worker reads.
    if (!chunk_coords_service_.ready()) return false;
    // Pending edits are not in the index yet.
    if (storage_.hasPendingEdit(pos)) return false;
    return !chunk_coords_service_.index().contains(pos);
}

bool AsyncLevelLoader::isRegionAbsent(const region_pos& p) const {
    if (!chunk_coords_service_.ready()) return false;
    // Edit paths invalidate memoized empty regions before they are drawn again.
    return !chunk_coords_service_.index().containsAnyChunk(p.x, p.z, p.dim, constant::RW);
}

std::optional<ChunkCoordsBoundingBox> AsyncLevelLoader::chunkCoordsBoundingBox(int dim) const {
    if (!this->loaded_ || !chunkCoordsReady()) return std::nullopt;
    return chunk_coords_service_.boundingBox(dim);
}

std::unordered_map<QImage*, std::vector<bl::vec3>> AsyncLevelLoader::getActorList(const region_pos& rp) {
    if (!this->loaded_) return {};
    bool null_region{false};
    auto* region = this->tryGetRegion(rp, null_region);
    if (null_region || (!region)) return {};
    return region->actors_;
}

std::map<bl::chunk_pos, std::map<QImage*, ChunkRegion::ActorCount>> AsyncLevelLoader::getActorCountList(const region_pos& rp) {
    if (!this->loaded_) return {};
    bool null_region{false};
    auto* region = this->tryGetRegion(rp, null_region);
    if (null_region || (!region)) return {};
    return region->actors_counts_;
}

std::vector<bl::hardcoded_spawn_area> AsyncLevelLoader::getHSAs(const region_pos& rp) {
    if (!this->loaded_) return {};
    bool null_region{false};
    auto* region = this->tryGetRegion(rp, null_region);
    if (null_region || (!region)) return {};
    return region->HSAs_;
}

BlockTipsInfo AsyncLevelLoader::getBlockTips(const bl::block_pos& p, int dim) {
    if (!this->loaded_) return {};
    auto cp = p.to_chunk_pos();
    cp.dim = dim;
    auto rp = constant::c2r(cp);
    bool null_region{false};
    auto* region = this->tryGetRegion(rp, null_region);
    if (null_region || (!region)) return {};
    auto& info = region->tips_info_;
    return region->tips_info_[p.x - rp.x * 16][p.z - rp.z * 16];
}

std::string AsyncLevelLoader::getBlockName(const bl::block_pos& p, int dim) {
    if (!this->loaded_) return {};
    auto cp = p.to_chunk_pos();
    cp.dim = dim;
    auto rp = constant::c2r(cp);
    bool null_region{false};
    // only query already-cached regions; an unloaded region stays unloaded
    auto* region = this->peekRegion(rp, null_region);
    if (null_region || (!region)) return {};
    const auto& info = region->tips_info_[p.x - rp.x * 16][p.z - rp.z * 16];
    return region->blockName(info.block_id);
}

QImage AsyncLevelLoader::bakedSlimeChunkImage(const region_pos& rp) {
    // slime chunks only exist in the overworld
    if (rp.dim != 0) return {};
    return cache_manager_.slimeChunkImage(rp);
}
