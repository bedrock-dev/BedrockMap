#include "chunk_task.h"

#include <qimage.h>

#include <QVector3D>
#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunk_task.h"
#include "config.h"
#include "maptile.h"

namespace {

    void renderStyle1(ChunkRegion *region, int IMG_WIDTH) {
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
                // 直接 QRgb 运算替代 setPixelColor/pixelColor（慢 10-20x）
                auto *line = reinterpret_cast<QRgb *>(region->terrain_bake_image_.scanLine(j));
                QRgb px = line[i];
                int r = qRed(px), g = qGreen(px), b = qBlue(px);
                if (current_height * 2 > sum) {
                    // lighter(f): multiply by f/100, clamp to 255
                    line[i] = qRgb(std::min(255, r * cfg::SHADOW_LEVEL / 100), std::min(255, g * cfg::SHADOW_LEVEL / 100),
                                   std::min(255, b * cfg::SHADOW_LEVEL / 100));
                } else if (current_height * 2 < sum) {
                    // darker(f): multiply by 100/f
                    line[i] = qRgb(r * 100 / cfg::SHADOW_LEVEL, g * 100 / cfg::SHADOW_LEVEL, b * 100 / cfg::SHADOW_LEVEL);
                }
            }
        }
    }

    void renderStyle2(ChunkRegion *region, int IMG_WIDTH) {
        constexpr float k_small = 1.0f / 2.0f;
        constexpr float k_large = 1.0f / 12.0f;
        constexpr int step_large = 2;
        constexpr float ambient = 0.50f;
        constexpr float brightness = 1.15f;
        QVector3D sun(0.3f, 1.0f, -0.15f);
        sun.normalize();

        for (int i = 0; i < IMG_WIDTH; i++) {
            for (int j = 0; j < IMG_WIDTH; j++) {
                auto &tp = region->tips_info_;
                auto h = tp[i][j].height;
                if (h == -128) continue;

                float dhdx_f = (i > 0 && i < IMG_WIDTH - 1) ? (tp[i + 1][j].height - tp[i - 1][j].height)
                                                            : (i == 0 ? (tp[i + 1][j].height - h) : (h - tp[i - 1][j].height));
                float dhdz_f = (j > 0 && j < IMG_WIDTH - 1) ? (tp[i][j + 1].height - tp[i][j - 1].height)
                                                            : (j == 0 ? (tp[i][j + 1].height - h) : (h - tp[i][j - 1].height));

                int i0 = std::max(0, i - step_large);
                int i1 = std::min(IMG_WIDTH - 1, i + step_large);
                int j0 = std::max(0, j - step_large);
                int j1 = std::min(IMG_WIDTH - 1, j + step_large);
                float dhdx_l = (tp[i1][j].height - tp[i0][j].height) / static_cast<float>(i1 - i0);
                float dhdz_l = (tp[i][j1].height - tp[i][j0].height) / static_cast<float>(j1 - j0);

                float dhdx = dhdx_f * 0.6f + dhdx_l * 0.4f;
                float dhdz = dhdz_f * 0.6f + dhdz_l * 0.4f;

                float tilt = std::sqrt(dhdx_l * dhdx_l + dhdz_l * dhdz_l) * k_large;
                float vy = 1.0f / (1.0f + tilt);
                QVector3D normal(-dhdx * k_small * vy, vy, -dhdz * k_small * vy);
                normal.normalize();

                float diffuse = std::max(0.0f, QVector3D::dotProduct(normal, sun));
                float factor = (diffuse * (1.0f - ambient) + ambient) * brightness;

                // 直接 QRgb 运算替代 setPixelColor/pixelColor
                auto apply_rgb = [factor](QRgb px) -> QRgb {
                    return qRgb(qBound(0, qRound(qRed(px) * factor), 255), qBound(0, qRound(qGreen(px) * factor), 255),
                                qBound(0, qRound(qBlue(px) * factor), 255));
                };
                auto *terrain_line = reinterpret_cast<QRgb *>(region->terrain_bake_image_.scanLine(j));
                auto *biome_line = reinterpret_cast<QRgb *>(region->biome_bake_image_.scanLine(j));
                terrain_line[i] = apply_rgb(terrain_line[i]);
                biome_line[i] = apply_rgb(biome_line[i]);
            }
        }
    }

}  // namespace

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
            chunks_[i * cfg::RW + j] = this->loader_->getChunk(p);
        }
    }

#ifdef QT_DEBUG
    std::chrono::steady_clock::time_point load_end = std::chrono::steady_clock::now();
#endif

    for (auto &chunk : chunks_) {
        if (chunk && chunk->loaded()) {
            region->valid = true;
            break;
        }
    }

    const auto IMG_WIDTH = cfg::RW << 4;

    if (region->valid) {
        for (int rw = 0; rw < cfg::RW; rw++) {
            for (int rh = 0; rh < cfg::RW; rh++) {
                auto *chunk = chunks_[rw * cfg::RW + rh];
                region->chunk_bit_map_.set(rw * cfg::RW + rh, chunk != nullptr);
            }
        }

        // init bg
        auto img = MapTile::CREATE_REGION_TILE(region->chunk_bit_map_, !this->loader_->transparentVoid());
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

    // blender
    if (cfg::MAP_RENDER_STYLE == 1) {
        renderStyle1(region, IMG_WIDTH);
    } else if (cfg::MAP_RENDER_STYLE == 2) {
        renderStyle2(region, IMG_WIDTH);
    }

#ifdef QT_DEBUG
    std::chrono::steady_clock::time_point total_end = std::chrono::steady_clock::now();
    auto load_time = std::chrono::duration_cast<std::chrono::microseconds>(load_end - begin).count();
    auto render_time = std::chrono::duration_cast<std::chrono::microseconds>(total_end - load_end).count();
#else
    auto load_time = -1;
    auto render_time = -1;
#endif
    emit finish(this->pos_.x, this->pos_.z, this->pos_.dim, region, load_time, render_time, chunks_);
    for (auto *chunk : chunks_) delete chunk;
}

int64_t RegionTimer::mean() const {
    return this->values.empty() ? 0 : std::accumulate(values.begin(), values.end(), 0ll) / static_cast<int64_t>(values.size());
}

void LoadThumbnailTask::run() {
    std::bitset<cfg::RW * cfg::RW> chunk_bit_map;
    for (int i = 0; i < cfg::RW; i++) {
        for (int j = 0; j < cfg::RW; j++) {
            bl::chunk_pos p{this->pos_.x + i, this->pos_.z + j, this->pos_.dim};
            auto *ch = this->loader_->getChunk(p);
            bool load = (ch && ch->loaded());
            chunk_bit_map.set(i * cfg::RW + j, load);
            delete ch;
        }
    }
    emit finish(this->pos_.x, this->pos_.z, this->pos_.dim, MapTile::CREATE_REGION_THUMBNAIL(chunk_bit_map));
}
