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
    return TerrainAtlasLayout::tileSize(terrain_render_config::region_blocks, blocks_per_texel);
}

TerrainTileSnapshot TerrainTiles::buildSnapshot(const ChunkRegion& region, const region_pos& pos, int blocks_per_texel, int layer) {
    return TerrainTileSnapshot{pos,
                               std::max(1, blocks_per_texel),
                               layer,
                               colorTile(region, blocks_per_texel, layer != 0),
                               heightTile(region, blocks_per_texel),
                               materialTile(region, blocks_per_texel, pos.x, pos.z, pos.dim, layer)};
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

QImage TerrainTiles::heightTile(const ChunkRegion& region, int blocks_per_texel) {
    const int texels = texelsPerRegion(blocks_per_texel);
    const int bp = std::max(1, blocks_per_texel);
    QImage tile(texels, texels, QImage::Format_RGBA8888);
    for (int z = 0; z < texels; ++z) {
        auto* row = tile.scanLine(z);
        const int block_z = std::min(z * bp, terrain_render_config::region_blocks - 1);
        for (int x = 0; x < texels; ++x) {
            const int block_x = std::min(x * bp, terrain_render_config::region_blocks - 1);
            const auto& info = region.tips_info_[block_x][block_z];
            const int solid = terrain_height_codec::encodeHeight(info.solid_height);
            const int top = terrain_height_codec::encodeHeight(info.height);
            row[x * 4] = static_cast<uchar>(terrain_height_codec::lowByte(solid));
            row[x * 4 + 1] = static_cast<uchar>(terrain_height_codec::highByte(solid));
            row[x * 4 + 2] = static_cast<uchar>(terrain_height_codec::lowByte(top));
            row[x * 4 + 3] = static_cast<uchar>(terrain_height_codec::highByte(top));
        }
    }
    return tile;
}

QImage TerrainTiles::materialTile(const ChunkRegion& region, int blocks_per_texel, int regionX, int regionZ, int dimension, int layer) {
    const int texels = texelsPerRegion(blocks_per_texel);
    const int bp = std::max(1, blocks_per_texel);
    const std::uint16_t regionHash =
        terrain_material_flags::regionHash(regionX / constant::RW, regionZ / constant::RW, dimension, layer, bp);
    QImage tile(texels, texels, QImage::Format_RGBA8888);
    for (int z = 0; z < texels; ++z) {
        auto* row = tile.scanLine(z);
        const int block_z = std::min(z * bp, terrain_render_config::region_blocks - 1);
        for (int x = 0; x < texels; ++x) {
            const int block_x = std::min(x * bp, terrain_render_config::region_blocks - 1);
            const auto& info = region.tips_info_[block_x][block_z];
            row[x * 4] = static_cast<uchar>(static_cast<unsigned char>(info.biome));
            row[x * 4 + 1] = static_cast<uchar>(terrain_material_flags::forTips(info));
            if (!region.chunk_bit_map_.test((block_x / 16) * constant::RW + block_z / 16))
                row[x * 4 + 1] |= terrain_material_flags::missing_chunk;
            // B/A are slot metadata, sampled only at the tile origin.
            row[x * 4 + 2] = static_cast<uchar>(x == 0 && z == 0 ? regionHash >> 8 : 0);
            row[x * 4 + 3] = static_cast<uchar>(x == 0 && z == 0 ? regionHash & 0xFFu : 0);
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
