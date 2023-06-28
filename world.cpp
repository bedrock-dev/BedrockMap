#include "world.h"

#include <QVector3D>
#include <QtDebug>
#include <algorithm>
#include <array>
#include <cmath>

#include "asynclevelloader.h"
#include "color.h"
#include "lrucache.h"

namespace {
QVector<QRgb> biome_color_table{};

QVector<QRgb> height_color_table{};

QImage *EMPTY_IMAGE{nullptr};

QImage *SLIME_IMAGE{nullptr};

QRgb viridisColormap(float k) {
    using vec3 = QVector3D;
    auto x = std::clamp(k, 0.0f, 1.0f);
    vec3 v31 = vec3(1.0, x, x * x);
    vec3 v32 = v31 * v31.z() * x;

    const vec3 krp1 = vec3(0.039908, 0.452562, 69.9753);
    const vec3 krp2 = vec3(-0.248869, 0.610199, 12.2464);
    const vec3 kr1 = vec3(0.269227, 0.47499, -2.9514);
    const vec3 kr2 = vec3(5.37388, -2.13208, 0);

    auto xx = vec3::dotProduct(kr1, v31) + vec3::dotProduct(kr2, v32) + krp1.x() * std::exp(-krp1.z() * pow(x - krp1.y(), 2)) +
              krp2.x() * std::exp(-krp2.z() * pow(x - krp2.y(), 2));

    const vec3 kgp1 = vec3(-0.106982, 0.461882, 7.16574);
    const vec3 kg1 = vec3(0.0249171, 1.69456, -0.801966);
    auto yy = vec3::dotProduct(kg1, v31) + kgp1.x() * std::exp(-kgp1.z() * pow(x - kgp1.y(), 2));

    const vec3 kbp1 = vec3(-0.806146, 0.820469, 11.9012);
    const vec3 kbp2 = vec3(0.206473, -0.596378, -1.70402);
    const vec3 kb1 = vec3(-0.048423, 0.729215, -2.04136);
    const vec3 kb2 = vec3(-19.2609, 51.8115, -46.3694);
    auto zz = vec3::dotProduct(kb1, v31) + vec3::dotProduct(kb2, v32) + kbp1.x() * std::exp(-kbp1.z() * pow(x - kbp1.y(), 2)) +
              kbp2.x() * std::exp(-kbp2.z() * pow(x - kbp2.y(), 2));

    return qRgb(std::clamp(static_cast<int>(xx * 255), 0, 255), std::clamp(static_cast<int>(yy * 255), 0, 255),
                std::clamp(static_cast<int>(zz * 255), 0, 255));
}

}  // namespace

world::world() {
    initBiomeColorTable();
    this->top_biome_image_cache_ = new QCache<bl::chunk_pos, QImage>(16384);
    this->height_image_cache_ = new QCache<bl::chunk_pos, QImage>(16384);
}

QFuture<bl::chunk *> world::getChunkDirect(const bl::chunk_pos &p) { return this->level_loader_.getChunkDirect(p); }

bool world::init(const std::string &level_path) {
    auto r = this->level_loader_.init(level_path);

    this->loadered_ = r;
    return r;
}

void world::close() {
    if (!this->loadered_) {
        return;
    }

    this->top_biome_image_cache_->clear();
    this->height_image_cache_->clear();
    this->loadered_ = false;
    this->level_loader_.close();
}

QImage *world::topBiome(const bl::chunk_pos &p) {
    if (!this->loadered_) return EMPTY_IMAGE;

    auto *img = this->top_biome_image_cache_->operator[](p);
    if (img) return img;  // hit

    bool null_chunk{false};
    auto *ch = this->level_loader_.get(p, null_chunk);
    if (null_chunk) {
        return EMPTY_IMAGE;
    }

    if (ch) {
        auto *image = new QImage(16, 16, QImage::Format_Indexed8);
        image->setColorTable(biome_color_table);
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int idx = static_cast<uint>(ch->get_top_biome(i, j));
                if (idx >= 0 && idx < biome_color_table.size()) {
                    image->setPixel(i, j, idx);
                }
            }
        }
        // only cache non empty chunk
        this->top_biome_image_cache_->insert(p, image);
        return image;
    } else {
        return EMPTY_IMAGE;
    }
}

QImage *world::height(const bl::chunk_pos &p) {
    if (!this->loadered_) return EMPTY_IMAGE;

    auto *img = this->height_image_cache_->operator[](p);
    if (img) return img;  // hit

    bool null_chunk{false};
    auto *ch = this->level_loader_.get(p, null_chunk);
    if (null_chunk) {
        return EMPTY_IMAGE;
    }

    if (ch) {
        auto *image = new QImage(16, 16, QImage::Format_Indexed8);
        image->setColorTable(height_color_table);
        for (int i = 0; i < 16; i++) {
            for (int j = 0; j < 16; j++) {
                int idx = ch->get_height(i, j) + 64;
                if (idx >= 0 && idx < height_color_table.size()) {
                    image->setPixel(i, j, idx);
                }
            }
        }
        // only cache non empty chunk
        this->height_image_cache_->insert(p, image);
        return image;
    } else {
        return EMPTY_IMAGE;
    }
}

QImage *world::slimeChunk(const bl::chunk_pos &p) { return p.is_slime() ? SLIME_IMAGE : EMPTY_IMAGE; }

void world::initBiomeColorTable() {
    bl::init_biome_color_palette_from_file(
        R"(C:\Users\xhy\dev\bedrock-level\data\colors\biome.json)");
    biome_color_table.resize(bl::biome::LEN + 1);
    for (int i = 0; i < bl::biome::none; i++) {
        auto color = bl::get_biome_color(static_cast<bl::biome>(i));
        biome_color_table[i] = qRgb(color.r, color.g, color.b);
    }

    height_color_table.resize(384);
    for (int i = 0; i < 384; i++) {
        height_color_table[i] = viridisColormap(i / 384.0);
    }
    // empty
    EMPTY_IMAGE = new QImage(16, 16, QImage::Format_Indexed8);
    EMPTY_IMAGE->setColor(0, qRgba(240, 240, 240, 255));
    EMPTY_IMAGE->setColor(1, qRgba(210, 210, 210, 255));
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            EMPTY_IMAGE->setPixel(i, j, (i / 8 + j / 8) % 2);
        }
    }

    // slime

    SLIME_IMAGE = new QImage(16, 16, QImage::Format_Indexed8);
    SLIME_IMAGE->setColor(0, qRgb(87, 157, 66));
    SLIME_IMAGE->fill(0);
}
