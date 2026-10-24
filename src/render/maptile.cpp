#include "render/maptile.h"

#include <QString>
#include <cstdint>

#include "base/config.h"
#include "data_3d.h"
#include "loader/regionbake.h"
#include "render/resourcemanager.h"
#include "sub_chunk.h"

void MapTile::renderTerrainColumn(ChunkRegion* region, bl::chunk* ch, const MapFilter& filter, int rw, int rh, int chx, int chz, int y,
                                  int y_solid) {
    const int X = (rw << 4) + chx;
    const int Z = (rh << 4) + chz;
    const auto raw_info = ch->get_block_with_color(chx, y, chz);
    const auto& info = raw_info;
    const auto biome = ch->get_biome(chx, y, chz);

    bl::block_appearance solid_info = info;
    int16_t solid_h = static_cast<int16_t>(y);
    if (y_solid >= 0 && y_solid < y) {
        solid_info = ch->get_block_with_color(chx, y_solid, chz);
        solid_h = static_cast<int16_t>(y_solid);
    }

    bl::block_appearance render_info = info;
    const bool transparent_water_overlay =
        setting::current().transparentWater && info.name == "minecraft:water" && y_solid >= 0 && y_solid < y;
    if (transparent_water_overlay) {
        render_info = solid_info;
    }

    auto& tips = region->tips_info_[X][Z];
    tips.block_id = region->internBlockName(render_info.name);
    tips.biome = ch->get_top_biome(chx, chz);
    tips.height = static_cast<int16_t>(y);
    tips.solid_height = solid_h;
    // Keep the exact palette entry for GPU material lookup.
    const auto& gpu_base = transparent_water_overlay ? solid_info : raw_info;
    tips.gpu_base_color = qRgba(gpu_base.color.r, gpu_base.color.g, gpu_base.color.b, gpu_base.color.a);
    tips.gpu_tint_kind = static_cast<uint8_t>(bl::block_biome_tint_kind(gpu_base.name));
    tips.gpu_water_overlay = transparent_water_overlay;
    const bool biome_visible = (filter.biomes_list_.count(biome) == 0) == filter.biome_black_mode_;
    tips.gpu_biome_visible = biome_visible;
}

void MapTile::bakeChunkTerrain(bl::chunk* ch, const MapFilter& filter, int rw, int rh, ChunkRegion* region) {
    if (!ch || !region) return;
    auto [miny, maxy] = ch->get_y_range();

    if (filter.enable_layer_) {
        if (filter.layer > maxy || filter.layer < miny) return;
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                const auto& b = ch->get_block_name(i, filter.layer, j);
                if ((filter.blocks_list_.count(b) == 0) == filter.block_black_mode_) {
                    renderTerrainColumn(region, ch, filter, rw, rh, i, j, filter.layer, -1);
                }
            }
        }
        return;
    }

    if (filter.isBlockFilterDefault()) {
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                auto height = 319;
                auto [top_y, solid_y] = ch->get_top_y(i, j, height);
                if (top_y < miny) continue;
                renderTerrainColumn(region, ch, filter, rw, rh, i, j, top_y, solid_y);
            }
        }
        return;
    }

    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            int y = ch->get_height(i, j);
            if (y < miny) continue;
            bool found = false;
            int found_y = miny - 1, solid_y = miny - 1;
            while (y >= miny) {
                const auto& b = ch->get_block_name(i, y, j);
                if (!found && (filter.blocks_list_.count(b) == 0) == filter.block_black_mode_) {
                    found = true;
                    found_y = y;
                    if (b != "minecraft:water") {
                        solid_y = y;
                        break;
                    }
                    y--;
                    continue;
                }
                if (found && b != "minecraft:air" && b != "minecraft:water") {
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

void MapTile::bakeChunkActors(bl::chunk* ch, const MapFilter& filter, ChunkRegion* region) {
    if (!ch) return;
    auto entities = ch->entities();
    auto mode = setting::current().actorRenderStyle;
    for (auto& e : entities) {
        const auto identifier = e->identifier();
        auto key = QString::fromUtf8(identifier.data(), static_cast<qsizetype>(identifier.size())).replace("minecraft:", "");
        if ((filter.actors_list_.count(key.toStdString()) == 0) == filter.actor_black_mode_) {
            if (mode == 0) {
                region->actors_[ActorImage(key)].push_back(e->pos());
            } else {
                auto chunk_pos = ch->get_pos();
                auto& ac = region->actors_counts_[chunk_pos][ActorImage(key)];
                ac = {e->pos(), ac.count + 1};
            }
        }
    }
}
