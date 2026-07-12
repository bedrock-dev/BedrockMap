#include "chunk_task.h"

#include <qimage.h>

#include <algorithm>
#include <bitset>
#include <cmath>
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
        auto &tp = region->tips_info_;
        for (int i = 0; i < IMG_WIDTH; i++) {
            for (int j = 0; j < IMG_WIDTH; j++) {
                auto h = tp[i][j].height;
                if (h == -128) continue;
                auto cur = h;
                auto sum = cur * 2;
                if (i == 0 && j != 0) {
                    sum = tp[i][j - 1].height * 2;
                } else if (i != 0 && j == 0) {
                    sum = tp[i - 1][j].height * 2;
                } else if (i != 0 && j != 0) {
                    sum = tp[i][j - 1].height + tp[i - 1][j].height;
                }
                // 直接 QRgb 运算替代 setPixelColor/pixelColor（慢 10-20x）
                auto *line = reinterpret_cast<QRgb *>(region->terrain_bake_image_.scanLine(j));
                QRgb px = line[i];
                int r = qRed(px), g = qGreen(px), b = qBlue(px);
                if (cur * 2 > sum) {
                    // lighter(f): multiply by f/100, clamp to 255
                    line[i] = qRgb(std::min(255, r * setting::SHADOW_LEVEL / 100), std::min(255, g * setting::SHADOW_LEVEL / 100),
                                   std::min(255, b * setting::SHADOW_LEVEL / 100));
                } else if (cur * 2 < sum) {
                    // darker(f): multiply by 100/f
                    line[i] = qRgb(r * 100 / setting::SHADOW_LEVEL, g * 100 / setting::SHADOW_LEVEL, b * 100 / setting::SHADOW_LEVEL);
                }
            }
        }
    }

    // ---- Edge bevel: per-block highlight/shadow edges based on neighbour height ----
    void applyBevel(QImage &hr_t, QImage &hr_b, ChunkRegion *region, int IMG_WIDTH, int scale) {
        const int HR = hr_t.width();
        auto &tp = region->tips_info_;
        const int edge_w = std::max(1, scale / 6);
        constexpr float kEdgeBright = 1.18f;
        constexpr float kEdgeDark = 0.72f;
        constexpr float kCornerBoost = 0.40f;
        const float rcp_ew = 1.0f / edge_w;

        for (int bi = 0; bi < IMG_WIDTH; bi++) {
            for (int bj = 0; bj < IMG_WIDTH; bj++) {
                float h = tp[bi][bj].solid_height;
                if (tp[bi][bj].height == -128) continue;

                float hl = (bi > 0) ? tp[bi - 1][bj].solid_height : h;
                float hr_ = (bi < IMG_WIDTH - 1) ? tp[bi + 1][bj].solid_height : h;
                float ht = (bj > 0) ? tp[bi][bj - 1].solid_height : h;
                float hb = (bj < IMG_WIDTH - 1) ? tp[bi][bj + 1].solid_height : h;

                int x0 = bi * scale, x1 = x0 + scale;
                int y0 = bj * scale, y1 = y0 + scale;

                for (int hi = x0; hi < x1; hi++) {
                    int px = hi - x0;
                    float tx_l = (px < edge_w) ? 1.0f - px * rcp_ew : 0.0f;
                    float tx_r = (px >= scale - edge_w) ? (px - (scale - edge_w)) * rcp_ew : 0.0f;

                    for (int hj = y0; hj < y1; hj++) {
                        int py = hj - y0;
                        float ty_t = (py < edge_w) ? 1.0f - py * rcp_ew : 0.0f;
                        float ty_b = (py >= scale - edge_w) ? (py - (scale - edge_w)) * rcp_ew : 0.0f;

                        if (tx_l == 0.0f && tx_r == 0.0f && ty_t == 0.0f && ty_b == 0.0f) continue;

                        float factor = 1.0f;

                        auto apply_edge = [&](float t, float cur, float neigh) {
                            if (t <= 0.0f) return;
                            if (cur > neigh)
                                factor *= 1.0f + (kEdgeBright - 1.0f) * t;
                            else if (cur < neigh)
                                factor *= 1.0f + (kEdgeDark - 1.0f) * t;
                        };
                        apply_edge(tx_l, h, hl);
                        apply_edge(tx_r, h, hr_);
                        apply_edge(ty_t, h, ht);
                        apply_edge(ty_b, h, hb);

                        auto apply_corner = [&](float tc, float n1, float n2) {
                            if (tc <= 0.0f) return;
                            if (h > n1 && h > n2)
                                factor *= 1.0f + (kEdgeBright - 1.0f) * tc * kCornerBoost;
                            else if (h < n1 && h < n2)
                                factor *= 1.0f + (kEdgeDark - 1.0f) * tc * kCornerBoost;
                        };
                        float c_tl = std::min(tx_l, ty_t);
                        float c_tr = std::min(tx_r, ty_t);
                        float c_bl = std::min(tx_l, ty_b);
                        float c_br = std::min(tx_r, ty_b);
                        apply_corner(c_tl, hl, ht);
                        apply_corner(c_tr, hr_, ht);
                        apply_corner(c_bl, hl, hb);
                        apply_corner(c_br, hr_, hb);

                        if (factor == 1.0f) continue;
                        factor = std::clamp(factor, 0.55f, 1.58f);

                        auto *tline = reinterpret_cast<QRgb *>(hr_t.scanLine(hj));
                        QRgb px_c = tline[hi];
                        tline[hi] = qRgb(qBound(0, qRound(qRed(px_c) * factor), 255), qBound(0, qRound(qGreen(px_c) * factor), 255),
                                         qBound(0, qRound(qBlue(px_c) * factor), 255));
                        auto *bline = reinterpret_cast<QRgb *>(hr_b.scanLine(hj));
                        px_c = bline[hi];
                        bline[hi] = qRgb(qBound(0, qRound(qRed(px_c) * factor), 255), qBound(0, qRound(qGreen(px_c) * factor), 255),
                                         qBound(0, qRound(qBlue(px_c) * factor), 255));
                    }
                }
            }
        }
    }

    // ---- Shadow map: binary ray-march + PCF multi-sample blur for soft edges ----
    //    Uses coarse block heights (tips_info_) so 1-block height differences are preserved.
    void applyShadowMap(QImage &hr_t, QImage &hr_b, ChunkRegion *region, int IMG_WIDTH, int scale, float azimuth_rad, float zenith_rad) {
        const int HR = hr_t.width();
        auto &tp = region->tips_info_;
        constexpr float kShadowDarkness = 0.52f;
        const int kPcfRadius = setting::SHADOW_PCF_RADIUS;  // PCF blur radius (0=hard, 8=very soft)
        constexpr int kMaxSteps = 192;

        // Sun 2D direction
        const float sun_x = std::sin(azimuth_rad) * std::cos(zenith_rad);
        const float sun_y = std::sin(zenith_rad);
        const float sun_z = -std::cos(azimuth_rad) * std::cos(zenith_rad);
        const float sun_2d_len = std::sqrt(sun_x * sun_x + sun_z * sun_z);
        const float sdx = sun_x / sun_2d_len;
        const float sdz = sun_z / sun_2d_len;
        const float dz_per_step = (sun_y / sun_2d_len) / scale;

        // Helper: coarse height at HR pixel
        auto coarse_h = [&](int hi, int hj) -> float {
            int bi = std::clamp(hi / scale, 0, IMG_WIDTH - 1);
            int bj = std::clamp(hj / scale, 0, IMG_WIDTH - 1);
            return tp[bi][bj].height;
        };

        // 1. Binary shadow map: kShadowDarkness=shadowed, 1.0=lit
        std::vector<float> shadow_map(HR * HR, 1.0f);
        for (int hi = 0; hi < HR; hi++) {
            for (int hj = 0; hj < HR; hj++) {
                float h = coarse_h(hi, hj);
                if (h == -128) continue;

                float fx = static_cast<float>(hi) + 0.5f;
                float fy = static_cast<float>(hj) + 0.5f;
                for (int s = 1; s <= kMaxSteps; s++) {
                    int si = static_cast<int>(fx + s * sdx);
                    int sj = static_cast<int>(fy + s * sdz);
                    if (si < 0 || si >= HR || sj < 0 || sj >= HR) break;
                    float nh = coarse_h(si, sj);
                    if (nh == -128) break;
                    if (nh > h + s * dz_per_step + 0.1f) {
                        shadow_map[hi * HR + hj] = kShadowDarkness;
                        break;
                    }
                }
            }
        }

        // 2. PCF blur + apply
        for (int hi = 0; hi < HR; hi++) {
            for (int hj = 0; hj < HR; hj++) {
                if (coarse_h(hi, hj) == -128) continue;

                int i0 = std::max(0, hi - kPcfRadius), i1 = std::min(HR - 1, hi + kPcfRadius);
                int j0 = std::max(0, hj - kPcfRadius), j1 = std::min(HR - 1, hj + kPcfRadius);
                float sum = 0.0f;
                int count = 0;
                for (int si = i0; si <= i1; si++) {
                    for (int sj = j0; sj <= j1; sj++) {
                        if (coarse_h(si, sj) == -128) continue;
                        sum += shadow_map[si * HR + sj];
                        count++;
                    }
                }
                if (count == 0) continue;
                float shadow = sum / count;
                if (shadow >= 1.0f) continue;

                auto *tline = reinterpret_cast<QRgb *>(hr_t.scanLine(hj));
                QRgb px = tline[hi];
                tline[hi] = qRgb(qBound(0, qRound(qRed(px) * shadow), 255), qBound(0, qRound(qGreen(px) * shadow), 255),
                                 qBound(0, qRound(qBlue(px) * shadow), 255));
                auto *bline = reinterpret_cast<QRgb *>(hr_b.scanLine(hj));
                px = bline[hi];
                bline[hi] = qRgb(qBound(0, qRound(qRed(px) * shadow), 255), qBound(0, qRound(qGreen(px) * shadow), 255),
                                 qBound(0, qRound(qBlue(px) * shadow), 255));
            }
        }
    }

    // Shadow map → then bevel on top so SSAO bright edges show through shadows.
    void renderStyle2(ChunkRegion *region, int IMG_WIDTH) {
        const int scale = std::clamp(setting::SHADOW_RENDER_SCALE, 1, 32);
        const int HR = IMG_WIDTH * scale;

        QImage hr_t = region->terrain_bake_image_.scaled(HR, HR, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        if (hr_t.isNull()) return;

        constexpr float deg2rad = 3.1415926535f / 180.0f;
        const float azimuth_rad = 315.0f * deg2rad;
        const float zenith_rad = 60.0f * deg2rad;

        // 只渲染地形，群系图保持原样；dummy 用于吸收写入（被丢弃）
        QImage dummy(HR, HR, hr_t.format());

        // 1. 先叠水面颜色
        if (setting::TRANSPARENT_WATER) {
            auto &tp = region->tips_info_;
            for (int bi = 0; bi < IMG_WIDTH; bi++) {
                for (int bj = 0; bj < IMG_WIDTH; bj++) {
                    auto &info = tp[bi][bj];
                    if (info.water_depth == 0) continue;

                    float water_opacity = std::min(0.15f * info.water_depth, 0.85f);
                    int wr = qRed(info.water_surface_color);
                    int wg = qGreen(info.water_surface_color);
                    int wb = qBlue(info.water_surface_color);

                    int x0 = bi * scale, x1 = x0 + scale;
                    int y0 = bj * scale, y1 = y0 + scale;
                    for (int hj = y0; hj < y1; hj++) {
                        auto *line = reinterpret_cast<QRgb *>(hr_t.scanLine(hj));
                        for (int hi = x0; hi < x1; hi++) {
                            QRgb px = line[hi];
                            line[hi] = qRgba(static_cast<uint8_t>((1 - water_opacity) * qRed(px) + water_opacity * wr),
                                             static_cast<uint8_t>((1 - water_opacity) * qGreen(px) + water_opacity * wg),
                                             static_cast<uint8_t>((1 - water_opacity) * qBlue(px) + water_opacity * wb), 255);
                        }
                    }
                }
            }
        }

        // 2. 阴影落在水面/地形上
        applyShadowMap(hr_t, dummy, region, IMG_WIDTH, scale, azimuth_rad, zenith_rad);

        // 3. Bevel 加强边缘（水面也会有一点微起伏效果）
        applyBevel(hr_t, dummy, region, IMG_WIDTH, scale);

        region->terrain_bake_image_ = hr_t;
    }

    // ---- 水面颜色叠加（无 renderStyle / style1 时用）----
    void applyWaterOverlay(ChunkRegion *region, int IMG_WIDTH) {
        auto &tp = region->tips_info_;
        for (int i = 0; i < IMG_WIDTH; i++) {
            for (int j = 0; j < IMG_WIDTH; j++) {
                auto &info = tp[i][j];
                if (info.water_depth == 0) continue;

                float water_opacity = std::min(0.15f * info.water_depth, 0.85f);
                auto *line = reinterpret_cast<QRgb *>(region->terrain_bake_image_.scanLine(j));
                QRgb px = line[i];
                int wr = qRed(info.water_surface_color);
                int wg = qGreen(info.water_surface_color);
                int wb = qBlue(info.water_surface_color);
                line[i] = qRgba(static_cast<uint8_t>((1 - water_opacity) * qRed(px) + water_opacity * wr),
                                static_cast<uint8_t>((1 - water_opacity) * qGreen(px) + water_opacity * wg),
                                static_cast<uint8_t>((1 - water_opacity) * qBlue(px) + water_opacity * wb), 255);
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
    bl::chunk *chunks_[constant::RW * constant::RW]{nullptr};
    // 读取区块数据
    for (int i = 0; i < constant::RW; i++) {
        for (int j = 0; j < constant::RW; j++) {
            bl::chunk_pos p{this->pos_.x + i, this->pos_.z + j, this->pos_.dim};
            chunks_[i * constant::RW + j] = this->loader_->getChunk(p);
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

    const auto IMG_WIDTH = constant::RW << 4;

    if (region->valid) {
        for (int rw = 0; rw < constant::RW; rw++) {
            for (int rh = 0; rh < constant::RW; rh++) {
                auto *chunk = chunks_[rw * constant::RW + rh];
                region->chunk_bit_map_.set(rw * constant::RW + rh, chunk != nullptr);
            }
        }

        // init bg
        auto img = MapTile::CREATE_REGION_TILE(region->chunk_bit_map_, !this->loader_->transparentVoid());
        region->terrain_bake_image_ = img;
        region->biome_bake_image_ = img;
        region->height_bake_image_ = img;
        // draw blocks
        for (int rw = 0; rw < constant::RW; rw++) {
            for (int rh = 0; rh < constant::RW; rh++) {
                auto *chunk = chunks_[rw * constant::RW + rh];
                this->filter_->renderImages(chunk, rw, rh, region);
                this->filter_->bakeChunkActors(chunk, region);
                if (chunk) {
                    auto hss = chunk->HSAs();
                    region->HSAs_.insert(region->HSAs_.end(), hss.begin(), hss.end());
                }
            }
        }

        // blender
        if (setting::MAP_RENDER_STYLE == 1) {
            if (setting::TRANSPARENT_WATER) applyWaterOverlay(region, IMG_WIDTH);
            renderStyle1(region, IMG_WIDTH);
        } else if (setting::MAP_RENDER_STYLE == 2) {
            renderStyle2(region, IMG_WIDTH);  // 内部已处理水面叠加
        } else if (setting::TRANSPARENT_WATER) {
            applyWaterOverlay(region, IMG_WIDTH);
        }
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
    std::bitset<constant::RW * constant::RW> chunk_bit_map;
    for (int i = 0; i < constant::RW; i++) {
        for (int j = 0; j < constant::RW; j++) {
            bl::chunk_pos p{this->pos_.x + i, this->pos_.z + j, this->pos_.dim};
            auto *ch = this->loader_->getChunk(p);
            bool load = (ch && ch->loaded());
            chunk_bit_map.set(i * constant::RW + j, load);
            delete ch;
        }
    }
    emit finish(this->pos_.x, this->pos_.z, this->pos_.dim, MapTile::CREATE_REGION_THUMBNAIL(chunk_bit_map));
}
