//
// Created by xhy on 2023/7/11.
//
#include "config.h"
#include "color.h"
#include "json/json.hpp"
#include <QtDebug>
#include <fstream>


namespace {
    QImage *bg_img_{nullptr};
    QImage *empty_img_{nullptr};
    QImage *bg_{nullptr};
}  // namespace



int cfg::REGION_CACHE_SIZE = 4096;
int cfg::EMPTY_REGION_CACHE_SIZE = 16384;
int cfg::THREAD_NUM = 8;
std::string  cfg::COLOR_THEME = "light";
const std::string  cfg::SOFTWARE_NAME = "BedrockMap";
const std::string  cfg::SOFTWARE_VERSION = "v0.1";
#ifdef  QT_DEBUG
const std::string cfg::CONFIG_FILE_PATH = R"(C:\Users\xhy\dev\Qt\BedrockMap\config.json)";
const std::string cfg::BLOCK_FILE_PATH = R"(C:\Users\xhy\dev\bedrock-level\data\colors\block_color.json)";
const std::string cfg::BIOME_FILE_PATH = R"(C:\Users\xhy\dev\bedrock-level\data\colors\biome_color.json)";
#else
const std::string cfg::CONFIG_FILE_PATH = "config.json";
const std::string cfg::BLOCK_FILE_PATH = "block_color.json";
const std::string cfg::BIOME_FILE_PATH = "biome_color.json";
#endif
float cfg::ZOOM_SPEED = 1.2;

region_pos cfg::c2r(const bl::chunk_pos &ch) {
    auto cx = ch.x < 0 ? ch.x - cfg::RW + 1 : ch.x;
    auto cz = ch.z < 0 ? ch.z - cfg::RW + 1 : ch.z;
    return region_pos{cx / cfg::RW * cfg::RW, cz / cfg::RW * cfg::RW, ch.dim};
}

void cfg::initColorTable() {
    qDebug() << "Block color path is: " << BLOCK_FILE_PATH.c_str();
    qDebug() << "Biome color path is: " << BIOME_FILE_PATH.c_str();

    bl::init_biome_color_palette_from_file(cfg::BIOME_FILE_PATH);
    bl::init_block_color_palette_from_file(cfg::BLOCK_FILE_PATH);

    // init image
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

    bg_ = new QImage(8, 8, QImage::Format_RGBA8888);
    bg_->fill(QColor(0, 0, 0, 0));
    std::vector<std::string> fills{
            "XXXXXXXX",  //
            "XXXXXXXX",  //
            "X  XX  X",  //
            "X  XX  X",  //
            "XXX  XXX",  //
            "XX    XX",  //
            "XX    XX",  //
            "XX XX XX",  //
    };
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (fills[i][j] == ' ') bg_->setPixelColor(i, j, QColor(31, 138, 112, 160));
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

QImage *cfg::BG() { return bg_; }

void cfg::initConfig() {

    qDebug() << "Configuration path is " << CONFIG_FILE_PATH.c_str();

    try {

        nlohmann::json j;
        std::ifstream f(CONFIG_FILE_PATH);
        if (!f.is_open()) {
            qWarning() << "Can not find config file.";
        } else {
            f >> j;
            COLOR_THEME = j["theme"].get<std::string>();
            REGION_CACHE_SIZE = j["region_cache_size"].get<int>();
            EMPTY_REGION_CACHE_SIZE = j["empty_region_cache_size"].get<int>();
            THREAD_NUM = j["background_thread_number"].get<int>();
        }

    } catch (std::exception &e) {
        qWarning() << "Invalid config file format" << e.what();
    }
    qInfo() << "Region cache size: " << REGION_CACHE_SIZE;
    qInfo() << "Empty region cache size: " << EMPTY_REGION_CACHE_SIZE;
    qInfo() << "Background thread number: " << THREAD_NUM;
    qInfo() << "Theme: " << COLOR_THEME.c_str();

    if (REGION_CACHE_SIZE < 128) {
        REGION_CACHE_SIZE = 128;
        qWarning() << "Invalid  Region cache size, reset it to 128";
    }

    if (EMPTY_REGION_CACHE_SIZE < 4096) {
        EMPTY_REGION_CACHE_SIZE = 4096;
        qWarning() << "Invalid Region cache size, reset it to 4096";
    }

    if (THREAD_NUM < 1) {
        THREAD_NUM = 1;
        qWarning() << "Invalid background thread number, reset it to 1";
    }
}
