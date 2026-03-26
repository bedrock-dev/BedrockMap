#ifndef CONFIG_H
#define CONFIG_H

#include <qchar.h>
#include <qimage.h>
#include <sys/stat.h>

#include <QColor>
#include <QImage>
#include <cstdint>
#include <string>

#include "bedrock_key.h"

typedef bl::chunk_pos region_pos;

struct cfg {
    // base info
    const static std::string SOFTWARE_NAME;
    const static std::string SOFTWARE_VERSION;

    // Unconfigurable

    // 配置文件
    const static std::string CONFIG_FILE_PATH;  // 配置文件路径
    // 颜色表文件路径
    const static std::string BLOCK_FILE_PATH;
    const static std::string BIOME_FILE_PATH;
    const static QString SHADER_FILE_PATH;
    //
    static constexpr uint8_t RW = 8u;  //(1<<w) //区域d大小,一个区域由RW * RW个区块组成，且区块坐标对齐8的倍速
    const static int GRID_WIDTH;       // 地区格子宽度(单位是区块)

    // Configurable

    // Gui
    static QString FONT_FAMILY;
    static int FONT_SIZE;
    static QString COLOR_THEME;
    static bool OPEN_NBT_EDITOR_ONLY;

    // Map
    static int SHADOW_LEVEL;
    static float ZOOM_SPEED;
    static int MINIMUM_SCALE_LEVEL;
    static int MAXIMUM_SCALE_LEVEL;
    static int MAP_RENDER_STYLE;
    static QString GRID_LINE_COLOR;
    static int ACTOR_RENDER_STYLE;
    static int ACTOR_BORDER_WIDTH;
    static QString ACTOR_BORDER_COLOR;
    static QString VOID_MAP_COLOR;
    static bool TRANSPARENT_WATER;
    static bool ENABLE_THUMBNAIL_MODE;

    // Cache
    static int THREAD_NUM;
    static int REGION_CACHE_SIZE;
    static int EMPTY_REGION_CACHE_SIZE;
    static int THUMBNAIL_REION_CACHE_SIZE;

    // misc
    static bool LOAD_GLOBAL_DATA;
    static int MAX_GLOBAL_DATA_LOAD_COUNT;

    // debug
    static bool LOG_OUT_MISSING_TEXTURE;

    // Configurable (not in config file, can be changed at runtime)
    static bool transparent_void;

    static region_pos c2r(const bl::chunk_pos &ch);

    static void initColorTable();

    static void initConfig();

    static QString VERSION_STRING();
    static QString MCBE_LEVEL_PATH;
};

#endif  // CONFIG_H
