#ifndef MAP_TILE_H
#define MAP_TILE_H
#include <qcolor.h>
#include <qimage.h>
#include <qrgb.h>

#include <QImage>
#include <bitset>

#include "chunk.h"
#include "config.h"
#include "renderfilterdialog.h"
#include "resourcemanager.h"

class AsyncLevelLoader;

// map tile, to fill the mapview
class MapTile {
   public:
    MapTile() = delete;

    // render a single chunk's terrain colour + height/biome tips into the region.
    // fast-path: if filter is default (only excludes air+unknown), uses get_top_y().
    // slow-path: scans down from the height-map to find matching blocks.
    static void bakeChunkTerrain(bl::chunk *ch, const MapFilter *filter, int rw, int rh, ChunkRegion *region);

    static void bakeChunkActors(bl::chunk *ch, const MapFilter *filter, ChunkRegion *region);

    // render passes (called after all chunks in a region are baked)
    static void renderStyle0(ChunkRegion *region, int IMG_WIDTH);
    static void renderStyle1(ChunkRegion *region, int IMG_WIDTH);
    static void renderStyle2(ChunkRegion *region, int IMG_WIDTH, AsyncLevelLoader *loader, const bl::chunk_pos &region_pos);

   private:
    static void renderTerrainColumn(ChunkRegion *region, bl::chunk *ch, const MapFilter *filter, int rw, int rh, int chx, int chz, int y,
                                    int y_solid);

   public:
    // create a chessboard image(witdt * scale)^2 with color c1 and c2
    static QImage createQuadChessTile(int width, const QRgb &c1, const QRgb &c2, int scale = 1);

    // create a tile according to the bit map
    template <size_t W>
    static QImage createBitMapTile(std::bitset<W * W> chunk_bit_map, const QRgb &c0, const QRgb &c1, int scale = 1) {
        int scaledWidth = W * scale;
        QImage image(scaledWidth, scaledWidth, QImage::Format_RGB32);

        std::vector<int> rowBits(scaledWidth);
        for (int y = 0; y < scaledWidth; ++y) {
            rowBits[y] = (y / scale) * W;
        }

        for (int y = 0; y < scaledWidth; ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            int rowStart = rowBits[y];
            for (int x = 0; x < scaledWidth; ++x) {
                int bitIndex = rowStart + (x / scale);
                line[x] = chunk_bit_map[bitIndex] ? c0 : c1;
            }
        }
        return image;
    }

   public:
    // region that are not loaed
    static QImage &UNLOADED_REGION_TILE();
    // reegion that are loaded but have no valid chunk
    static QImage &NULL_REGION_TILE();

    static QImage CREATE_REGION_TILE(const std::bitset<constant::RW * constant::RW> &region_bit_map, bool fillChunk);

    static QImage *CREATE_REGION_THUMBNAIL(std::bitset<constant::RW * constant::RW> &region_bit_map);
};

#endif
