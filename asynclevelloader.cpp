#include "asynclevelloader.h"

#include <QObject>
#include <QtConcurrent>
#include <chrono>
#include <thread>
#include "iconmanager.h"
#include "config.h"
#include "qdebug.h"
#include "leveldb/write_batch.h"

AsyncLevelLoader::AsyncLevelLoader() {
    this->pool_.setMaxThreadCount(8);
    for (int i = 0; i < 3; i++) {
        this->region_cache_.push_back(new QCache<region_pos, chunk_region>(cfg::REGION_CACHE_SIZE));
        this->invalid_cache_.push_back(new QCache<region_pos, char>(cfg::EMPTY_REGION_CACHE_SIZE));
    }
    this->slime_chunk_cache_ = new QCache<region_pos, QImage>(8192);
    this->level_.set_cache(false);
}

chunk_region *AsyncLevelLoader::tryGetRegion(const region_pos &p, bool &empty) {
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
    auto *task = new LoadRegionTask(&this->level_, p, &this->map_filter_);
    connect(task, &LoadRegionTask::finish, this, [this](int x, int z, int dim, chunk_region *region) {
        if (!region || (!region->valid)) {
            this->invalid_cache_[dim]->insert(bl::chunk_pos(x, z, dim), new char(0));
            delete region;
        } else {
            this->region_cache_[dim]->insert(bl::chunk_pos(x, z, dim), region);
        }
        this->processing_.remove(bl::chunk_pos{x, z, dim});
    });
    this->processing_.add(p);
    this->pool_.start(task);
    return nullptr;
}

bool AsyncLevelLoader::open(const std::string &path) {
    this->level_.set_cache(false);
    this->loaded_ = this->level_.open(path);
    return this->loaded_;
}

AsyncLevelLoader::~AsyncLevelLoader() { this->close(); }

void LoadRegionTask::run() {
    auto *region = new chunk_region();
    std::array<std::array<bl::chunk *, cfg::RW>, cfg::RW> chunks_{};
    for (auto &line: chunks_) {
        std::fill(line.begin(), line.end(), nullptr);
    }

    for (int i = 0; i < cfg::RW; i++) {
        for (int j = 0; j < cfg::RW; j++) {
            bl::chunk_pos p{this->pos_.x + i, this->pos_.z + j, this->pos_.dim};
            try {
                chunks_[i][j] = this->level_->get_chunk(p, true);
            } catch (std::exception &e) {
                chunks_[i][j] = nullptr;
            }
        }
    }

    for (auto &line: chunks_) {
        for (auto &ch: line) {
            if (ch && ch->loaded()) region->valid = true;
        }
    }

    const auto IMG_WIDTH = cfg::RW << 4;
    if (region->valid) {  //尝试烘焙

        region->terrain_bake_image_ = cfg::BACKGROUND_IMAGE_COPY();
        region->biome_bake_image_ = cfg::BACKGROUND_IMAGE_COPY();

        for (int rw = 0; rw < cfg::RW; rw++) {
            for (int rh = 0; rh < cfg::RW; rh++) {
                auto *chunk = chunks_[rw][rh];
                region->chunk_bit_map_[rw][rh] = chunk != nullptr;
                this->filter_->bakeChunkTerrain(chunk, rw, rh, region);
                this->filter_->bakeChunkBiome(chunk, rw, rh, region);
                this->filter_->bakeChunkActors(chunk, region);
                if (chunk) {
                    auto hss = chunk->HSAs();
                    region->HSAs_.insert(region->HSAs_.end(), hss.begin(), hss.end());
                }

            }
        }
    }

    //烘焙
    const int SHADOW = 130;
    for (int i = 0; i < IMG_WIDTH; i++) {
        for (int j = 0; j < IMG_WIDTH; j++) {
            auto current_height = region->tips_info_[i][j].height;
            if (current_height == -128)continue;
            int sum = current_height * 2;
            if (i == 0 && j != 0) {
                sum = region->tips_info_[i][j - 1].height * 2;
            } else if (i != 0 && j == 0) {
                sum = region->tips_info_[i - 1][j].height * 2;
            } else if (i != 0 && j != 0) {
                sum = region->tips_info_[i][j - 1].height + region->tips_info_[i - 1][j].height;
            }

            if (current_height * 2 > sum) {
                region->terrain_bake_image_->setPixelColor(i, j, region->terrain_bake_image_->pixelColor(i, j).lighter(
                        SHADOW));
            } else if (current_height * 2 < sum) {
                region->terrain_bake_image_->setPixelColor(i, j, region->terrain_bake_image_->pixelColor(i, j).darker(
                        SHADOW));
            }
        }
    }


    for (auto &chs: chunks_) {
        for (auto &ch: chs) {
            delete ch;
        }
    }
    emit finish(this->pos_.x, this->pos_.z, this->pos_.dim, region);
}

void AsyncLevelLoader::close() {
    if (!this->loaded_) return;
    qInfo() << "Try close level";
    this->loaded_ = false;    //阻止UI层请求数据
    this->processing_.clear();  //队列清除
    this->pool_.clear();        //清除所有任务
    this->pool_.waitForDone();  //等待当前任务完成
    qInfo() << "Clear work pool";
    this->level_.close();  //关闭存档
    this->clearAllCache();
}

bl::chunk *AsyncLevelLoader::getChunkDirect(const bl::chunk_pos &p) {
    return this->level_.get_chunk(p);
//    auto directChunkReader = [&](const bl::chunk_pos &chunk_pos) {
//        return this->level_.get_chunk(chunk_pos);
//    };
//    return QtConcurrent::run(directChunkReader, p);

}


void AsyncLevelLoader::clearAllCache() {
    this->pool_.clear(); //取消所有任务
    this->processing_.clear();
    for (auto &cache: this->region_cache_) {
        cache->clear();
    }

    for (auto cache: this->invalid_cache_) {
        cache->clear();
    }
    this->slime_chunk_cache_->clear();
}

QFuture<bool> AsyncLevelLoader::dropChunk(const bl::chunk_pos &min, const bl::chunk_pos &max) {
    auto directChunkReader = [&](const bl::chunk_pos &min, const bl::chunk_pos &max) {
        int res = 0;
        for (int i = min.x; i <= max.x; i++) {
            for (int j = min.z; j <= max.z; j++) {
                bl::chunk_pos cp{i, j, min.dim};
                this->level_.remove_chunk(cp);
            }
        }
        return true;
    };
    return QtConcurrent::run(directChunkReader, min, max);
}

bool AsyncLevelLoader::modifyLeveldat(bl::palette::compound_tag *nbt) {
    if (!this->loaded_)return false;
    level_.dat().set_nbt(nbt);
    auto raw = level_.dat().to_raw();
    bl::utils::write_file(level_.root_path() + "/" + bl::bedrock_level::LEVEL_DATA, raw.data(), raw.size());
    return true;
}

bool AsyncLevelLoader::modifyPlayerList(
        const std::unordered_map<std::string, bl::palette::compound_tag *> &new_list) {
    if (!this->loaded_)return false;
    //先写入磁盘再修改内存
    leveldb::WriteBatch batch;
    for (auto &kv: this->level_.player_list().data()) {
        if (!new_list.count(kv.first)) {
            batch.Delete(kv.first);
        } else { //put
            //可以检查不一样的才修改，不过没啥必要
            batch.Put(kv.first, kv.second->to_raw());
        }
    }
    auto s = this->level_.db()->Write(leveldb::WriteOptions(), &batch);
    if (s.ok()) {
        this->level_.player_list().reset(new_list);
        return true;
    }
    return false;
}


bool AsyncLevelLoader::modifyVillageList(
        const std::unordered_map<std::string, std::array<bl::palette::compound_tag *, 4>> &new_village_list) {
    if (!this->loaded_)return false;
    leveldb::WriteBatch batch;
    for (auto &kv: this->level_.village_list().data()) {
        const auto uuid = kv.first;
        auto it = new_village_list.find(uuid);
        if (it == new_village_list.end()) { //新的村庄表找不到这个了，直接把四个key全删了
            for (int i = 0; i < 4; i++) {
                bl::village_key key{kv.first, static_cast<bl::village_key::key_type>(i)};
                batch.Delete(key.to_raw());
            }
            continue;
        }
        //还有这个key的
        for (int i = 0; i < 4; i++) {
            bl::village_key key{kv.first, static_cast<bl::village_key::key_type>(i)};
            if (!it->second[i]) { //空指针，也是直接删除
                batch.Delete(key.to_raw());
            } else { //还在的，直接覆盖
                batch.Put(key.to_raw(), it->second[i]->to_raw());
            }
        }
    }

    auto s = this->level_.db()->Write(leveldb::WriteOptions(), &batch);
    if (s.ok()) {
        this->level_.village_list().reset(new_village_list);
        return true;
    }
    return false;
}

bool AsyncLevelLoader::modifyChunkBlockEntities(const bl::chunk_pos &cp,
                                                const std::string &raw) {
    bl::chunk_key key{bl::chunk_key::BlockEntity, cp, -1};
    auto s = this->level_.db()->Put(leveldb::WriteOptions(), key.to_raw(), raw);
    return s.ok();

}

bool AsyncLevelLoader::modifyChunkPendingTicks(const bl::chunk_pos &cp,
                                               const std::string &raw) {
    bl::chunk_key key{bl::chunk_key::PendingTicks, cp, -1};
    auto s = this->level_.db()->Put(leveldb::WriteOptions(), key.to_raw(), raw);
    return s.ok();
}

bool AsyncLevelLoader::modifyChunkActors(
        bl::chunk *ch,
        const std::vector<bl::actor *> &acs) {
    //不同的区块不用的算法
    if (!ch) return false;
    if (ch->get_version() == bl::Old) {
//        直接写入
        std::string raw;
        for (auto &p: acs)
            raw += p->root()->to_raw();
        bl::chunk_key key{bl::chunk_key::Entity, ch->get_pos()};
        auto s = this->level_.db()->Put(leveldb::WriteOptions(), key.to_raw(), raw);
        return s.ok();
    }


    std::unordered_map<std::string, bl::actor *> actor_map;
    for (auto ac: acs) {
        actor_map[ac->uid_raw()] = ac;
    }
    //new versions
    //获取所有的实体以及uid
    leveldb::WriteBatch batch;
    auto es = ch->entities();
    for (auto cur: es) {
        if (!actor_map.count(cur->uid_raw())) {
            batch.Delete("actorprefix" + cur->uid_raw());
        }
    }

    //写入新的实体
    std::string digest;
    for (auto &ac: actor_map) {
        batch.Put("actorprefix" + ac.first, ac.second->root()->to_raw());
        digest += ac.first;
    }

    //写入摘要
    bl::actor_digest_key adk{ch->get_pos()};
    batch.Put(adk.to_raw(), digest);
    auto s = this->level_.db()->Write(leveldb::WriteOptions(), &batch);
    return s.ok();
}

chunk_region::~chunk_region() {
    delete terrain_bake_image_;
    delete biome_bake_image_;
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

    res.push_back(QString("Slime Chunk cache: %2/%3")
                          .arg(QString::number(this->slime_chunk_cache_->totalCost()),
                               QString::number(this->slime_chunk_cache_->maxCost())));

    res.emplace_back("Background thread pool:");
    res.push_back(QString(" - Total threads: %1").arg(QString::number(cfg::THREAD_NUM)));
    res.push_back(QString(" - Background tasks %1").arg(QString::number(this->processing_.size())));

    return res;
}

QImage *AsyncLevelLoader::bakedTerrainImage(const region_pos &rp) {
    if (!this->loaded_) return cfg::BACKGROUND_IMAGE();
    bool null_region{false};
    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region)return cfg::BACKGROUND_IMAGE();
    return region ? region->terrain_bake_image_ : cfg::BACKGROUND_IMAGE();
}

QImage *AsyncLevelLoader::bakedBiomeImage(const region_pos &rp) {
    if (!this->loaded_) return cfg::BACKGROUND_IMAGE();
    bool null_region{false};

    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region) return cfg::BACKGROUND_IMAGE();

    return region ? region->biome_bake_image_ : cfg::BACKGROUND_IMAGE();
}

std::unordered_map<QImage *, std::vector<bl::vec3>> AsyncLevelLoader::getActorList(const region_pos &rp) {
    if (!this->loaded_) return {};
    bool null_region{false};
    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region || (!region)) return {};
    return region->actors_;

}

std::vector<bl::hardcoded_spawn_area> AsyncLevelLoader::getHSAs(const region_pos &rp) {
    if (!this->loaded_)return {};
    bool null_region{false};
    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region || (!region)) return {};
    return region->HSAs_;
}

BlockTipsInfo AsyncLevelLoader::getBlockTips(const bl::block_pos &p, int dim) {
    if (!this->loaded_)return {};
    auto cp = p.to_chunk_pos();
    cp.dim = dim;
    auto rp = cfg::c2r(cp);
    bool null_region{false};
    auto *region = this->tryGetRegion(rp, null_region);
    if (null_region)return {};
    auto &info = region->tips_info_;
    auto min_block_pos = rp.get_min_pos(bl::ChunkVersion::New);
    return region->tips_info_[p.x - min_block_pos.x][p.z - min_block_pos.z];
}


QImage *AsyncLevelLoader::bakedHeightImage(const region_pos &rp) {
    return cfg::BACKGROUND_IMAGE();
}

QImage *AsyncLevelLoader::bakedSlimeChunkImage(const region_pos &rp) {
    if (rp.dim != 0)return cfg::EMPTY_IMAGE();
    auto *img = this->slime_chunk_cache_->operator[](rp);
    if (img) {
        return img;
    }
    auto *res = new QImage(cfg::RW << 4, cfg::RW << 4, QImage::Format_RGBA8888);
    for (int rw = 0; rw < cfg::RW; rw++) {
        for (int rh = 0; rh < cfg::RW; rh++) {
            bl::chunk_pos cp(rp.x + rw, rp.z + rh, rp.dim);
            auto color = cp.is_slime() ? QColor(162, 255, 134, 100) : QColor(0, 0, 0, 0);
            for (int i = 0; i < 16; i++) {
                for (int j = 0; j < 16; j++) {
                    res->setPixelColor((rw << 4) + i, (rh << 4) + j, color);
                }
            }
        }
    }
    this->slime_chunk_cache_->insert(rp, res);
    return res;
}









