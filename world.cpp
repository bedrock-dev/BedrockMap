#include "world.h"

#include <QVector3D>
#include <QtDebug>
#include <algorithm>

#include <cmath>

#include "asynclevelloader.h"
#include "color.h"
#include <QPainter>


namespace {
    QVector<QRgb> biome_color_table{};
    QVector<QRgb> height_color_table{};

    QColor EMPTY_COLOR_TABLE[2]{QColor(20, 20, 20), QColor(40, 40, 40)};

    QImage *empty_image_{nullptr};

//    QImage *EMPTY_IMAGE{nullptr};

    QImage *SLIME_IMAGE{nullptr};

    QRgb ViridisColorMap(float k) {
        using vec3 = QVector3D;
        auto x = std::clamp(k, 0.0f, 1.0f);
        vec3 v31 = vec3(1.0, x, x * x);
        vec3 v32 = v31 * v31.z() * x;

        const vec3 krp1 = vec3(0.039908, 0.452562, 69.9753);
        const vec3 krp2 = vec3(-0.248869, 0.610199, 12.2464);
        const vec3 kr1 = vec3(0.269227, 0.47499, -2.9514);
        const vec3 kr2 = vec3(5.37388, -2.13208, 0);

        auto xx = vec3::dotProduct(kr1, v31) + vec3::dotProduct(kr2, v32) +
                  krp1.x() * std::exp(-krp1.z() * pow(x - krp1.y(), 2)) +
                  krp2.x() * std::exp(-krp2.z() * pow(x - krp2.y(), 2));

        const vec3 kgp1 = vec3(-0.106982, 0.461882, 7.16574);
        const vec3 kg1 = vec3(0.0249171, 1.69456, -0.801966);
        auto yy = vec3::dotProduct(kg1, v31) + kgp1.x() * std::exp(-kgp1.z() * pow(x - kgp1.y(), 2));

        const vec3 kbp1 = vec3(-0.806146, 0.820469, 11.9012);
        const vec3 kbp2 = vec3(0.206473, -0.596378, -1.70402);
        const vec3 kb1 = vec3(-0.048423, 0.729215, -2.04136);
        const vec3 kb2 = vec3(-19.2609, 51.8115, -46.3694);
        auto zz = vec3::dotProduct(kb1, v31) + vec3::dotProduct(kb2, v32) +
                  kbp1.x() * std::exp(-kbp1.z() * pow(x - kbp1.y(), 2)) +
                  kbp2.x() * std::exp(-kbp2.z() * pow(x - kbp2.y(), 2));

        return qRgb(std::clamp(static_cast<int>(xx * 255), 0, 255), std::clamp(static_cast<int>(yy * 255), 0, 255),
                    std::clamp(static_cast<int>(zz * 255), 0, 255));
    }

}  // namespace

world::world() {
    initBiomeColorTable();
    this->layer_cache_ = new QCache<bl::chunk_pos, LayerCacheInfo>(cfg::LAYER_IMAGE_CACHE_SIZE);

}

QFuture<bl::chunk *> world::getChunkDirect(const bl::chunk_pos &p) { return this->level_loader_.getChunkDirect(p); }

bool world::init(const std::string &level_path) {
    auto r = this->level_loader_.init(level_path);
    this->loaded_ = r;
    return r;
}

void world::close() {
    if (!this->loaded_) {
        return;
    }

    this->layer_cache_->clear();
    this->loaded_ = false;
    this->level_loader_.close();
}

QImage *world::topBiome(const region_pos &p) {
    if (!this->loaded_ || !this->layer_cache_) return EMPTY_IMAGE();
    auto *info = this->layer_cache_->operator[](p);
    if (info) return info->biome;  // hit
    bool null_region{false};
    auto *region = this->level_loader_.getRegion(p, null_region, &this->render_filter_);
    if (null_region) {
        return EMPTY_IMAGE();
    }

    if (region) {
        auto *layer = LayerCacheInfo::fromRegion(region);
        this->layer_cache_->insert(p, layer);
        return layer->biome;

    } else {
        return EMPTY_IMAGE();
    }
}

QImage *world::topBlock(const region_pos &p) {
    if (!this->loaded_ || !this->layer_cache_) return EMPTY_IMAGE();
    auto *info = this->layer_cache_->operator[](p);
    if (info) return info->terrain;
    bool null_region{false};
    auto *region = this->level_loader_.getRegion(p, null_region, &this->render_filter_);
    if (null_region) {
        return EMPTY_IMAGE();
    }

    if (region) {
        auto *layer = LayerCacheInfo::fromRegion(region);
        this->layer_cache_->insert(p, layer);
        return layer->terrain;
    } else {
        return EMPTY_IMAGE();
    }
}

QImage *world::height(const region_pos &p) {
    return EMPTY_IMAGE();
}

//覆盖即可
QImage *world::slimeChunk(const bl::chunk_pos &p) { return p.is_slime() ? SLIME_IMAGE : nullptr; }

void world::initBiomeColorTable() {
    bl::init_biome_color_palette_from_file(R"(C:\Users\xhy\dev\bedrock-level\data\colors\biome.json)");
    bl::init_block_color_palette_from_file(R"(C:\Users\xhy\dev\bedrock-level\data\colors\block.json)");

    biome_color_table.resize(bl::biome::LEN + 1);

    for (int i = 0; i < bl::biome::none; i++) {
        auto color = bl::get_biome_color(static_cast<bl::biome>(i));
        biome_color_table[i] = qRgb(color.r, color.g, color.b);
    }

    height_color_table.resize(384);
    for (int i = 0; i < 384; i++) {
        height_color_table[i] = ViridisColorMap(i / 384.0);
    }

    // empty
    empty_image_ = new QImage(cfg::RW << 4, cfg::RW << 4, QImage::Format_RGBA8888);
    const int BW = cfg::RW << 4;
    for (int i = 0; i < BW; i++) {
        for (int j = 0; j < BW; j++) {
            const int arr[2]{cfg::BG_GRAY, cfg::BG_GRAY + 20};
            const int idx = (i / (cfg::RW * 8) + j / (cfg::RW * 8)) % 2;
            empty_image_->setPixelColor(i, j, QColor(arr[idx], arr[idx], arr[idx]));
        }
    }

    // slime
    SLIME_IMAGE = new QImage(16, 16, QImage::Format_Indexed8);
    SLIME_IMAGE->setColor(0, qRgba(87, 157, 66, 100));
    SLIME_IMAGE->fill(0);
}

QImage *world::EMPTY_IMAGE() {
    return empty_image_;
}

void world::clear_all_cache() {
    this->layer_cache_->clear();
    this->level_loader_.clear_all_cache();
}

LayerCacheInfo *LayerCacheInfo::fromRegion(chunk_region *r) {
    if (!r)return nullptr;
    auto *biome_image = new QImage(cfg::RW << 4, cfg::RW << 4, QImage::Format_RGBA8888);
    memcpy(biome_image->bits(), r->biome_bake_image_->bits(), biome_image->byteCount());
    auto *terrain_image = new QImage(cfg::RW << 4, cfg::RW << 4, QImage::Format_RGBA8888);
    memcpy(terrain_image->bits(), r->terrain_bake_image_->bits(), biome_image->byteCount());
    return new LayerCacheInfo{terrain_image, biome_image, r->tips_info_};
}

