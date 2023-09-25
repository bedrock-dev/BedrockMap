#ifndef CONFIG_H
#define CONFIG_H

#include <QColor>
#include <QImage>
#include <bitset>
#include <cstdint>

#include "bedrock_key.h"

typedef bl::chunk_pos region_pos;

struct cfg {
    // 软件基本信息
    const static std::string SOFTWARE_NAME;
    const static std::string SOFTWARE_VERSION;


    // 不可配置的
    // 配置文件
    const static std::string CONFIG_FILE_PATH; //配置文件路径
    // 颜色表文件路径
    const static std::string BLOCK_FILE_PATH;
    const static std::string BIOME_FILE_PATH;
    static constexpr uint8_t RW = 8u;  //(1<<w) //区域d大小,一个区域由RW * RW个区块组成，且区块坐标对齐8的倍速

    const static int GRID_WIDTH; //地区格子宽度(单位是区块)
    // 可配置的
    static int SHADOW_LEVEL;             // 地形图的阴影等级
    static float ZOOM_SPEED;             // 滚轮缩放苏晒
    static int THREAD_NUM;               // 后台线程数
    static int REGION_CACHE_SIZE;        // 区域缓存大小
    static int EMPTY_REGION_CACHE_SIZE;  // 空区域缓存大小
    static int MINIMUM_SCALE_LEVEL;      // 最大缩放等级
    static int MAXIMUM_SCALE_LEVEL;      // 最小缩放等级
    static int FONT_SIZE;                // 字体大小
    static bool FANCY_TERRAIN_RENDER;    // 是否渲染阴影
    static bool LOAD_GLOBAL_DATA;        // 是否加载村庄等全局数据
    static bool OPEN_NBT_EDITOR_ONLY;   //直接打开nbt编辑器
    static std::string COLOR_THEME;     //主体

    //运行时配置
    static bool transparent_void;

    static region_pos c2r(const bl::chunk_pos &ch);

    static void initColorTable();

    static void initConfig();

    static QImage CREATE_REGION_IMG(const std::bitset<cfg::RW * cfg::RW> &bitmap);

    static QImage *UNLOADED_REGION_IMAGE();

    static QImage *NULL_REGION_IMAGE();

//    static QImage *EMPTY_REGION_IMAGE();

    static QString VERSION_STRING();
};

#endif  // CONFIG_H
