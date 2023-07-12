//
// Created by xhy on 2023/7/11.
//
#include "config.h"
#include "color.h"

namespace {

    QImage *bg_img_{nullptr};
    QImage *empty_img_{nullptr};

}

region_pos cfg::c2r(const bl::chunk_pos &ch) {
    auto cx = ch.x < 0 ? ch.x - cfg::RW + 1 : ch.x;
    auto cz = ch.z < 0 ? ch.z - cfg::RW + 1 : ch.z;
    return region_pos{cx / cfg::RW * cfg::RW, cz / cfg::RW * cfg::RW, ch.dim};
}

void cfg::initColorTable() {
    bl::init_biome_color_palette_from_file(R"(C:\Users\xhy\dev\bedrock-level\data\colors\biome.json)");
    bl::init_block_color_palette_from_file(R"(C:\Users\xhy\dev\bedrock-level\data\colors\block.json)");


    //init image
    bg_img_ = new QImage(cfg::RW << 4, cfg::RW << 4, QImage::Format_RGBA8888);
    empty_img_ = new QImage(cfg::RW << 4, cfg::RW << 4, QImage::Format_RGBA8888);
    const int BW = cfg::RW << 4;
    for (int i = 0; i < BW; i++) {
        for (int j = 0; j < BW; j++) {
            const int arr[2]{cfg::BG_GRAY, cfg::BG_GRAY + 20};
            const int idx = (i / (cfg::RW * 8) + j / (cfg::RW * 8)) % 2;
            bg_img_->setPixelColor(i, j, QColor(arr[idx], arr[idx], arr[idx]));
            empty_img_->setPixelColor(i, j, QColor(0, 0, 0, 0));
        }
    }
}

QImage *cfg::BACKGROUND_IMAGE() {
    return bg_img_;
}

QImage *cfg::EMPTY_IMAGE() {
    return empty_img_;
}

QImage *cfg::BACKGROUND_IMAGE_COPY() {
    auto *img = new QImage(cfg::RW << 4, cfg::RW << 4, QImage::Format_RGBA8888);
    if (bg_img_)
        memcpy(img->bits(), bg_img_->bits(), img->byteCount());
    return img;
}



