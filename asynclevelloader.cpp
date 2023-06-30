#include "asynclevelloader.h"

#include <QObject>
#include <QtConcurrent>
#include <chrono>
#include <thread>

#include "config.h"
#include "qdebug.h"
#include "world.h"

namespace {
    QColor EMPTY_COLOR_TABLE[2]{QColor(20, 20, 20), QColor(40, 40, 40)};

}

AsyncLevelLoader::AsyncLevelLoader() {
    this->pool_.setMaxThreadCount(4);
    for (int i = 0; i < 3; i++) {
        this->region_cache_.push_back(new QCache<region_pos, chunk_region>(cfg::REGION_CACHE_SIZE));
        this->invalid_cache_.push_back(new QCache<region_pos, char>(cfg::EMPTY_REGION_CACHE_SIZE));
    }
}

chunk_region *AsyncLevelLoader::getRegion(const region_pos &p, bool &empty) {
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
    if (this->region_in_queue(p)) {
        return nullptr;
    }

    auto *task = new LoadRegionTask(&this->level_, p);
    connect(task, SIGNAL(finish(int, int, int, chunk_region * )), this,
            SLOT(handle_task_finished_task(int, int, int, chunk_region * )));
    this->insert_region_to_queue(p);
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
    } else if (!region->valid()) {
        this->invalid_cache_[dim]->insert(bl::chunk_pos{x, z, dim}, new char(0));
        delete region;
    } else {
        this->region_cache_[dim]->insert(bl::chunk_pos{x, z, dim}, region);
    }
    this->remove_region_from_queue(bl::chunk_pos{x, z, dim});
}

AsyncLevelLoader::~AsyncLevelLoader() { this->close(); }

void LoadRegionTask::run() {
    auto *region = new chunk_region();
    for (int i = 0; i < cfg::RW; i++) {
        for (int j = 0; j < cfg::RW; j++) {
            bl::chunk_pos p{this->pos_.x + i, this->pos_.z + j, this->pos_.dim};
            try {
                region->chunks_[i][j] = this->level_->get_chunk(p);
            } catch (std::exception &e) {
                region->chunks_[i][j] = nullptr;
            }
        }
    }

    if (region->valid()) { //尝试烘焙
        region->biome_bake_image_ = new QImage(16 * cfg::RW, 16 * cfg::RW, QImage::Format_RGBA8888);
        region->terrain_bake_image_ = new QImage(16 * cfg::RW, 16 * cfg::RW, QImage::Format_RGBA8888);

        for (int rw = 0; rw < cfg::RW; rw++) {
            for (int rh = 0; rh < cfg::RW; rh++) {
                if (region->chunks_[rw][rh]) {
                    for (int i = 0; i < 16; i++) {
                        for (int j = 0; j < 16; j++) {
                            auto b = bl::get_biome_color(region->chunks_[rw][rh]->get_top_biome(i, j));
                            region->biome_bake_image_->setPixelColor((rw << 4) + i, (rh << 4) + j,
                                                                     QColor(b.r, b.g, b.b));
                            auto c = region->chunks_[rw][rh]->get_top_block_color(i, j);
                            region->terrain_bake_image_->setPixelColor(i + (rw << 4), j + (rh << 4),
                                                                       QColor(c.r, c.g, c.b, c.a));
                        }
                    }
                } else {
                    for (int i = 0; i < 16; i++) {
                        for (int j = 0; j < 16; j++) {
                            const int x = i + (rw << 4);
                            const int y = j + (rh << 4);
                            region->terrain_bake_image_->setPixelColor(x, y, EMPTY_COLOR_TABLE[
                                    (x / (cfg::RW * 8) + y / (cfg::RW * 8)) % 2]);
                            region->biome_bake_image_->setPixelColor(x, y, EMPTY_COLOR_TABLE[
                                    (x / (cfg::RW * 8) + y / (cfg::RW * 8)) % 2]);
                        }
                    }
                }
            }
        }
    }
    emit finish(this->pos_.x, this->pos_.z, this->pos_.dim, region);
}

bool AsyncLevelLoader::region_in_queue(const bl::chunk_pos &p) {
    bool exist{false};
    {
        std::lock_guard<std::mutex> l(this->m_);
        exist = this->processing_.count(p) > 0;
    }
    return exist;
}

void AsyncLevelLoader::insert_region_to_queue(const bl::chunk_pos &pos) {
    std::lock_guard<std::mutex> l(this->m_);
    this->processing_.insert(pos);
}

void AsyncLevelLoader::remove_region_from_queue(const bl::chunk_pos &pos) {
    std::lock_guard<std::mutex> l(this->m_);
    this->processing_.erase(pos);
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

chunk_region::~chunk_region() {
    for (int i = 0; i < cfg::RW; i++) {
        for (int j = 0; j < cfg::RW; j++) {
            delete chunks_[i][j];
        }
    }
    delete terrain_bake_image_;
    delete biome_bake_image_;
}

bool chunk_region::valid() const {
    for (int i = 0; i < cfg::RW; i++) {
        for (int j = 0; j < cfg::RW; j++) {
            if (chunks_[i][j]) return true;
        }
    }

    return false;
}
