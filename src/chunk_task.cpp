#include "chunk_task.h"

#include <qimage.h>

#include <QVector3D>
#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

#include "bedrock_key.h"
#include "config.h"
#include "include/chunk_task.h"
#include "maptile.h"

void RegionTimer::push(int64_t value) {
    this->values.push_back(value);
    if (this->values.size() > 10) {
        this->values.pop_front();
    }
}

ChunkRegion::~ChunkRegion() = default;

void LoadRegionTask::run() {
#ifdef QT_DEBUG
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
#endif

    auto *region = new ChunkRegion();
    bl::chunk *chunks_[cfg::RW * cfg::RW]{nullptr};

    // 读取区块数据
    for (int i = 0; i < cfg::RW; i++) {
        for (int j = 0; j < cfg::RW; j++) {
            bl::chunk_pos p{this->pos_.x + i, this->pos_.z + j, this->pos_.dim};
            chunks_[i * cfg::RW + j] = this->level_->get_chunk(p, true);
        }
    }

#ifdef QT_DEBUG
    std::chrono::steady_clock::time_point load_end = std::chrono::steady_clock::now();
#endif

    // 如果有合法区块，当前区域就是有效的
    for (auto &chunk : chunks_) {
        if (chunk && chunk->loaded()) {
            region->valid = true;
            break;
        }
    }

    const auto IMG_WIDTH = cfg::RW << 4;
    // 有效的才开始渲染
    if (region->valid) {  // 尝试烘焙
        for (int rw = 0; rw < cfg::RW; rw++) {
            for (int rh = 0; rh < cfg::RW; rh++) {
                auto *chunk = chunks_[rw * cfg::RW + rh];
                region->chunk_bit_map_.set(rw * cfg::RW + rh, chunk != nullptr);
            }
        }

        // init bg
        auto img = MapTile::CREATE_REGION_TILE(region->chunk_bit_map_, !cfg::transparent_void);
        region->terrain_bake_image_ = img;
        region->biome_bake_image_ = img;
        region->height_bake_image_ = img;
        // draw blocks
        for (int rw = 0; rw < cfg::RW; rw++) {
            for (int rh = 0; rh < cfg::RW; rh++) {
                auto *chunk = chunks_[rw * cfg::RW + rh];
                this->filter_->renderImages(chunk, rw, rh, region);
                this->filter_->bakeChunkActors(chunk, region);
                if (chunk) {
                    auto hss = chunk->HSAs();
                    region->HSAs_.insert(region->HSAs_.end(), hss.begin(), hss.end());
                }
            }
        }
    }

    // 烘焙

    if (cfg::MAP_RENDER_STYLE == 1) {
        for (int i = 0; i < IMG_WIDTH; i++) {
            for (int j = 0; j < IMG_WIDTH; j++) {
                auto current_height = region->tips_info_[i][j].height;
                if (current_height == -128) continue;
                int sum = current_height * 2;
                if (i == 0 && j != 0) {
                    sum = region->tips_info_[i][j - 1].height * 2;
                } else if (i != 0 && j == 0) {
                    sum = region->tips_info_[i - 1][j].height * 2;
                } else if (i != 0 && j != 0) {
                    sum = region->tips_info_[i][j - 1].height + region->tips_info_[i - 1][j].height;
                }

                if (current_height * 2 > sum) {
                    region->terrain_bake_image_.setPixelColor(i, j,
                                                              region->terrain_bake_image_.pixelColor(i, j).lighter(cfg::SHADOW_LEVEL));
                    //  region->biome_bake_image_.setPixelColor(i, j, region->biome_bake_image_.pixelColor(i,
                    //  j).lighter(cfg::SHADOW_LEVEL));

                } else if (current_height * 2 < sum) {
                    region->terrain_bake_image_.setPixelColor(i, j, region->terrain_bake_image_.pixelColor(i, j).darker(cfg::SHADOW_LEVEL));
                    //                    region->biome_bake_image_.setPixelColor(i, j, region->biome_bake_image_.pixelColor(i,
                    //                    j).lighter(cfg::SHADOW_LEVEL));
                }
            }
        }
    } else if (cfg::MAP_RENDER_STYLE == 2) {
        QVector3D normal{0., 2., 0.};
        QVector3D sun{5., -8, -1.};
        sun.normalize();
        double nx{0.}, nz{0.};
        for (int i = 0; i < IMG_WIDTH; i++) {
            for (int j = 0; j < IMG_WIDTH; j++) {
                auto &tp = region->tips_info_;
                auto current_height = tp[i][j].height;
                if (current_height == -128) continue;
                if (i == 0) {
                    nx = (current_height - tp[i + 1][j].height) << 1;
                } else if (i == IMG_WIDTH - 1) {
                    nx = (tp[i - 1][j].height - current_height) << 1;
                } else {
                    nx = tp[i - 1][j].height - tp[i + 1][j].height;
                }

                if (j == 0) {
                    nz = (current_height - tp[i][j + 1].height) << 1;
                } else if (j == IMG_WIDTH - 1) {
                    nz = (tp[i][j - 1].height - current_height) << 1;
                } else {
                    nz = tp[i][j - 1].height - tp[i][j + 1].height;
                }
                normal.setX(nx);
                normal.setZ(nz);
                normal.normalize();
                auto ambient = 0.32;
                auto diffuse = std::clamp(QVector3D::dotProduct(normal, sun), 0.f, 1.f) * (1 - ambient) + ambient;
                auto terraincolor = region->terrain_bake_image_.pixelColor(i, j);
                auto fragTerrainColor = QColor::fromRgbF(std::clamp(terraincolor.redF() * (diffuse), 0.0, 1.0),    // r
                                                         std::clamp(terraincolor.greenF() * (diffuse), 0.0, 1.0),  // g
                                                         std::clamp(terraincolor.blueF() * (diffuse), 0.0, 1.0),   // b
                                                         terraincolor.alphaF())
                                            .light(240);
                auto biomecolor = region->biome_bake_image_.pixelColor(i, j);
                auto fragBiomeColor = QColor::fromRgbF(std::clamp(biomecolor.redF() * (diffuse), 0.0, 1.0),    // r
                                                       std::clamp(biomecolor.greenF() * (diffuse), 0.0, 1.0),  // g
                                                       std::clamp(biomecolor.blueF() * (diffuse), 0.0, 1.0),   // b
                                                       biomecolor.alphaF())
                                          .light(240);
                region->terrain_bake_image_.setPixelColor(i, j, fragTerrainColor);
                region->biome_bake_image_.setPixelColor(i, j, fragBiomeColor);
            }
        }
    }
    for (auto *ch : chunks_) delete ch;

#ifdef QT_DEBUG
    std::chrono::steady_clock::time_point total_end = std::chrono::steady_clock::now();
    auto load_time = std::chrono::duration_cast<std::chrono::microseconds>(load_end - begin).count();
    auto render_time = std::chrono::duration_cast<std::chrono::microseconds>(total_end - load_end).count();
#else
    auto load_time = -1;
    auto render_time = -1;
#endif
    emit finish(this->pos_.x, this->pos_.z, this->pos_.dim, region, load_time, render_time, chunks_);
}

int64_t RegionTimer::mean() const {
    return this->values.empty() ? 0 : std::accumulate(values.begin(), values.end(), 0ll) / static_cast<int64_t>(values.size());
}

void LoadThumbnailTask::run() {
    std::bitset<cfg::RW * cfg::RW> chunk_bit_map;
    for (int i = 0; i < cfg::RW; i++) {
        for (int j = 0; j < cfg::RW; j++) {
            bl::chunk_pos p{this->pos_.x + i, this->pos_.z + j, this->pos_.dim};
            auto key1 = bl::chunk_key{bl::chunk_key::VersionOld, p}.to_raw();
            auto key2 = bl::chunk_key{bl::chunk_key::VersionNew, p}.to_raw();
            std::string value1, value2;
            bool load = this->level_->load_raw(key1, value1) || this->level_->load_raw(key2, value2);
            chunk_bit_map.set(i * cfg::RW + j, load);
        }
    }
    emit finish(this->pos_.x, this->pos_.z, this->pos_.dim, MapTile::CREATE_REGION_THUMBSNAIL(chunk_bit_map));
}
