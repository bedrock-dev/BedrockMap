#include "asynclevelloader.h"

#include <qcache.h>
#include <qcolor.h>
#include <qcontainerfwd.h>
#include <qglobal.h>
#include <qimage.h>
#include <qobject.h>
#include <qvector3d.h>

#include <QObject>
#include <QVector3D>
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

AsyncLevelLoader::AsyncLevelLoader() {
    this->pool_.setMaxThreadCount(setting::current().THREAD_NUM);
    for (int dim : {0, 1, 2}) {
        this->region_cache_[dim] = std::make_unique<QCache<region_pos, ChunkRegion>>(setting::current().REGION_CACHE_SIZE);
        this->invalid_cache_[dim] = std::make_unique<QCache<region_pos, char>>(setting::current().EMPTY_REGION_CACHE_SIZE);
    }
    this->slime_chunk_cache_ = std::make_unique<QCache<region_pos, QImage>>(8192);
    this->height_map_cache_ = std::make_unique<QCache<bl::chunk_pos, std::array<int16_t, 256>>>(setting::current().HEIGHT_MAP_CACHE_SIZE);
}

ChunkRegion *AsyncLevelLoader::tryGetRegion(const region_pos &p, bool &empty) {
    empty = false;
    if (!this->loaded_) return nullptr;
    auto *invalid = ensureDimCache(invalid_cache_, p.dim, setting::current().EMPTY_REGION_CACHE_SIZE)->operator[](p);
    if (invalid) {
        empty = true;
        return nullptr;
    }
    // chunk cache
    auto *region = ensureDimCache(region_cache_, p.dim, setting::current().REGION_CACHE_SIZE)->operator[](p);
    if (region) return region;
    // not in cache but in queue
    if (this->processing_.contains(p)) return nullptr;
    auto *task = new LoadRegionTask(this, p, this->map_filter_);
    connect(task, &LoadRegionTask::finish, this,
            [this](int x, int z, int dim, ChunkRegion *region, long long load_time, long long render_time, bl::chunk **chunks) {
                if (!region || (!region->valid)) {
                    ensureDimCache(invalid_cache_, dim, setting::current().EMPTY_REGION_CACHE_SIZE)
                        ->insert(bl::chunk_pos(x, z, dim), new char(0));
                    delete region;
                } else {
                    this->region_load_timer_.push(load_time);
                    this->region_render_timer_.push(render_time);
                    ensureDimCache(region_cache_, dim, setting::current().REGION_CACHE_SIZE)->insert(bl::chunk_pos(x, z, dim), region);
                }
                this->processing_.remove(bl::chunk_pos(x, z, dim));
                emit regionReady();
            });
    this->processing_.add(p);
    this->pool_.start(task);
    return nullptr;
}

ChunkRegion *AsyncLevelLoader::peekRegion(const region_pos &p, bool &empty) {
    empty = false;
    if (!this->loaded_) return nullptr;
    auto *invalid = ensureDimCache(invalid_cache_, p.dim, setting::current().EMPTY_REGION_CACHE_SIZE)->operator[](p);
    if (invalid) {
        empty = true;
        return nullptr;
    }
    return ensureDimCache(region_cache_, p.dim, setting::current().REGION_CACHE_SIZE)->operator[](p);
}

bool AsyncLevelLoader::open(const std::string &path) {
    this->loaded_ = this->level_.open(path);
    if (this->loaded_) {
        this->stop_chunk_coords_preload_ = false;
        this->chunk_coords_ready_ = false;
        this->chunk_coords_.clear();
        if (this->preload_all_chunk_coords_) {
            LOG_F(INFO, "Start loading all chunk cooords");
            this->pool_.start(QRunnable::create([this]() {
                if (this->chunk_coords_.load(this->level_.db(), this->stop_chunk_coords_preload_)) {
                    this->chunk_coords_ready_.store(true, std::memory_order_release);
                    emit regionReady();
                }
            }));
        }
    }
    return this->loaded_;
}

AsyncLevelLoader::~AsyncLevelLoader() { this->close(); }

void AsyncLevelLoader::close() {
    if (!this->loaded_) return;
    LOG_F(INFO, "Try close level");
    this->loaded_ = false;  // blocking UI request
    this->stop_chunk_coords_preload_ = true;
    this->processing_.clear();  // clear queue
    this->pool_.clear();        // clear and wait for done
    this->pool_.waitForDone();
    this->level_.close();  // close the level
    this->chunk_coords_ready_ = false;
    this->chunk_coords_.clear();
    this->clearAllCache();
}

bl::chunk *AsyncLevelLoader::getChunk(const bl::chunk_pos &p, bl::chunk_load_policy policy) {
    if (!this->loaded_) return nullptr;
    if (this->level_cache_.hasChunk(p)) {
        return this->level_cache_.getChunk(p, policy);
    }
    return this->level_.get_chunk(p, policy);
}

std::optional<bl::raw_chunk> AsyncLevelLoader::getRawChunk(const bl::chunk_pos &p) {
    if (!this->loaded_) return std::nullopt;
    if (this->level_cache_.hasChunk(p)) return this->level_cache_.getRawChunk(p);
    bl::raw_chunk rc(p);
    if (rc.read(level_)) return rc;
    return std::nullopt;
}

bool AsyncLevelLoader::deleteChunk(const bl::chunk_pos &p) {
    if (!this->loaded_ || chunkCoordsLoading()) return false;
    this->level_cache_.putMissing(level_, p);
    clearChunkCache(p);
    updateChunkCoords(p, false);
    emit dirtyChanged();
    return true;
}

bool AsyncLevelLoader::putRawChunk(const bl::raw_chunk &raw) {
    LOG_F(INFO, "put raw chunk %s", raw.pos().to_string().c_str());
    if (!this->loaded_ || chunkCoordsLoading()) return false;
    this->level_cache_.putChunk(raw.pos(), raw);
    clearChunkCache(raw.pos());
    updateChunkCoords(raw.pos(), true);
    emit dirtyChanged();
    return true;
}

void AsyncLevelLoader::updateChunkCoords(const bl::chunk_pos &pos, bool present) {
    if (!preload_all_chunk_coords_ || !chunkCoordsReady()) return;
    chunk_coords_.enqueueUpdate(pos, present, [this]() { emit regionReady(); });
}

bool AsyncLevelLoader::createVoid(const bl::chunk_pos &p) {
    if (!this->loaded_ || chunkCoordsLoading()) return false;
    auto raw = getRawChunk(p);
    if (raw.has_value()) {
        raw->clear_terrain();
        putRawChunk(raw.value());
    } else {
        // TODO: create new
    }
    return true;
}

bool AsyncLevelLoader::setRawChunkBiome(const bl::chunk_pos &p, bl::biome biome) {
    if (!this->loaded_ || chunkCoordsLoading()) return false;
    auto raw = getRawChunk(p);
    if (!raw.has_value()) return false;
    raw->set_biome(biome);
    return putRawChunk(raw.value());
}

void AsyncLevelLoader::clearChunkCache(const bl::chunk_pos &p) {
    auto rp = constant::c2r(p);
    auto it = region_cache_.find(rp.dim);
    if (it != region_cache_.end()) it->second->remove(rp);
    auto it2 = invalid_cache_.find(rp.dim);
    if (it2 != invalid_cache_.end()) it2->second->remove(rp);
}

bool AsyncLevelLoader::commit() {
    LOG_F(INFO, "Commit chunks change");
    if (!this->loaded_ || chunkCoordsLoading() || this->level_cache_.empty()) return true;
    leveldb::WriteBatch batch;
    this->level_cache_.commit(batch);
    auto s = this->level_.db()->Write(leveldb::WriteOptions(), &batch);
    if (!s.ok()) {
        LOG_F(ERROR, "Failed to commit chunk changes: %s", s.ToString().c_str());
        return false;
    }
    this->level_cache_.clear();
    emit dirtyChanged();
    return true;
}

void AsyncLevelLoader::clearAllCache() {
    LOG_F(INFO, "Clear cache");
    for (auto &[dim, cache] : region_cache_) cache->clear();
    for (auto &[dim, cache] : invalid_cache_) cache->clear();
    this->slime_chunk_cache_->clear();
    {
        QMutexLocker lock(&height_map_mutex_);
        height_map_cache_->clear();
    }
}

std::optional<std::array<int16_t, 256>> AsyncLevelLoader::getHeightMap(const bl::chunk_pos &pos) {
    {
        QMutexLocker lock(&height_map_mutex_);
        auto *cached = height_map_cache_->object(pos);
        if (cached) return *cached;
    }

    // Cache miss — load from LevelDB (Data3D preferred, Data2D fallback)
    std::string raw;
    bl::biome3d b3d;
    for (auto kt : {bl::chunk_key::Data3D, bl::chunk_key::Data2D}) {
        bl::chunk_key key{kt, pos};
        if (level_.load_raw(key.to_raw(), raw) && !raw.empty()) {
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
                QMutexLocker lock(&height_map_mutex_);
                height_map_cache_->insert(pos, new std::array<int16_t, 256>(hm));
                return hm;
            }
        }
    }
    return std::nullopt;
}

void AsyncLevelLoader::putHeightMap(const bl::chunk_pos &pos, const std::array<int16_t, 256> &hm) {
    QMutexLocker lock(&height_map_mutex_);
    // Only insert if not already cached (cheaper than unconditional replace)
    if (!height_map_cache_->object(pos)) {
        height_map_cache_->insert(pos, new std::array<int16_t, 256>(hm));
    }
}

void AsyncLevelLoader::loadGlobalData(GlobalNBTLoadResult &result, std::atomic_bool &stop) {
    static const std::vector<std::string> others_keys{"portals",   "scoreboard", "AutonomousEntities", "BiomeData", "Nether",
                                                      "Overworld", "TheEnd",     "schedulerWT",        "mobevents"};
    std::string value;
    LOG_F(INFO, "Loading Global Data(Others)");
    for (auto &key : others_keys) {
        if (level_.load_raw(key, value)) {
            if (stop) break;
            result.otherData.append_nbt(key, value);
        }
    }

    LOG_F(INFO, "Loading Global Data(Village)");
    level_.foreach_key_with_prefix(
        "VILLAGE_",
        [&result, &stop](const auto &key, const auto &value) {
            auto vk = bl::village_key::parse(key);
            if (vk.valid()) result.villageData.append_village(vk, value);
        },
        stop, setting::current().MAX_GLOBAL_DATA_LOAD_COUNT);

    LOG_F(INFO, "Loading Global Data(Map)");
    level_.foreach_key_with_prefix(
        "map_", [&result, &stop](const auto &key, const auto &value) { result.mapData.append_nbt(key, value); }, stop,
        setting::current().MAX_GLOBAL_DATA_LOAD_COUNT);

    LOG_F(INFO, "Loading Global Data(Player)");

    std::string local_player;
    if (level_.load_raw("~local_player", local_player)) {
        result.playerData.append_nbt("~local_player", local_player);
    }
    level_.foreach_key_with_prefix(
        "player_",
        [&result, &stop](const std::string &key, const std::string &value) {
            // 43:key with player_20f2a225-0aaa-3c7d-9e2d-c57b7ec01be5
            // 50:key with player_server_20f2a225-0aaa-3c7d-9e2d-c57b7ec01be5
            if (key.size() != 43 && key.size() != 50) return;
            result.playerData.append_nbt(key, value);
        },
        stop, setting::current().MAX_GLOBAL_DATA_LOAD_COUNT);
}

bool AsyncLevelLoader::modifyLeveldat(bl::nbt::compound_tag *nbt) {
    if (!this->loaded_ || chunkCoordsLoading()) return false;
    level_.dat().set_nbt(nbt);
    auto raw = level_.dat().to_raw();
    bl::utils::write_file(level_.root_path() + "/" + bl::bedrock_level::LEVEL_DATA, raw.data(), raw.size());
    return true;
}

bool AsyncLevelLoader::modifyDBGlobal(const std::unordered_map<std::string, std::string> &modifies) {
    if (!this->loaded_ || chunkCoordsLoading()) return false;
    leveldb::WriteBatch batch;
    for (auto &kv : modifies) {
        if (kv.second.empty()) {
            LOG_F(INFO, "Delete key: %s", kv.first.c_str());
            batch.Delete(kv.first);
        } else {
            batch.Put(kv.first, kv.second);
            LOG_F(INFO, "Put key: %s", kv.first.c_str());
        }
    }
    auto s = this->level_.db()->Write(leveldb::WriteOptions(), &batch);
    if (!s.ok()) {
        LOG_F(ERROR, "Failed to modify global data: %s", s.ToString().c_str());
        return false;
    }
    return true;
}

std::vector<QString> AsyncLevelLoader::debugInfo() {
    std::vector<QString> res;
    res.emplace_back("Region cache:");
    for (auto &[dim, cache] : region_cache_) {
        res.push_back(
            QString(" - [%1]: %2/%3").arg(QString::number(dim), QString::number(cache->totalCost()), QString::number(cache->maxCost())));
    }
    res.emplace_back("Null region cache:");
    for (auto &[dim, cache] : invalid_cache_) {
        res.push_back(
            QString(" - [%1]: %2/%3").arg(QString::number(dim), QString::number(cache->totalCost()), QString::number(cache->maxCost())));
    }
    res.push_back(QString("Slime Chunk cache: %2/%3")
                      .arg(QString::number(this->slime_chunk_cache_->totalCost()), QString::number(this->slime_chunk_cache_->maxCost())));

    res.emplace_back("Background thread pool:");
    res.push_back(QString(" - Total threads: %1").arg(QString::number(setting::current().THREAD_NUM)));

    res.emplace_back("Chunk Modify Cache");
    auto [e, ne] = level_cache_.chunkCounts();
    res.push_back(QString(" - Modified: %1").arg(QString::number(ne)));
    res.push_back(QString(" - Delete: %1").arg(QString::number(e)));

#ifdef QT_DEBUG
    res.push_back(QString(" - Background tasks %1").arg(QString::number(this->processing_.size())));
    res.push_back(
        QString("Region timer: %1 ms:")
            .arg(QString::number(static_cast<double>(this->region_render_timer_.mean() + this->region_load_timer_.mean()) / 1000.0)));
    res.push_back(QString(" - Region Load: %1 ms").arg(QString::number(static_cast<double>(this->region_load_timer_.mean()) / 1000.0)));
    res.push_back(QString(" - Region Render: %1 ms").arg(QString::number(static_cast<double>(this->region_render_timer_.mean()) / 1000.0)));
#endif
    return res;
}

QImage *AsyncLevelLoader::bakedTerrainImage(const region_pos &rp) {
    if (!this->loaded_) return &MapTile::UNLOADED_REGION_TILE();
    bool null_region{false};
    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region) return &MapTile::NULL_REGION_TILE();
    return region ? &region->terrain_bake_image_ : &MapTile::UNLOADED_REGION_TILE();
}

QImage *AsyncLevelLoader::bakedBiomeImage(const region_pos &rp) {
    if (!this->loaded_) return &MapTile::UNLOADED_REGION_TILE();
    bool null_region{false};

    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region) return &MapTile::NULL_REGION_TILE();
    return region ? &region->biome_bake_image_ : &MapTile::UNLOADED_REGION_TILE();
}

const QImage *AsyncLevelLoader::chunkCoordsImage(const region_pos &rp) const {
    if (!this->loaded_) return &MapTile::UNLOADED_REGION_TILE();
    if (!this->chunk_coords_ready_.load(std::memory_order_acquire)) return &MapTile::COORDS_LOADING_TILE();
    if (const auto *image = this->chunk_coords_.image(rp)) return image;
    return &MapTile::COORDS_EMPTY_TILE();
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

QImage *AsyncLevelLoader::bakedSlimeChunkImage(const region_pos &rp) {
    // slime chunks only exist in the overworld
    if (rp.dim != 0) return nullptr;
    auto *img = this->slime_chunk_cache_->operator[](rp);
    if (img) {
        return img;
    }
    auto *res = new QImage(constant::RW << 4, constant::RW << 4, QImage::Format_Indexed8);
    res->setColor(0, qRgba(0, 0, 0, 0));
    res->setColor(1, qRgba(29, 145, 44, 190));
    for (int rw = 0; rw < constant::RW; rw++) {
        for (int rh = 0; rh < constant::RW; rh++) {
            bl::chunk_pos cp(rp.x + rw, rp.z + rh, rp.dim);
            auto color = cp.is_slime() ? 1 : 0;
            for (int i = 0; i < 16; i++) {
                for (int j = 0; j < 16; j++) {
                    res->setPixel((rw << 4) + i, (rh << 4) + j, color);
                }
            }
        }
    }
    this->slime_chunk_cache_->insert(rp, res);
    return res;
}
