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
#include "leveldb/write_batch.h"
#include "loguru/loguru.hpp"
#include "maptile.h"
#include "utils.h"

AsyncLevelLoader::AsyncLevelLoader() {
    this->pool_.setMaxThreadCount(cfg::THREAD_NUM);
    for (int i = 0; i < 3; i++) {
        this->region_cache_.push_back(new QCache<region_pos, ChunkRegion>(cfg::REGION_CACHE_SIZE));
        this->invalid_cache_.push_back(new QCache<region_pos, char>(cfg::EMPTY_REGION_CACHE_SIZE));
        this->thumbnails_cache_.push_back(new QCache<region_pos, QImage>(cfg::THUMBNAIL_REION_CACHE_SIZE));
    }
    this->slime_chunk_cache_ = new QCache<region_pos, QImage>(8192);
    /**
     * 不要相信bedrock_level的任何数据，不在库内做任何长期的缓存
     */
    this->level_.set_cache(false);
}

ChunkRegion *AsyncLevelLoader::tryGetRegion(const region_pos &p, bool &empty) {
    empty = false;
    if (!this->loaded_) return nullptr;
    auto *invalid = this->invalid_cache_[p.dim]->operator[](p);
    if (invalid) {
        empty = true;
        return nullptr;
    }
    // chunk cache
    auto *region = this->region_cache_[p.dim]->operator[](p);
    if (region) return region;
    // not in cache but in queue
    if (this->processing_.contains(p)) return nullptr;
    auto *task = new LoadRegionTask(this, p, &this->map_filter_);
    connect(task, &LoadRegionTask::finish, this,
            [this](int x, int z, int dim, ChunkRegion *region, long long load_time, long long render_time, bl::chunk **chunks) {
                this->region_load_timer_.push(load_time);
                this->region_render_timer_.push(render_time);
                if (!region || (!region->valid)) {
                    this->invalid_cache_[dim]->insert(bl::chunk_pos(x, z, dim), new char(0));
                    delete region;
                } else {
                    this->region_cache_[dim]->insert(bl::chunk_pos(x, z, dim), region);
                }
                this->processing_.remove(bl::chunk_pos(x, z, dim));
            });
    this->processing_.add(p);
    this->pool_.start(task);
    return nullptr;
}

QImage *AsyncLevelLoader::tryGetThumbnail(const region_pos &p) {
    auto *img = this->thumbnails_cache_[p.dim]->operator[](p);
    if (img) return img;
    if (this->thumbnail_processing_.contains(p)) return nullptr;
    auto *task = new LoadThumbnailTask(this, p);
    connect(task, &LoadThumbnailTask::finish, this, [this](int x, int z, int dim, QImage *img) {
        if (img) {
            this->thumbnails_cache_[dim]->insert(bl::chunk_pos(x, z, dim), img);
        }
        this->thumbnail_processing_.remove(bl::chunk_pos(x, z, dim));
    });
    this->thumbnail_processing_.add(p);
    this->pool_.start(task);
    return nullptr;
}

bool AsyncLevelLoader::open(const std::string &path) {
    this->level_.set_cache(false);
    this->loaded_ = this->level_.open(path);
    return this->loaded_;
}

AsyncLevelLoader::~AsyncLevelLoader() { this->close(); }

void AsyncLevelLoader::close() {
    if (!this->loaded_) return;
    LOG_F(INFO, "Try close level");
    this->loaded_ = false;      // 阻止UI层请求数据
    this->processing_.clear();  // 队列清除
    this->pool_.clear();        // 清除所有任务
    this->pool_.waitForDone();  // 等待当前任务完成
    LOG_F(INFO, "Clear work pool");
    this->level_.close();  // 关闭存档
    this->clearAllCache();
}

bl::chunk *AsyncLevelLoader::getChunk(const bl::chunk_pos &p) {
    if (!this->loaded_) return nullptr;
    if (this->level_cache_.hasChunk(p)) {
        return this->level_cache_.getChunk(p);
    }
    return this->level_.get_chunk(p, false);
}

std::optional<bl::raw_chunk> AsyncLevelLoader::getRawChunk(const bl::chunk_pos &p) {
    if (!this->loaded_) return std::nullopt;
    if (this->level_cache_.hasChunk(p)) return this->level_cache_.getRawChunk(p);
    bl::raw_chunk rc(p);
    rc.read(level_);
    return rc;
}

bool AsyncLevelLoader::deleteChunk(const bl::chunk_pos &p) {
    if (!this->loaded_) return false;
    this->level_cache_.putMissing(level_, p);
    clearChunkCache(p);
    emit dirtyChanged();
    return true;
}

bool AsyncLevelLoader::putRawChunk(const bl::raw_chunk &raw) {
    LOG_F(INFO, "put raw chunk %s", raw.pos().to_string().c_str());
    if (!this->loaded_) return false;
    this->level_cache_.putChunk(raw.pos(), raw);
    clearChunkCache(raw.pos());
    emit dirtyChanged();
    return true;
}

bool AsyncLevelLoader::createVoid(const bl::chunk_pos &p) {
    if (!this->loaded_) return false;
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
    if (!this->loaded_) return false;
    auto raw = getRawChunk(p);
    if (!raw.has_value()) return false;
    raw->set_biome(biome);
    return putRawChunk(raw.value());
}

void AsyncLevelLoader::clearChunkCache(const bl::chunk_pos &p) {
    auto rp = cfg::c2r(p);
    this->region_cache_[rp.dim]->remove(rp);
    this->invalid_cache_[rp.dim]->remove(rp);
    this->thumbnails_cache_[rp.dim]->remove(rp);
}

void AsyncLevelLoader::commit() {
    LOG_F(INFO, "Commit chunks change");
    if (!this->loaded_ || this->level_cache_.empty()) return;
    leveldb::WriteBatch batch;
    this->level_cache_.commit(batch);
    auto s = this->level_.db()->Write(leveldb::WriteOptions(), &batch);
    emit dirtyChanged();
}

void AsyncLevelLoader::clearAllCache() {
    LOG_F(INFO, "Clear cache");
    for (auto &cache : this->region_cache_) cache->clear();
    for (auto cache : this->invalid_cache_) cache->clear();
    for (auto cache : this->thumbnails_cache_) cache->clear();
    this->slime_chunk_cache_->clear();
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
        stop, cfg::MAX_GLOBAL_DATA_LOAD_COUNT);

    LOG_F(INFO, "Loading Global Data(Map)");
    level_.foreach_key_with_prefix(
        "map_", [&result, &stop](const auto &key, const auto &value) { result.mapData.append_nbt(key, value); }, stop,
        cfg::MAX_GLOBAL_DATA_LOAD_COUNT);

    LOG_F(INFO, "Loading Global Data(Player)");
    level_.foreach_key_with_prefix(
        "player_",
        [&result, &stop](const std::string &key, const std::string &value) {
            // key with player_20f2a225-0aaa-3c7d-9e2d-c57b7ec01be5
            if (key.size() != 43) return;
            result.playerData.append_nbt(key, value);
        },
        stop, cfg::MAX_GLOBAL_DATA_LOAD_COUNT);
}

bool AsyncLevelLoader::modifyLeveldat(bl::palette::compound_tag *nbt) {
    if (!this->loaded_) return false;
    level_.dat().set_nbt(nbt);
    auto raw = level_.dat().to_raw();
    bl::utils::write_file(level_.root_path() + "/" + bl::bedrock_level::LEVEL_DATA, raw.data(), raw.size());
    return true;
}

bool AsyncLevelLoader::modifyDBGlobal(const std::unordered_map<std::string, std::string> &modifies) {
    if (!this->loaded_) return false;
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
    return true;
}

std::vector<QString> AsyncLevelLoader::debugInfo() {
    std::vector<QString> res;
    res.emplace_back("Region cache:");
    for (int i = 0; i < 3; i++) {
        res.push_back(QString(" - [%1]: %2/%3")
                          .arg(QString::number(i), QString::number(this->region_cache_[i]->totalCost()),
                               QString::number(this->region_cache_[i]->maxCost())));
    }
    res.emplace_back("Null region cache:");
    for (int i = 0; i < 3; i++) {
        res.push_back(QString(" - [%1]: %2/%3")
                          .arg(QString::number(i), QString::number(this->invalid_cache_[i]->totalCost()),
                               QString::number(this->invalid_cache_[i]->maxCost())));
    }
    res.emplace_back("Thumbnail region cache:");
    for (int i = 0; i < 3; i++) {
        res.push_back(QString(" - [%1]: %2/%3")
                          .arg(QString::number(i), QString::number(this->thumbnails_cache_[i]->totalCost()),
                               QString::number(this->thumbnails_cache_[i]->maxCost())));
    }

    res.push_back(QString("Slime Chunk cache: %2/%3")
                      .arg(QString::number(this->slime_chunk_cache_->totalCost()), QString::number(this->slime_chunk_cache_->maxCost())));

    res.emplace_back("Background thread pool:");
    res.push_back(QString(" - Total threads: %1").arg(QString::number(cfg::THREAD_NUM)));

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

QImage *AsyncLevelLoader::bakedHeightImage(const region_pos &rp) {
    if (!this->loaded_) return &MapTile::UNLOADED_REGION_TILE();
    bool null_region{false};
    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region) return &MapTile::NULL_REGION_TILE();
    return region ? &region->height_bake_image_ : &MapTile::UNLOADED_REGION_TILE();
}

QImage *AsyncLevelLoader::bakeThumbnailImage(const region_pos &rp) {
    if (!this->loaded_) return &MapTile::UNLOADED_REGION_TILE();
    return this->tryGetThumbnail(rp);
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
    auto rp = cfg::c2r(cp);
    bool null_region{false};
    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region || (!region)) return {};
    auto &info = region->tips_info_;
    auto min_block_pos = rp.get_min_pos(bl::ChunkVersion::New);
    return region->tips_info_[p.x - min_block_pos.x][p.z - min_block_pos.z];
}

QImage *AsyncLevelLoader::bakedSlimeChunkImage(const region_pos &rp) {
    if (rp.dim != 0) return &MapTile::UNLOADED_REGION_TILE();
    auto *img = this->slime_chunk_cache_->operator[](rp);
    if (img) {
        return img;
    }
    auto *res = new QImage(cfg::RW << 4, cfg::RW << 4, QImage::Format_Indexed8);
    res->setColor(0, qRgba(0, 0, 0, 0));
    res->setColor(1, qRgba(29, 145, 44, 190));
    for (int rw = 0; rw < cfg::RW; rw++) {
        for (int rh = 0; rh < cfg::RW; rh++) {
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
