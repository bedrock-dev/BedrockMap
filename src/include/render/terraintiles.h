#ifndef BEDROCKMAP_TERRAINTILES_H
#define BEDROCKMAP_TERRAINTILES_H

#include <QImage>
#include <QPoint>
#include <QRect>
#include <algorithm>
#include <cstdint>

#include "loader/regionbake.h"

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
    /// Resident 128x128 slices in the color/height/material texture arrays.
    /// 768 slices cost ~144 MiB across the three arrays and cover a 4K viewport
    /// at one screen pixel per texel, including boundary groups and margins.
    inline constexpr int tile_array_layers = 768;
    /// Number of coarser LODs prefetched ahead of the active atlas.
    inline constexpr int prefetch_lod_levels = 2;
    /// The final detailed LOD before switching to the sampled overview.
    inline constexpr int overview_blocks_per_texel = 32;
    [[nodiscard]] constexpr bool atlasHoldsWholeTiles() {
        for (int blocks_per_texel = 1; blocks_per_texel <= max_blocks_per_texel; blocks_per_texel *= 2)
            if (atlas_size_pixels % (region_blocks / blocks_per_texel) != 0) return false;
        return true;
    }
    static_assert(atlasHoldsWholeTiles(), "the atlas must hold a whole number of region tiles at every level");
    static_assert(atlas_size_pixels % region_blocks == 0, "the atlas must hold at least one region tile");
}  // namespace terrain_render_config

/// Pure atlas geometry shared by renderers with different atlas sizes.
class TerrainAtlasLayout {
   public:
    [[nodiscard]] static int tileSize(int regionBlocks, int blocksPerTexel) {
        return std::max(1, regionBlocks / std::max(1, blocksPerTexel));
    }

    [[nodiscard]] static int slotsPerSide(int atlasPixels, int regionBlocks, int blocksPerTexel) {
        return std::max(1, atlasPixels / tileSize(regionBlocks, blocksPerTexel));
    }

    [[nodiscard]] static int capacity(int atlasPixels, int regionBlocks, int blocksPerTexel) {
        const int side = slotsPerSide(atlasPixels, regionBlocks, blocksPerTexel);
        return side * side;
    }

    [[nodiscard]] static int slotFor(QPoint regionChunkOrigin, int regionBlocks, int side) {
        const int chunksPerRegion = std::max(1, regionBlocks / 16);
        const int regionX = floorDiv(regionChunkOrigin.x(), chunksPerRegion);
        const int regionZ = floorDiv(regionChunkOrigin.y(), chunksPerRegion);
        return floorMod(regionZ, side) * side + floorMod(regionX, side);
    }

    [[nodiscard]] static QPoint tileOrigin(int slot, int side, int tileSize) {
        return QPoint((slot % side) * tileSize, (slot / side) * tileSize);
    }

   private:
    [[nodiscard]] static int floorDiv(int value, int divisor) {
        const int quotient = value / divisor;
        return value % divisor < 0 ? quotient - 1 : quotient;
    }

    [[nodiscard]] static int floorMod(int value, int divisor) {
        return ((value % divisor) + divisor) % divisor;
    }
};

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

/// Fixed-point height encoding shared with terrainarray.frag.
///
/// One RGBA8888 texel carries two heights as 16-bit little-endian codes: the
/// solid surface in R/G and the top surface in B/A. `void_code` marks a column
/// the bake never filled (ChunkRegion's -128 sentinel), which the shader reads
/// as `void_height`, far below any real surface. Fixed point rather than a
/// float texture keeps every height exact and the atlas a quarter of the size,
/// and nearest filtering keeps the codes from interpolating into nonsense.
namespace terrain_height_codec {
    inline constexpr int height_bias{512};
    inline constexpr int void_code{0};
    /// Height the shader substitutes for `void_code`; below any real column.
    inline constexpr int void_height{-1000};
    /// Highest code the 16-bit pair can carry.
    inline constexpr int max_code{65535};

    [[nodiscard]] inline int encodeHeight(qint16 height) {
        if (height <= -128) return void_code;
        return std::clamp(static_cast<int>(height) + height_bias, 1, max_code);
    }

    [[nodiscard]] inline qint16 decodeHeight(int code) {
        return code == void_code ? static_cast<qint16>(void_height) : static_cast<qint16>(code - height_bias);
    }

    [[nodiscard]] inline int highByte(int code) { return (code >> 8) & 0xFF; }
    [[nodiscard]] inline int lowByte(int code) { return code & 0xFF; }
}  // namespace terrain_height_codec

/// Material flag bits carried in the material atlas' green channel, shared with
/// terrainarray.frag.
namespace terrain_material_flags {
    inline constexpr unsigned char water_tint = 1u << 0;
    inline constexpr unsigned char grass_tint = 1u << 1;
    inline constexpr unsigned char leaves_tint = 1u << 2;
    inline constexpr unsigned char water_overlay = 1u << 3;
    inline constexpr unsigned char terrain_sample = 1u << 4;
    /// Missing chunks use the live checker palette rather than cached colour.
    inline constexpr unsigned char missing_chunk = 1u << 5;
    /// Hash stored in material B/A. The shader recomputes it from the sampled
    /// world position, so stale atlas contents cannot be mistaken for live data
    /// after a pan, layer change, dimension change or LOD rebuild.
    [[nodiscard]] inline std::uint16_t regionHash(int regionX, int regionZ, int dimension, int layer, int blocksPerTexel) {
        std::uint32_t value = static_cast<std::uint16_t>(regionX) * 40503u;
        value ^= static_cast<std::uint16_t>(regionZ) * 60307u;
        value ^= static_cast<std::uint16_t>(dimension) * 27581u;
        value ^= static_cast<std::uint16_t>(layer) * 12347u;
        value ^= static_cast<std::uint16_t>(blocksPerTexel) * 54311u;
        value ^= value >> 8;
        value &= 0xFFFFu;
        return static_cast<std::uint16_t>(value == 0 ? 1 : value);
    }

    [[nodiscard]] inline unsigned char forTips(const BlockTipsInfo& info) {
        unsigned char flags = info.height > -128 ? terrain_sample : 0;
        switch (static_cast<bl::biome_tint_kind>(info.gpu_tint_kind)) {
            case bl::biome_tint_kind::water:
                flags |= water_tint;
                break;
            case bl::biome_tint_kind::grass:
                flags |= grass_tint;
                break;
            case bl::biome_tint_kind::leaves:
                flags |= leaves_tint;
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
    /// Solid surface in R/G and top surface in B/A, see terrain_height_codec.
    [[nodiscard]] static QImage heightTile(const ChunkRegion& region, int blocks_per_texel);
    /// R = biome id, G = material flags (tint, overlay, terrain, missing chunk).
    /// B/A = the region hash used to reject stale atlas slots.
    [[nodiscard]] static QImage materialTile(const ChunkRegion& region, int blocks_per_texel, int regionX, int regionZ, int dimension,
                                             int layer);
    /// Three rows of tint colours indexed by biome id: water, leaves, grass.
    [[nodiscard]] static QImage biomePaletteImage();
    /// Texels one region occupies at a level, i.e. region_blocks / level.
    [[nodiscard]] static int texelsPerRegion(int blocks_per_texel);
};

#endif  // BEDROCKMAP_TERRAINTILES_H
