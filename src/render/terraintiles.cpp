#include "render/terraintiles.h"

#include <QColor>
#include <algorithm>

#include "color.h"

namespace {
    QRgb backgroundAt(const ChunkRegion& region, int block_x, int block_z, QRgb void_color) {
        const int chunk_x = block_x / 16;
        const int chunk_z = block_z / 16;
        if (region.chunk_bit_map_.test(chunk_x * constant::RW + chunk_z)) return void_color;
        return (((block_x / 64) ^ (block_z / 64)) & 1) ? qRgb(40, 40, 40) : qRgb(20, 20, 20);
    }
}  // namespace

int TerrainTiles::texelsPerRegion(int blocks_per_texel) {
    return std::max(1, terrain_render_config::region_blocks / std::max(1, blocks_per_texel));
}

TerrainTileSnapshot TerrainTiles::buildSnapshot(const ChunkRegion& region, const region_pos& pos, int blocks_per_texel, int layer) {
    // The biome layer draws categorical biome colours that must not be tinted
    // again by the shader.
    QImage surface = surfaceTile(region, blocks_per_texel, layer == 0);
    return TerrainTileSnapshot{pos,        std::max(1, blocks_per_texel), layer, colorTile(region, blocks_per_texel, layer != 0), surface,
                               region.chunk_bit_map_.any()};
}

QImage TerrainTiles::colorTile(const ChunkRegion& region, int blocks_per_texel, bool biome_layer) {
    const int texels = texelsPerRegion(blocks_per_texel);
    const int bp = std::max(1, blocks_per_texel);
    const QRgb void_color = QColor(setting::current().voidMapColor).rgb();
    QImage tile(texels, texels, QImage::Format_RGBA8888);
    for (int z = 0; z < texels; ++z) {
        auto* row = tile.scanLine(z);
        const int block_z = std::min(z * bp, terrain_render_config::region_blocks - 1);
        for (int x = 0; x < texels; ++x) {
            const int block_x = std::min(x * bp, terrain_render_config::region_blocks - 1);
            const auto& info = region.tips_info_[block_x][block_z];
            QRgb color = backgroundAt(region, block_x, block_z, void_color);
            if (biome_layer) {
                if (info.gpu_biome_visible && info.height > -128) {
                    const auto biome = bl::get_biome_color(info.biome);
                    color = qRgba(biome.r, biome.g, biome.b, biome.a);
                }
            } else if (info.gpu_base_color != 0) {
                color = info.gpu_base_color;
            }
            row[x * 4] = static_cast<uchar>(qRed(color));
            row[x * 4 + 1] = static_cast<uchar>(qGreen(color));
            row[x * 4 + 2] = static_cast<uchar>(qBlue(color));
            row[x * 4 + 3] = 255;
        }
    }
    return tile;
}

QImage TerrainTiles::surfaceTile(const ChunkRegion& region, int blocks_per_texel, bool material_tint) {
    const int texels = texelsPerRegion(blocks_per_texel);
    const int bp = std::max(1, blocks_per_texel);
    QImage tile(texels, texels, QImage::Format_RGBA8888);
    for (int z = 0; z < texels; ++z) {
        auto* row = tile.scanLine(z);
        const int block_z = std::min(z * bp, terrain_render_config::region_blocks - 1);
        for (int x = 0; x < texels; ++x) {
            const int block_x = std::min(x * bp, terrain_render_config::region_blocks - 1);
            const auto& info = region.tips_info_[block_x][block_z];
            const int top = terrain_height_codec::encodeHeight(info.height);
            const int solid = top == terrain_height_codec::void_code ? terrain_height_codec::void_marker
                                                                     : terrain_height_codec::encodeHeight(info.solid_height);
            unsigned char flags = material_tint ? terrain_material_flags::forTips(info) : 0;
            if (!region.chunk_bit_map_.test((block_x / 16) * constant::RW + block_z / 16)) flags |= terrain_material_flags::missing_chunk;
            const std::uint32_t packed = static_cast<std::uint32_t>(solid) | (static_cast<std::uint32_t>(top) << 10) |
                                         (static_cast<std::uint32_t>(static_cast<unsigned char>(info.biome)) << 20) |
                                         (static_cast<std::uint32_t>(flags) << 28);
            row[x * 4] = static_cast<uchar>(packed & 0xFFu);
            row[x * 4 + 1] = static_cast<uchar>((packed >> 8) & 0xFFu);
            row[x * 4 + 2] = static_cast<uchar>((packed >> 16) & 0xFFu);
            row[x * 4 + 3] = static_cast<uchar>((packed >> 24) & 0xFFu);
        }
    }
    return tile;
}

QImage TerrainTiles::biomePaletteImage() {
    QImage image(256, 3, QImage::Format_RGBA8888);
    const bl::biome_tint_kind kinds[] = {bl::biome_tint_kind::water, bl::biome_tint_kind::leaves, bl::biome_tint_kind::grass};
    for (int row = 0; row < 3; ++row) {
        for (int id = 0; id < 256; ++id) {
            const auto color = bl::get_biome_tint_color(static_cast<bl::biome>(id), kinds[row]);
            image.setPixelColor(id, row, QColor(color.r, color.g, color.b, 255));
        }
    }
    return image;
}
