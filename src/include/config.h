#ifndef CONFIG_H
#define CONFIG_H

#include <QColor>
#include <QImage>
#include <cstdint>
#include <bitset>
#include "bedrock_key.h"

typedef bl::chunk_pos region_pos;

struct cfg {
    // 软件基本信息
    const static std::string SOFTWARE_NAME;
    const static std::string SOFTWARE_VERSION;

    // 不可配置的
    const static int BG_GRAY;
    const static int GRID_WIDTH;


    static constexpr uint8_t RW = 8u;  //(1<<w)
    static constexpr uintptr_t LOW_IMG_SCALE = 64;
    // 配置文件下面是可配置的
    static int SHADOW_LEVEL;
    static float ZOOM_SPEED;
    static int THREAD_NUM;
    static int REGION_CACHE_SIZE;
    static int EMPTY_REGION_CACHE_SIZE;
    static int MINIMUM_SCALE_LEVEL;
    static int MAXIMUM_SCALE_LEVEL;
    static int FONT_SIZE;
    static bool FANCY_TERRAIN_RENDER;



    // 其他配置(不对外开放接口)

    // 配置文件
    static std::string COLOR_THEME;
    const static std::string CONFIG_FILE_PATH;
    // 颜色文件
    const static std::string BLOCK_FILE_PATH;
    const static std::string BIOME_FILE_PATH;

    static region_pos c2r(const bl::chunk_pos &ch);

    static void initColorTable();

    static void initConfig();

    static QImage INIT_REGION_IMG(const std::bitset<cfg::RW * cfg::RW> &bitmap);

    static QImage *UNLOADED_REGION_IMAGE();

    static QImage *NULL_REGION_IMAGE();

    static QImage *EMPTY_REGION_IMAGE();

    static QString VERSION_STRING();
};

#endif  // CONFIG_H
