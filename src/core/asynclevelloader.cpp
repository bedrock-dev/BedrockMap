#include "asynclevelloader.h"

#include <qcontainerfwd.h>
#include <qglobal.h>
#include <qimage.h>
#include <qobject.h>

#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QtConcurrent>
#include <optional>
#include <string>
#include <vector>

#include "bedrock_key.h"
#include "chunk.h"
#include "chunk_task.h"
#include "chunkio.h"
#include "config.h"
#include "data_3d.h"
#include "leveldb/write_batch.h"
#include "loguru/loguru.hpp"
#include "maptile.h"

AsyncLevelLoader::AsyncLevelLoader()
    : region_scheduler_(this),
      edit_service_(
          storage_, cache_manager_, chunk_coords_service_, loaded_, preload_all_chunk_coords_, [this]() { emit dirtyChanged(); },
          [this]() {
              if (loaded_.load(std::memory_order_acquire)) emit regionReady();
          }) {
    connect(
        &region_scheduler_, &RegionRenderScheduler::regionFinished, this,
        [this](region_pos pos, ChunkRegion *region, long long loadTime, long long renderTime) {
            if (!region || !region->valid) {
                cache_manager_.insertEmpty(pos);
                delete region;
            } else {
                region_load_timer_.push(loadTime);
                region_render_timer_.push(renderTime);
                cache_manager_.insertRegion(pos, region);
            }
            emit regionReady();
        },
        Qt::DirectConnection);
}

ChunkRegion *AsyncLevelLoader::tryGetRegion(const region_pos &p, bool &empty) {
    empty = false;
    if (!this->loaded_) return nullptr;
    auto *region = cache_manager_.findRegion(p, empty);
    if (empty) return nullptr;
    if (region) return region;
    if (region_scheduler_.contains(p)) return nullptr;
    region_scheduler_.request(p, map_filter_);
    return nullptr;
}

ChunkRegion *AsyncLevelLoader::peekRegion(const region_pos &p, bool &empty) {
    empty = false;
    if (!this->loaded_) return nullptr;
    return cache_manager_.peekRegion(p, empty);
}

bool AsyncLevelLoader::open(const std::string &path) {
    this->loaded_ = this->storage_.open(path);
    if (this->loaded_) {
        if (this->preload_all_chunk_coords_) {
            LOG_F(INFO, "Start loading all chunk cooords");
            chunk_coords_service_.start(storage_.level().db(), true, [this]() {
                if (loaded_) emit regionReady();
            });
        }
    }
    return this->loaded_;
}

AsyncLevelLoader::~AsyncLevelLoader() { this->close(); }

void AsyncLevelLoader::close() {
    // Keep the expensive shutdown on the caller's worker thread. UI-owned
    // caches are cleared by the GUI close-completion handler before deletion.
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

bl::chunk *AsyncLevelLoader::getChunk(const bl::chunk_pos &p, bl::chunk_load_policy policy) { return edit_service_.getChunk(p, policy); }

std::optional<bl::raw_chunk> AsyncLevelLoader::getRawChunk(const bl::chunk_pos &p) { return edit_service_.getRawChunk(p); }

bool AsyncLevelLoader::deleteChunk(const bl::chunk_pos &p) { return edit_service_.deleteChunk(p); }

bool AsyncLevelLoader::putRawChunk(const bl::raw_chunk &raw) { return edit_service_.putRawChunk(raw); }

void AsyncLevelLoader::setRenderViewport(const region_pos &minRegion, const region_pos &maxRegion) {
    if (!loaded_.load(std::memory_order_acquire)) return;
    region_scheduler_.setViewport(minRegion, maxRegion);
}

bool AsyncLevelLoader::createVoid(const bl::chunk_pos &p) { return edit_service_.createVoid(p); }

bool AsyncLevelLoader::setRawChunkBiome(const bl::chunk_pos &p, bl::biome biome) { return edit_service_.setRawChunkBiome(p, biome); }

void AsyncLevelLoader::clearChunkCache(const bl::chunk_pos &p) { edit_service_.clearChunkCache(p); }

bool AsyncLevelLoader::commit() { return edit_service_.commit(); }

bool AsyncLevelLoader::commitEdits(const std::unordered_map<std::string, std::string> &globalModifies,
                                   const bl::nbt::compound_tag *levelDat) {
    return edit_service_.commitEdits(globalModifies, levelDat);
}

void AsyncLevelLoader::clearAllCache() {
    LOG_F(INFO, "Clear cache");
    cache_manager_.clear();
}

std::optional<std::array<int16_t, 256>> AsyncLevelLoader::getHeightMap(const bl::chunk_pos &pos) {
    // Render workers may still be unwinding while close() is initiated. The
    // scheduler waits for those workers before closing LevelDB, but callers
    // that arrive after the close barrier must not start a new read.
    if (!loaded_.load(std::memory_order_acquire)) return std::nullopt;
    if (auto cached = cache_manager_.heightMap(pos)) return cached;

    if (!loaded_.load(std::memory_order_acquire)) return std::nullopt;

    // Cache miss — load from LevelDB (Data3D preferred, Data2D fallback)
    std::string raw;
    bl::biome3d b3d;
    for (auto kt : {bl::chunk_key::Data3D, bl::chunk_key::Data2D}) {
        bl::chunk_key key{kt, pos};
        if (storage_.level().load_raw(key.to_raw(), raw) && !raw.empty()) {
            b3d.set_chunk_pos(pos);
            auto version = kt == bl::chunk_key::Data3D ? bl::New : bl::Old;
            b3d.set_version(version);
            bool ok = (kt == bl::chunk_key::Data3D) ? b3d.load_from_d3d(raw.data(), raw.size()) : b3d.load_from_d2d(raw.data(), raw.size());
            if (ok) {
                auto hm = b3d.height_map();
                // Apply the version-correct min_y baseline (same logic as biome3d::height()).
                // -128 is the void sentinel and must stay unchanged.
                auto [miny, maxy] = pos.get_y_range(version);
                for (auto &h : hm)
                    if (h != -128) h += miny;
                cache_manager_.putHeightMap(pos, hm);
                return hm;
            }
        }
    }
    return std::nullopt;
}

void AsyncLevelLoader::putHeightMap(const bl::chunk_pos &pos, const std::array<int16_t, 256> &hm) { cache_manager_.putHeightMap(pos, hm); }

void AsyncLevelLoader::loadGlobalData(GlobalNBTLoadResult &result, std::atomic_bool &stop) {
    static const std::vector<std::string> others_keys{"portals",   "scoreboard", "AutonomousEntities", "BiomeData", "Nether",
                                                      "Overworld", "TheEnd",     "schedulerWT",        "mobevents"};
    std::string value;
    LOG_F(INFO, "Loading Global Data(Others)");
    for (auto &key : others_keys) {
        if (storage_.level().load_raw(key, value)) {
            if (stop) break;
            result.otherData.append_nbt(key, value);
        }
    }

    LOG_F(INFO, "Loading Global Data(Village)");
    storage_.level().foreach_key_with_prefix(
        "VILLAGE_",
        [&result, &stop](const auto &key, const auto &value) {
            auto vk = bl::village_key::parse(key);
            if (vk.valid()) result.villageData.append_village(vk, value);
        },
        stop, setting::current().MAX_GLOBAL_DATA_LOAD_COUNT);

    LOG_F(INFO, "Loading Global Data(Map)");
    storage_.level().foreach_key_with_prefix(
        "map_", [&result, &stop](const auto &key, const auto &value) { result.mapData.append_nbt(key, value); }, stop,
        setting::current().MAX_GLOBAL_DATA_LOAD_COUNT);

    LOG_F(INFO, "Loading Global Data(Player)");

    std::string local_player;
    if (storage_.level().load_raw("~local_player", local_player)) {
        result.playerData.append_nbt("~local_player", local_player);
    }
    storage_.level().foreach_key_with_prefix(
        "player_",
        [&result, &stop](const std::string &key, const std::string &value) {
            // 43:key with player_20f2a225-0aaa-3c7d-9e2d-c57b7ec01be5
            // 50:key with player_server_20f2a225-0aaa-3c7d-9e2d-c57b7ec01be5
            if (key.size() != 43 && key.size() != 50) return;
            result.playerData.append_nbt(key, value);
        },
        stop, setting::current().MAX_GLOBAL_DATA_LOAD_COUNT);
}

std::vector<QString> AsyncLevelLoader::debugInfo() {
    std::vector<QString> res;
    res.emplace_back("Region cache:");
    auto cacheInfo = cache_manager_.debugInfo();
    res.insert(res.end(), cacheInfo.begin(), cacheInfo.end());

    res.emplace_back("Background thread pool:");
    res.push_back(QString(" - Total threads: %1").arg(QString::number(setting::current().THREAD_NUM)));

    res.emplace_back("Chunk Modify Cache");
    auto [e, ne] = edit_service_.chunkModifyCounts();
    res.push_back(QString(" - Modified: %1").arg(QString::number(ne)));
    res.push_back(QString(" - Delete: %1").arg(QString::number(e)));

#ifdef QT_DEBUG
    res.push_back(QString(" - Region tasks pending/active: %1/%2")
                      .arg(QString::number(region_scheduler_.pendingCount()), QString::number(region_scheduler_.activeCount())));
    res.push_back(
        QString("Region timer: %1 ms:")
            .arg(QString::number(static_cast<double>(this->region_render_timer_.mean() + this->region_load_timer_.mean()) / 1000.0)));
    res.push_back(QString(" - Region Load: %1 ms").arg(QString::number(static_cast<double>(this->region_load_timer_.mean()) / 1000.0)));
    res.push_back(QString(" - Region Render: %1 ms").arg(QString::number(static_cast<double>(this->region_render_timer_.mean()) / 1000.0)));
#endif
    return res;
}

QImage AsyncLevelLoader::bakedTerrainImage(const region_pos &rp) {
    if (!this->loaded_) return MapTile::UNLOADED_REGION_TILE();
    bool null_region{false};
    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region) return MapTile::NULL_REGION_TILE();
    return region ? region->terrain_bake_image_ : MapTile::UNLOADED_REGION_TILE();
}

QImage AsyncLevelLoader::bakedBiomeImage(const region_pos &rp) {
    if (!this->loaded_) return MapTile::UNLOADED_REGION_TILE();
    bool null_region{false};

    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region) return MapTile::NULL_REGION_TILE();
    return region ? region->biome_bake_image_ : MapTile::UNLOADED_REGION_TILE();
}

QImage AsyncLevelLoader::chunkCoordsImage(const region_pos &rp) const {
    if (!this->loaded_) return MapTile::UNLOADED_REGION_TILE();
    if (!chunkCoordsReady()) return MapTile::COORDS_LOADING_TILE();
    auto image = chunk_coords_service_.image(rp);
    return image.isNull() ? MapTile::COORDS_EMPTY_TILE() : image;
}

std::optional<ChunkCoordsBoundingBox> AsyncLevelLoader::chunkCoordsBoundingBox(int dim) const {
    if (!this->loaded_ || !chunkCoordsReady()) return std::nullopt;
    return chunk_coords_service_.boundingBox(dim);
}

std::unordered_map<QImage *, std::vector<bl::vec3>> AsyncLevelLoader::getActorList(const region_pos &rp) {
    if (!this->loaded_) return {};
    bool null_region{false};
    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region || (!region)) return {};
    return region->actors_;
}

std::map<bl::chunk_pos, std::map<QImage *, ChunkRegion::ActorCount>> AsyncLevelLoader::getActorCountList(const region_pos &rp) {
    if (!this->loaded_) return {};
    bool null_region{false};
    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region || (!region)) return {};
    return region->actors_counts_;
}

std::vector<bl::hardcoded_spawn_area> AsyncLevelLoader::getHSAs(const region_pos &rp) {
    if (!this->loaded_) return {};
    bool null_region{false};
    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region || (!region)) return {};
    return region->HSAs_;
}

BlockTipsInfo AsyncLevelLoader::getBlockTips(const bl::block_pos &p, int dim) {
    if (!this->loaded_) return {};
    auto cp = p.to_chunk_pos();
    cp.dim = dim;
    auto rp = constant::c2r(cp);
    bool null_region{false};
    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region || (!region)) return {};
    auto &info = region->tips_info_;
    auto min_block_pos = rp.get_min_pos(bl::ChunkVersion::New);
    return region->tips_info_[p.x - min_block_pos.x][p.z - min_block_pos.z];
}

std::string AsyncLevelLoader::getBlockName(const bl::block_pos &p, int dim) {
    if (!this->loaded_) return {};
    auto cp = p.to_chunk_pos();
    cp.dim = dim;
    auto rp = constant::c2r(cp);
    bool null_region{false};
    // only query already-cached regions; an unloaded region stays unloaded
    auto *region = this->peekRegion(rp, null_region);
    if (null_region || (!region)) return {};
    auto min_block_pos = rp.get_min_pos(bl::ChunkVersion::New);
    const auto &info = region->tips_info_[p.x - min_block_pos.x][p.z - min_block_pos.z];
    return region->blockName(info.block_id);
}

QImage AsyncLevelLoader::bakedSlimeChunkImage(const region_pos &rp) {
    // slime chunks only exist in the overworld
    if (rp.dim != 0) return {};
    return cache_manager_.slimeChunkImage(rp);
}
