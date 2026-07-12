#ifndef MAP_TILE_H
#define MAP_TILE_H
#include <qcolor.h>
#include <qimage.h>
#include <qrgb.h>

#include <QImage>
#include <bitset>

#include "config.h"

// map tile, to fill the mapview
class MapTile {
   public:
    MapTile() = delete;
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
