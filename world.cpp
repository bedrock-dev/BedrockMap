#include "world.h"

#include <QtDebug>
#include <array>

#include "asynclevelloader.h"
#include "color.h"
#include "lrucache.h"

namespace {
QVector<QRgb> biome_color_table{};
QImage *EMPTY_IMAGE{nullptr};

}  // namespace

world::world() {
    this->top_biome_image_cache_ = new LRUCache<bl::chunk_pos, QImage>(65536);
    this->top_block_image_cache_ = new LRUCache<bl::chunk_pos, QImage>(65536);
    initBiomeColorTable();
}

bool world::init(const std::string &level_path) {
    return this->level_loader_.init(level_path);
}

QImage *world::topBiome(const bl::chunk_pos &p) {
    QImage *res{nullptr};

    if (this->top_biome_image_cache_->get(p, res)) {
        if (res) {
            return res;
        } else {
            return EMPTY_IMAGE;
        }
    }

    auto *ch = this->level_loader_.get(p);

    if (ch) {
        QImage *image = new QImage(16, 16, QImage::Format_Indexed8);
        image->setColorTable(biome_color_table);
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int idx = static_cast<uint>(ch->get_top_biome(i, j));
                if (idx >= 0 && idx < biome_color_table.size()) {
                    image->setPixel(i, j, idx);
                }
            }
        }

        this->top_biome_image_cache_->put(p, image);
        return image;
    } else {
        return EMPTY_IMAGE;
    }
}

void world::initBiomeColorTable() {
    bl::init_biome_color_palette_from_file(
        R"(C:\Users\xhy\dev\bedrock-level\data\colors\biome.json)");
    biome_color_table.resize(bl::biome::LEN + 1);
    for (int i = 0; i < bl::biome::none; i++) {
        auto color = bl::get_biome_color(static_cast<bl::biome>(i));
        biome_color_table[i] = qRgb(color.r, color.g, color.b);
    }

    EMPTY_IMAGE = new QImage(16 * 6, 16 * 6, QImage::Format_Indexed8);
    EMPTY_IMAGE->setColorTable(biome_color_table);
    EMPTY_IMAGE->fill(3);
}
