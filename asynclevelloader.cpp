#include "asynclevelloader.h"

#include <QObject>
#include <QtConcurrent>
#include <chrono>
#include <thread>

#include "config.h"
#include "qdebug.h"
#include "world.h"

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

    if (region->valid) {  //尝试烘焙
        region->biome_bake_image_ = new QImage(16 * cfg::RW, 16 * cfg::RW, QImage::Format_RGBA8888);
        region->terrain_bake_image_ = new QImage(16 * cfg::RW, 16 * cfg::RW, QImage::Format_RGBA8888);
        for (int rw = 0; rw < cfg::RW; rw++) {
            for (int rh = 0; rh < cfg::RW; rh++) {
                auto *chunk = chunks_[rw][rh];
                if (chunk) {
                    for (int i = 0; i < 16; i++) {
                        for (int j = 0; j < 16; j++) {
                            bl::color block_color{};
                            std::string block_name;
                            auto &tips = region->tips_info_[(rw << 4) + i][(rh << 4) + j];

                            //分两种情况
                            if (filter_->enable_layer_) {
                                auto [min_y, max_y] = this->pos_.get_y_range();
                                if (filter_->layer_ >= min_y && filter_->layer_ <= max_y) {
                                    tips.biome = chunk->get_biome(i, filter_->layer_, j);
                                    tips.block_name = chunk->get_block(i, filter_->layer_, j).name;
                                    block_color = chunk->get_block_color(i, filter_->layer_, j);
                                }
                            } else {
                                tips.biome = chunk->get_top_biome(i, j);
                                tips.block_name = chunk->get_top_block(i, j).name;
                                block_color = chunk->get_top_block_color(i, j);
                            }

                            auto biome_color = bl::get_biome_color(tips.biome);
                            region->biome_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
                                                                     QColor(biome_color.r, biome_color.g,
                                                                            biome_color.b));
                            region->terrain_bake_image_->setPixelColor(
                                    i + (rw << 4), j + (rh << 4),
                                    QColor(block_color.r, block_color.g, block_color.b, block_color.a));
                        }
                    }
                    //load actors
                    auto &actors = chunk->get_actor_list();
                    for (auto &ac: actors) {
                        auto *actor = this->level_->load_actor(ac);
                        if (actor) {
                            region->actors_[actor->identifier()].push_back(actor->pos());
                        }
                        delete actor;
                    }
                } else {
                    for (int i = 0; i < 16; i++) {
                        for (int j = 0; j < 16; j++) {
                            const int x = i + (rw << 4);
                            const int y = j + (rh << 4);
                            const int arr[2]{cfg::BG_GRAY, cfg::BG_GRAY + 20};
                            int index = (x / (cfg::RW * 8) + y / (cfg::RW * 8)) % 2;
                            region->terrain_bake_image_->setPixelColor(x, y,
                                                                       QColor(arr[index], arr[index], arr[index]));
                            region->biome_bake_image_->setPixelColor(x, y, QColor(arr[index], arr[index], arr[index]));
                        }
                    }
                }
                delete chunk;
            }
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

chunk_region::~chunk_region() {
    delete terrain_bake_image_;
    delete biome_bake_image_;
}


