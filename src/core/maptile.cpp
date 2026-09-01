#include "maptile.h"

#include <qcolor.h>
#include <qimage.h>
#include <qrgb.h>

#include <cstddef>
#include <cstdint>

#include "asynclevelloader.h"
#include "bedrock_level.h"
#include "color.h"  // bl::blend_color_with_biome, bl::get_biome_color
#include "config.h"
#include "maptile.h"
#include "sub_chunk.h"  // bl::block_info

// ---- helpers ----

namespace {
    // 45° sun direction → (dx, dy) where each component is -1, 0, or +1
    std::pair<int, int> sunVector() {
        switch (constant::SUN_DIRECTION) {
            case constant::SunDir::NW:
                return {-1, -1};
            case constant::SunDir::NE:
                return {1, -1};
            case constant::SunDir::SW:
                return {-1, 1};
            case constant::SunDir::SE:
                return {1, 1};
        }
        return {-1, -1};
    }

    void applyWaterOverlay(ChunkRegion *region, int IMG_WIDTH, int scale = 1) {
        auto &tp = region->tips_info_;
        const int res = IMG_WIDTH * scale;
        for (int bi = 0; bi < IMG_WIDTH; bi++) {
            for (int bj = 0; bj < IMG_WIDTH; bj++) {
                auto &info = tp[bi][bj];
                if (info.water_surface_color == 0) continue;
                float water_depth = static_cast<float>(info.height - info.solid_height);
                float water_opacity = std::min(0.15f * water_depth, 0.85f);
                int wr = qRed(info.water_surface_color);
                int wg = qGreen(info.water_surface_color);
                int wb = qBlue(info.water_surface_color);

                int x0 = bi * scale, x1 = x0 + scale;
                int y0 = bj * scale, y1 = y0 + scale;
                for (int hj = y0; hj < y1; hj++) {
                    auto *line = reinterpret_cast<QRgb *>(region->terrain_bake_image_.scanLine(hj));
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
}  // namespace

// ---- public ----

QImage MapTile::createQuadChessTile(int width, const QRgb &c1, const QRgb &c2, int scale) {
    int scaledWidth = width * scale;
    QImage image(scaledWidth, scaledWidth, QImage::Format_RGB32);
    for (int y = 0; y < scaledWidth; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        int gridY = (y / scale) % width;
        int yParity = gridY & 1;
        for (int x = 0; x < scaledWidth; ++x) {
            int gridX = (x / scale) % width;
            line[x] = ((gridX & 1) ^ yParity) ? c2 : c1;
        }
    }

    return image;
}

QImage &MapTile::UNLOADED_REGION_TILE() {
    static auto c1 = QColor(128, 128, 128).rgb();
    static auto c2 = QColor(148, 148, 148).rgb();
    static QImage img = createQuadChessTile(2, c1, c2, constant::RW << 3);
    return img;
}

QImage &MapTile::NULL_REGION_TILE() {
    static auto c1 = QColor(20, 20, 20).rgb();
    static auto c2 = QColor(40, 40, 40).rgb();

    static QImage img = createQuadChessTile(2, c1, c2, constant::RW << 3);
    return img;
}

QImage &MapTile::COORDS_LOADING_TILE() {
    static const auto color1 = QColor(128, 128, 128).rgb();
    static const auto color2 = QColor(148, 148, 148).rgb();
    static QImage image = createQuadChessTile(2, color1, color2, constant::COORDS_REGION_SIZE / 2);
    return image;
}

QImage &MapTile::COORDS_EMPTY_TILE() {
    static QImage image = [] {
        QImage result(constant::COORDS_REGION_SIZE, constant::COORDS_REGION_SIZE, QImage::Format_RGB32);
        result.fill(0xff000000u);
        return result;
    }();
    return image;
}

QImage MapTile::CREATE_REGION_TILE(const std::bitset<constant::RW * constant::RW> &chunk_bit_map, bool fill) {
    static auto color = QColor(setting::VOID_MAP_COLOR).rgb();
    auto img = NULL_REGION_TILE().copy();
    if (fill) {
        int gridSize = img.width() / constant::RW;

        for (int y = 0; y < img.height(); ++y) {
            QRgb *line = (QRgb *)img.scanLine(y);
            int gridY = y / gridSize;
            for (int x = 0; x < img.width(); ++x) {
                int gridX = x / gridSize;
                if (chunk_bit_map[gridX * constant::RW + gridY]) {
                    line[x] = color;
                }
            }
        }
    }
    return img;
}

QImage *MapTile::CREATE_REGION_THUMBNAIL(std::bitset<constant::RW * constant::RW> &region_bit_map) {
    static auto c1 = QColor(20, 20, 20).rgb();
    static auto c2 = QColor(40, 40, 40).rgb();
    static auto color = QColor(setting::VOID_MAP_COLOR).rgb();
    auto *img = new QImage(MapTile::createQuadChessTile(constant::RW, c1, c2));
    for (int y = 0; y < constant::RW; ++y) {
        QRgb *line = (QRgb *)img->scanLine(y);
        for (int x = 0; x < constant::RW; ++x) {
            if (region_bit_map[x * constant::RW + y]) {
                line[x] = color;
            }
        }
    }
    return img;
}

void MapTile::renderTerrainColumn(ChunkRegion *region, bl::chunk *ch, const MapFilter *filter, int rw, int rh, int chx, int chz, int y,
                                  int y_solid) {
    const int X = (rw << 4) + chx;
    const int Z = (rh << 4) + chz;
    auto info = ch->get_block(chx, y, chz);
    auto biome = ch->get_biome(chx, y, chz);
    info.color = bl::blend_color_with_biome(info.name, info.color, biome);

    bl::block_info solid_info = info;
    int16_t solid_h = static_cast<int16_t>(y);
    if (y_solid >= 0 && y_solid < y) {
        solid_info = ch->get_block(chx, y_solid, chz);
        solid_h = static_cast<int16_t>(y_solid);
    }

    bl::block_info render_info = info;
    if (setting::TRANSPARENT_WATER && info.name == "minecraft:water" && y_solid >= 0 && y_solid < y) {
        auto &tips = region->tips_info_[X][Z];
        tips.water_surface_color = qRgba(info.color.r, info.color.g, info.color.b, info.color.a);
        render_info = solid_info;
    }

    reinterpret_cast<QRgb *>(region->terrain_bake_image_.scanLine(Z))[X] =
        qRgba(render_info.color.r, render_info.color.g, render_info.color.b, render_info.color.a);

    if ((filter->biomes_list_.count(biome) == 0) == filter->biome_black_mode_) {
        auto biome_color = bl::get_biome_color(biome);
        reinterpret_cast<QRgb *>(region->biome_bake_image_.scanLine(Z))[X] =
            qRgba(biome_color.r, biome_color.g, biome_color.b, biome_color.a);
    }

    auto &tips = region->tips_info_[X][Z];
    tips.block_id = region->internBlockName(render_info.name);
    tips.solid_block_id = region->internBlockName(solid_info.name);
    // get_top_biome is robust when the exact surface layer stores none
    tips.biome = ch->get_top_biome(chx, chz);
    tips.height = static_cast<int16_t>(y);
    tips.solid_height = solid_h;
}

void MapTile::bakeChunkTerrain(bl::chunk *ch, const MapFilter *filter, int rw, int rh, ChunkRegion *region) {
    if (!ch || !region || !filter) return;
    auto [miny, maxy] = ch->get_pos().get_y_range(ch->get_version());

    // enable filter
    if (filter->enable_layer_) {
        if (filter->layer > maxy || filter->layer < miny) return;
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                auto b = ch->get_block_fast(i, filter->layer, j);
                if ((filter->blocks_list_.count(b.name) == 0) == filter->block_black_mode_) {
                    renderTerrainColumn(region, ch, filter, rw, rh, i, j, filter->layer, -1);
                }
            }
        }
        return;
    }

    // disable filter
    if (filter->isBlockFilterDefault()) {
        // Fast path: get_height() O(1) + get_top_y() scans only from surface
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                auto [top_y, solid_y] = ch->get_top_y(i, j, ch->get_height(i, j));
                if (top_y < miny) continue;
                renderTerrainColumn(region, ch, filter, rw, rh, i, j, top_y, solid_y);
            }
        }
        return;
    }

    // Slow path: custom filter — scan down from the height map
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            int y = ch->get_height(i, j);
            if (y < miny) continue;

            bool found = false;
            int found_y = miny - 1, solid_y = miny - 1;
            while (y >= miny) {
                auto b = ch->get_block_fast(i, y, j);
                if (!found && (filter->blocks_list_.count(b.name) == 0) == filter->block_black_mode_) {
                    found = true;
                    found_y = y;
                    if (b.name != "minecraft:water") {
                        solid_y = y;
                        break;
                    }
                    y--;
                    continue;
                }
                if (found && b.name != "minecraft:air" && b.name != "minecraft:water") {
                    solid_y = y;
                    break;
                }
                y--;
            }
            if (found) {
                renderTerrainColumn(region, ch, filter, rw, rh, i, j, found_y, solid_y);
            }
        }
    }
}

void MapTile::bakeChunkActors(bl::chunk *ch, const MapFilter *filter, ChunkRegion *region) {
    if (!ch) return;
    auto entities = ch->entities();
    auto mode = setting::ACTOR_RENDER_STYLE;
    for (auto &e : entities) {
        auto key = QString(e->identifier().c_str()).replace("minecraft:", "");
        if ((filter->actors_list_.count(key.toStdString()) == 0) == filter->actor_black_mode_) {
            if (mode == 0) {
                region->actors_[ActorImage(key)].push_back(e->pos());
            } else {
                auto chunk_pos = ch->get_pos();
                auto &ac = region->actors_counts_[chunk_pos][ActorImage(key)];
                ac = {e->pos(), ac.count + 1};
            }
        }
    }
}

// =====================================================================
//  render passes
// =====================================================================

void MapTile::renderStyle0(ChunkRegion *region, int IMG_WIDTH) { applyWaterOverlay(region, IMG_WIDTH); }

void MapTile::renderStyle1(ChunkRegion *region, int IMG_WIDTH) {
    applyWaterOverlay(region, IMG_WIDTH);

    // Directional shadow based on top block height — water blocks are skipped
    // (their colour comes from the sea floor, not the water surface).
    auto &tp = region->tips_info_;
    auto [sx, sy] = sunVector();
    const int kLevel = setting::SHADOW_LEVEL;

    for (int i = 0; i < IMG_WIDTH; i++) {
        for (int j = 0; j < IMG_WIDTH; j++) {
            if (tp[i][j].water_surface_color != 0) continue;  // water, skip shadow
            auto cur = tp[i][j].height;
            if (cur == -128) continue;

            int n1 = (i + sx >= 0 && i + sx < IMG_WIDTH) ? tp[i + sx][j].height : cur;
            int n2 = (j + sy >= 0 && j + sy < IMG_WIDTH) ? tp[i][j + sy].height : cur;
            int sum = (n1 == -128 ? cur : n1) + (n2 == -128 ? cur : n2);

            auto *line = reinterpret_cast<QRgb *>(region->terrain_bake_image_.scanLine(j));
            QRgb px = line[i];
            int r = qRed(px), g = qGreen(px), b = qBlue(px);
            if (cur * 2 > sum) {
                line[i] = qRgb(std::min(255, r * kLevel / 100), std::min(255, g * kLevel / 100), std::min(255, b * kLevel / 100));
            } else if (cur * 2 < sum) {
                line[i] = qRgb(r * 100 / kLevel, g * 100 / kLevel, b * 100 / kLevel);
            }
        }
    }
}

void MapTile::renderStyle2(ChunkRegion *region, int IMG_WIDTH, AsyncLevelLoader *loader, const bl::chunk_pos &region_pos) {
    const int scale = std::clamp(setting::TILE_RENDER_SCALE, 1, 32);
    const int HR = IMG_WIDTH * scale;

    QImage hr_t = region->terrain_bake_image_.scaled(HR, HR, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (hr_t.isNull()) return;

    auto &tp = region->tips_info_;
    const int edge_w = std::max(1, scale / 6);
    constexpr float kEdgeBright = 1.18f;
    constexpr float kEdgeDark = 0.76f;
    constexpr float kCornerBoost = 0.40f;
    const float rcp_ew = 1.0f / edge_w;

    for (int bi = 0; bi < IMG_WIDTH; bi++) {
        for (int bj = 0; bj < IMG_WIDTH; bj++) {
            auto &info = tp[bi][bj];
            float h = info.solid_height;
            if (info.height == -128) continue;

            // Underwater bevel fades with depth: deeper → closer to 1.0 (no bevel)
            float water_fade = 1.0f;
            if (info.water_surface_color != 0) {
                float wd = static_cast<float>(info.height - info.solid_height);
                water_fade = std::max(0.0f, 1.0f - wd / 5.0f);  // fully gone at depth ≥ 5
            }

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
                    factor = 1.0f + (factor - 1.0f) * water_fade;
                    factor = std::clamp(factor, 0.55f, 1.58f);

                    auto *tline = reinterpret_cast<QRgb *>(hr_t.scanLine(hj));
                    QRgb px_c = tline[hi];
                    tline[hi] = qRgb(qBound(0, qRound(qRed(px_c) * factor), 255), qBound(0, qRound(qGreen(px_c) * factor), 255),
                                     qBound(0, qRound(qBlue(px_c) * factor), 255));
                }
            }
        }
    }

    // ---- cross-region shadow (unified Data3D height source via cache) ----
    auto [sx, sy] = sunVector();
    const float kShadowDarkness = 1.0f - std::clamp(setting::SHADOW_LEVEL, 0, 255) / 255.0f * 0.75f;
    const int shadow_scale = setting::SHADOW_MAP_SCALE;
    const int SM = IMG_WIDTH * shadow_scale;
    constexpr int NH = 4;                   // neighbour chunks per side
    const int BORDER = NH * 16;             // border blocks per side
    const int EW = IMG_WIDTH + BORDER * 2;  // expanded width in blocks

    // 1. Build expanded height array — all entries from Data3D height_map via
    //    the cache, ensuring a single consistent height source across the
    //    centre region and its sunward border. Heights are world-space (min_y
    //    already applied in getHeightMap per the chunk's actual version).
    std::vector<int16_t> eh(EW * EW, -128);
    if (loader) {
        int dim = region_pos.dim;
        int region_cx = region_pos.x;  // chunk units
        int region_cz = region_pos.z;

        int ci0 = (sx < 0) ? region_cx - NH : region_cx;
        int ci1 = (sx > 0) ? region_cx + IMG_WIDTH / 16 + NH - 1 : region_cx + IMG_WIDTH / 16 - 1;
        int cj0 = (sy < 0) ? region_cz - NH : region_cz;
        int cj1 = (sy > 0) ? region_cz + IMG_WIDTH / 16 + NH - 1 : region_cz + IMG_WIDTH / 16 - 1;

        for (int gcx = ci0; gcx <= ci1; gcx++) {
            for (int gcz = cj0; gcz <= cj1; gcz++) {
                int local_x = (gcx - region_cx) * 16 + BORDER;
                int local_z = (gcz - region_cz) * 16 + BORDER;
                if (local_x < 0 || local_x >= EW - 15 || local_z < 0 || local_z >= EW - 15) continue;

                auto hm = loader->getHeightMap(bl::chunk_pos(gcx, gcz, dim));
                if (!hm) continue;
                for (int x = 0; x < 16; x++)
                    for (int z = 0; z < 16; z++) eh[(local_x + x) * EW + (local_z + z)] = (*hm)[x + z * 16];
            }
        }
    }

    // 2. Binary shadow map at SM resolution using expanded heights
    const int ESM = EW * shadow_scale;
    std::vector<float> shadow_map(SM * SM, 1.0f);
    const float dz_per_step = 1.0f / shadow_scale;

    for (int hi = 0; hi < SM; hi++) {
        for (int hj = 0; hj < SM; hj++) {
            int bi = hi / shadow_scale + BORDER;
            int bj = hj / shadow_scale + BORDER;
            auto h = eh[bi * EW + bj];
            if (h == -128) continue;

            float fx = static_cast<float>(hi) + 0.5f;
            float fy = static_cast<float>(hj) + 0.5f;
            int max_steps = (sx < 0 ? hi + BORDER * shadow_scale : ESM - 1 - hi);
            if (sy < 0)
                max_steps = std::min(max_steps, hj + BORDER * shadow_scale);
            else
                max_steps = std::min(max_steps, ESM - 1 - hj);
            if (max_steps > SM) max_steps = SM;

            for (int s = 1; s <= max_steps; s++) {
                int si = static_cast<int>(fx + s * sx);
                int sj = static_cast<int>(fy + s * sy);
                int sbi = si / shadow_scale + BORDER;
                int sbj = sj / shadow_scale + BORDER;
                auto nh = eh[sbi * EW + sbj];
                if (nh == -128) break;
                if (nh > h + s * dz_per_step + 0.1f) {
                    shadow_map[hi * SM + hj] = kShadowDarkness;
                    break;
                }
            }
        }
    }

    // 3. Per-HR-pixel shadow (direct lookup, no PCF)
    for (int bi = 0; bi < IMG_WIDTH; bi++) {
        for (int bj = 0; bj < IMG_WIDTH; bj++) {
            if (tp[bi][bj].height == -128) continue;

            int x0 = bi * scale, x1 = x0 + scale;
            int y0 = bj * scale, y1 = y0 + scale;
            int sm_x0 = bi * shadow_scale;
            int sm_y0 = bj * shadow_scale;

            for (int hi = x0; hi < x1; hi++) {
                int si = sm_x0 + (hi - x0) * shadow_scale / scale;
                for (int hj = y0; hj < y1; hj++) {
                    int sj = sm_y0 + (hj - y0) * shadow_scale / scale;
                    float shadow = shadow_map[si * SM + sj];
                    if (shadow >= 1.0f) continue;

                    auto *tline = reinterpret_cast<QRgb *>(hr_t.scanLine(hj));
                    QRgb px = tline[hi];
                    tline[hi] = qRgb(qBound(0, qRound(qRed(px) * shadow), 255), qBound(0, qRound(qGreen(px) * shadow), 255),
                                     qBound(0, qRound(qBlue(px) * shadow), 255));
                }
            }
        }
    }

    region->terrain_bake_image_ = hr_t;
    applyWaterOverlay(region, IMG_WIDTH, scale);
}
