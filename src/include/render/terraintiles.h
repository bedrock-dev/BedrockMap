#ifndef BEDROCKMAP_TERRAINTILES_H
#define BEDROCKMAP_TERRAINTILES_H

#include <QImage>
#include <QPoint>
#include <QRect>
#include <algorithm>
#include <cstdint>

#include "loader/chunkregion.h"

/// Tunables shared by every terrain renderer: atlas layout, region footprint,
/// request budget and tile-generation budget.
namespace terrain_render_config {
    inline constexpr int atlas_size_pixels = 4096;
    /// Texels one region occupies at one texel per block, i.e. its block edge.
    inline constexpr int region_blocks = 128;
    inline constexpr int max_blocks_per_texel = constant::MAX_TERRAIN_BLOCKS_PER_TEXEL;
    inline constexpr int overview_texture_pixels = 4096;
    /// Bound each eager scan pass; larger windows are expanded incrementally.
    inline constexpr int max_requested_regions = 16384;
    inline constexpr int max_tiles_per_poll = 32;
    /// Resident 128x128 slices in the color/surface texture arrays.
    /// 768 slices cost 96 MiB across the two arrays and cover a 4K viewport at
    /// one screen pixel per texel, including the request margin.
    inline constexpr int tile_array_layers = 768;
    /// Slices for the high-detail map: 256 MiB, and the slice limit of a D3D11
    /// texture array. Devices with a lower limit are clamped at runtime.
    inline constexpr int high_detail_tile_array_layers = 2048;
    /// Item pixels one texel must cover before the next coarser level is used.
    /// The high-detail map keeps full-resolution tiles, and the shading that
    /// needs them, down to half-pixel texels when the slice budget allows.
    /// Below that, unfiltered sampling skips most texels and shimmers.
    inline constexpr double balanced_texel_pixels = 1.0;
    inline constexpr double high_detail_texel_pixels = 0.5;
    /// Number of coarser LODs prefetched ahead of the active atlas.
    inline constexpr int prefetch_lod_levels = 2;
    /// The final detailed LOD before switching to the sampled overview.
    inline constexpr int overview_blocks_per_texel = 32;
    /// Page-table slices, one per level from 1 to max_blocks_per_texel blocks
    /// per texel. terrain_common.glsl's MAX_LOD_INDEX is this minus one.
    inline constexpr int page_table_levels = 6;
    static_assert((1 << (page_table_levels - 1)) == max_blocks_per_texel, "one page-table slice per detailed level");
    [[nodiscard]] constexpr bool atlasHoldsWholeTiles() {
        for (int blocks_per_texel = 1; blocks_per_texel <= max_blocks_per_texel; blocks_per_texel *= 2)
            if (atlas_size_pixels % (region_blocks / blocks_per_texel) != 0) return false;
        return true;
    }
    static_assert(atlasHoldsWholeTiles(), "the atlas must hold a whole number of region tiles at every level");
    static_assert(atlas_size_pixels % region_blocks == 0, "the atlas must hold at least one region tile");
}  // namespace terrain_render_config

/// Inclusive rectangular window in region indices plus the requested content.
struct TerrainViewportRequest {
    QRect regions;
    int dimension{0};
    int layer{0};

    [[nodiscard]] bool operator==(const TerrainViewportRequest& other) const {
        return regions == other.regions && dimension == other.dimension && layer == other.layer;
    }
    [[nodiscard]] bool operator!=(const TerrainViewportRequest& other) const { return !(*this == other); }

    [[nodiscard]] bool canReuseWith(const TerrainViewportRequest& next) const {
        return dimension == next.dimension && layer == next.layer && regions.isValid() && regions.intersects(next.regions);
    }
};

/// Fixed-point height encoding shared with terrain_common.glsl.
///
/// A surface texel packs solid height (bits 0-9), top height (bits 10-19),
/// biome id (bits 20-27) and material flags (bits 28-31). Code zero is void;
/// non-void heights are biased by 512 and cover -127 through 511.
/// A packed value of zero is reserved for "not uploaded yet" (cleared layer).
namespace terrain_height_codec {
    inline constexpr int height_bits{10};
    inline constexpr int max_code{(1 << height_bits) - 1};
    inline constexpr int height_bias{512};
    inline constexpr int void_code{0};
    /// Solid code stored under a void top, so a delivered void texel never
    /// packs to zero. No real column reaches this height.
    inline constexpr int void_marker{max_code};
    /// Height the shader substitutes for `void_code`; below any real column.
    inline constexpr int void_height{-1000};

    [[nodiscard]] inline int encodeHeight(qint16 height) {
        if (height <= -128) return void_code;
        return std::clamp(static_cast<int>(height) + height_bias, 1, max_code);
    }

    static_assert(height_bits * 2 + 8 + 4 == 32);
}  // namespace terrain_height_codec

/// Material flag bits packed into the high nibble of the surface texel.
namespace terrain_material_flags {
    inline constexpr unsigned char tint_mask = 0x3;
    inline constexpr unsigned char water_tint = 1;
    inline constexpr unsigned char grass_tint = 2;
    inline constexpr unsigned char leaves_tint = 3;
    inline constexpr unsigned char water_overlay = 1u << 2;
    /// Missing chunks use the live checker palette rather than cached colour.
    inline constexpr unsigned char missing_chunk = 1u << 3;

    [[nodiscard]] inline unsigned char forTips(const BlockTipsInfo& info) {
        unsigned char flags = 0;
        switch (static_cast<bl::biome_tint_kind>(info.gpu_tint_kind)) {
            case bl::biome_tint_kind::water:
                flags = water_tint;
                break;
            case bl::biome_tint_kind::grass:
                flags = grass_tint;
                break;
            case bl::biome_tint_kind::leaves:
                flags = leaves_tint;
                break;
            default:
                break;
        }
        if (info.gpu_water_overlay) flags |= water_overlay;
        return flags;
    }
}  // namespace terrain_material_flags

/// Turns a baked ChunkRegion into terrain tiles. One texel covers
/// `blocks_per_texel` blocks and samples the low-coordinate block of each
/// footprint.
/// The bake itself is never regenerated per level - only this sampling changes.
class TerrainTiles final {
   public:
    [[nodiscard]] static TerrainTileSnapshot buildSnapshot(const ChunkRegion& region, const region_pos& pos, int blocks_per_texel,
                                                           int layer);
    /// Raw, untinted palette colour (the shader applies the biome tint); the
    /// biome layer uses the categorically coloured biome bake instead.
    [[nodiscard]] static QImage colorTile(const ChunkRegion& region, int blocks_per_texel, bool biome_layer);
    /// Packed solid/top heights, biome id and material flags. Tint and water
    /// flags are omitted when `material_tint` is false (biome layer).
    [[nodiscard]] static QImage surfaceTile(const ChunkRegion& region, int blocks_per_texel, bool material_tint = true);
    /// Three rows of tint colours indexed by biome id: water, leaves, grass.
    [[nodiscard]] static QImage biomePaletteImage();
    /// Texels one region occupies at a level, i.e. region_blocks / level.
    [[nodiscard]] static int texelsPerRegion(int blocks_per_texel);
};

#endif  // BEDROCKMAP_TERRAINTILES_H
