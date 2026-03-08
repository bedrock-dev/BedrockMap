#include "maptile.h"

#include <qcolor.h>
#include <qimage.h>
#include <qrgb.h>

#include <cstddef>

#include "config.h"
#include "maptile.h"

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
    static QImage img = createQuadChessTile(2, c1, c2, cfg::RW << 3);
    return img;
}

QImage &MapTile::NULL_REGION_TILE() {
    static auto c1 = QColor(20, 20, 20).rgb();
    static auto c2 = QColor(40, 40, 40).rgb();
    static QImage img = createQuadChessTile(2, c1, c2, cfg::RW << 3);
    return img;
}

QImage MapTile::CREATE_REGION_TILE(std::bitset<cfg::RW * cfg::RW> chunk_bit_map, bool fill) {
    static auto color = QColor(cfg::TRANSSPARENT_VOID_COLOR.c_str()).rgb();
    auto img = NULL_REGION_TILE().copy();
    if (fill) {
        int gridSize = img.width() / cfg::RW;

        for (int y = 0; y < img.height(); ++y) {
            QRgb *line = (QRgb *)img.scanLine(y);
            int gridY = y / gridSize;
            for (int x = 0; x < img.width(); ++x) {
                int gridX = x / gridSize;
                if (chunk_bit_map[gridX * cfg::RW + gridY]) {
                    line[x] = color;
                }
            }
        }
    }
    return img;
}