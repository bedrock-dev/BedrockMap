#include "asynclevelloader.h"

#include <QObject>
#include <QtConcurrent>
#include <chrono>
#include <thread>

#include "config.h"
#include "qdebug.h"
#include "world.h"
#include "leveldb/write_batch.h"

namespace {
    //  QColor EMPTY_COLOR_TABLE[2]{QColor(20, 20, 20), QColor(40, 40, 40)};

}

AsyncLevelLoader::AsyncLevelLoader() {
    this->pool_.setMaxThreadCount(8);
    for (int i = 0; i < 3; i++) {
        this->region_cache_.push_back(new QCache<region_pos, chunk_region>(cfg::REGION_CACHE_SIZE));
        this->invalid_cache_.push_back(new QCache<region_pos, char>(cfg::EMPTY_REGION_CACHE_SIZE));
    }
}

chunk_region *AsyncLevelLoader::getRegion(const region_pos &p, bool &empty, const RenderFilter *filter) {
    empty = false;
    if (!this->loaded_) return nullptr;
    auto *invalid = this->invalid_cache_[p.dim]->operator[](p);
    if (invalid) {
        empty = true;
        return nullptr;
    }

    // chunk cache
    auto *region = this->region_cache_[p.dim]->operator[](p);
    if (region) {
        return region;
    }

    // not in cache but in queue
    if (this->processing_.contains(p)) {
        return nullptr;
    }

    auto *task = new LoadRegionTask(&this->level_, p, filter);
    connect(task, SIGNAL(finish(int, int, int, chunk_region * )), this,
            SLOT(handle_task_finished_task(int, int, int, chunk_region * )));
    this->processing_.add(p);
    this->pool_.start(task);
    return nullptr;
}

bool AsyncLevelLoader::init(const std::string &path) {
    this->level_.set_cache(false);
    auto res = this->level_.open(path);
    if (res) {
        this->loaded_ = true;
    }
    return res;
}

void AsyncLevelLoader::handle_task_finished_task(int x, int z, int dim, chunk_region *region) {
    if (!region) {
        this->invalid_cache_[dim]->insert(bl::chunk_pos{x, z, dim}, new char(0));
    } else if (!region->valid) {
        this->invalid_cache_[dim]->insert(bl::chunk_pos{x, z, dim}, new char(0));
        delete region;
    } else {
        this->region_cache_[dim]->insert(bl::chunk_pos{x, z, dim}, region);
    }
    this->processing_.remove(bl::chunk_pos{x, z, dim});
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
                chunks_[i][j] = this->level_->get_chunk(p);
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
        region->biome_bake_image_ = new QImage(cfg::RW << 4, cfg::RW << 4, QImage::Format_RGBA8888);
        region->terrain_bake_image_ = new QImage(cfg::RW << 4, cfg::RW << 4, QImage::Format_RGBA8888);
        for (int rw = 0; rw < cfg::RW; rw++) {
            for (int rh = 0; rh < cfg::RW; rh++) {
                auto *chunk = chunks_[rw][rh];
                if (chunk) {
                    for (int i = 0; i < 16; i++) {
                        for (int j = 0; j < 16; j++) {
                            bl::color block_color{};
                            std::string block_name;
                            auto &tips = region->tips_info_[(rw << 4) + i][(rh << 4) + j];

                            if (filter_->enable_layer_) {
                                auto [min_y, max_y] = this->pos_.get_y_range(chunk->get_version());
                                if (filter_->layer_ >= min_y && filter_->layer_ <= max_y) {
                                    tips.height = filter_->layer_;
                                    tips.biome = chunk->get_biome(i, filter_->layer_, j);
                                    tips.block_name = chunk->get_block(i, filter_->layer_, j).name;
                                    block_color = chunk->get_block_color(i, filter_->layer_, j);
                                }
                            } else {
                                tips.height = chunk->get_height(i, j);
                                tips.biome = chunk->get_top_biome(i, j);
                                tips.block_name = chunk->get_top_block(i, j).name;
                                block_color = chunk->get_top_block_color(i, j);
                            }

                            auto biome_color = bl::get_biome_color(tips.biome);
                            region->biome_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
                                                                     QColor(biome_color.r, biome_color.g,
                                                                            biome_color.b));

                            auto terrain_color = QColor(block_color.r, block_color.g, block_color.b, block_color.a);
                            region->terrain_bake_image_->setPixelColor(i + (rw << 4), j + (rh << 4), terrain_color);
                        }
                    }

                    for (auto &ac: chunk->entities()) {
                        region->actors_[ac->identifier()].push_back(ac->pos());
                    }

                } else {
                    for (int i = 0; i < 16; i++) {
                        for (int j = 0; j < 16; j++) {
                            const int x = i + (rw << 4);
                            const int y = j + (rh << 4);
                            auto &tips = region->tips_info_[(rw << 4) + i][(rh << 4) + j];
                            tips.height = -128;
                            const int arr[2]{cfg::BG_GRAY, cfg::BG_GRAY + 20};
                            int index = (x / (cfg::RW * 8) + y / (cfg::RW * 8)) % 2;
                            region->terrain_bake_image_->setPixelColor(x, y,
                                                                       QColor(arr[index], arr[index], arr[index]));
                            region->biome_bake_image_->setPixelColor(x, y, QColor(arr[index], arr[index], arr[index]));
                        }
                    }
                }
            }
        }
    }

    //烘焙
    const int SHADOW = 120;
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
                region->terrain_bake_image_->setPixelColor(i, j,
                                                           region->terrain_bake_image_->pixelColor(i, j).light(SHADOW));
            } else if (current_height * 2 < sum) {
                region->terrain_bake_image_->setPixelColor(i, j,
                                                           region->terrain_bake_image_->pixelColor(i, j).dark(SHADOW));
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
    this->loaded_ = false;    //阻止UI层请求数据
    this->processing_.clear();  //队列清除
    this->pool_.clear();        //清除所有任务
    this->pool_.waitForDone();  //等待当前任务完成
    this->level_.close();  //关闭存档
    for (auto &c: this->region_cache_) c->clear();
}

QFuture<bl::chunk *> AsyncLevelLoader::getChunkDirect(const bl::chunk_pos &p) {
    auto directChunkReader = [&](const bl::chunk_pos &chunk_pos) {
        auto *p = this->level_.get_chunk(chunk_pos);
        return p;
    };
    return QtConcurrent::run(directChunkReader, p);
}

void AsyncLevelLoader::clear_all_cache() {
    this->pool_.clear(); //取消所有任务
    this->processing_.clear();
    for (auto &cache: this->region_cache_) {
        cache->clear();
    }

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
        const bl::chunk_pos &cp,
        const std::unordered_map<std::string, bl::palette::compound_tag *> &bes) {
    //TODO
    return true;
}

chunk_region::~chunk_region() {
    delete terrain_bake_image_;
    delete biome_bake_image_;
}


