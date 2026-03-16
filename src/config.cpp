//
// Created by xhy on 2023/7/11.
//
#include "config.h"

#include <qcolor.h>
#include <qglobal.h>
#include <qimage.h>
#include <qnamespace.h>
#include <qnumeric.h>
#include <qsettings.h>

#include <QDir>
#include <QtDebug>
#include <string>

#include "color.h"

// Base info
const std::string cfg::SOFTWARE_NAME = "BedrockMap";
const std::string cfg::SOFTWARE_VERSION = "v0.5.0";

// Unconfigurable
const int cfg::GRID_WIDTH = 32;

// Configurable
// Gui
QString cfg::FONT_FAMILY;
int cfg::FONT_SIZE = -1;
QString cfg::COLOR_THEME = "developing";
bool cfg::OPEN_NBT_EDITOR_ONLY = false;

// map
int cfg::SHADOW_LEVEL = 128;
float cfg::ZOOM_SPEED = 1.2;
int cfg::MINIMUM_SCALE_LEVEL = 4;
int cfg::MAXIMUM_SCALE_LEVEL = 1024;
int cfg::MAP_RENDER_STYLE = 1;
QString cfg::GRID_LINE_COLOR = "#bbbbbb";
int cfg::ACTOR_RENDER_STYLE = 0;  // 0/1
int cfg::ACTOR_BORDER_WIDTH = 2;
QString cfg::ACTOR_BORDER_COLOR = "#ff000000";
QString cfg::VOID_MAP_COLOR = "#dddddd";
bool cfg::TRANSPARENT_WATER = true;

// cache
int cfg::THREAD_NUM = 8;
int cfg::REGION_CACHE_SIZE = 4096;
int cfg::EMPTY_REGION_CACHE_SIZE = 16384;
int cfg::THUMBNAIL_REION_CACHE_SIZE = 65536;

// misc
bool cfg::LOAD_GLOBAL_DATA = true;

// debug
bool cfg::LOG_OUT_MISSING_TEXTURE = false;

// others
bool cfg::transparent_void = false;

// 三个重要文件的路径，直接内置
#ifdef QT_DEBUG
const std::string cfg::CONFIG_FILE_PATH = R"(../config.ini)";
const std::string cfg::BLOCK_FILE_PATH = R"(../bedrock-level/data/colors/block_color.json)";
const std::string cfg::BIOME_FILE_PATH = R"(../bedrock-level/data/colors/biome_color.json)";
#else
const std::string cfg::CONFIG_FILE_PATH = "config.ini";
const std::string cfg::BLOCK_FILE_PATH = "block_color.json";
const std::string cfg::BIOME_FILE_PATH = "biome_color.json";
#endif

region_pos cfg::c2r(const bl::chunk_pos &ch) {
    auto cx = ch.x < 0 ? ch.x - cfg::RW + 1 : ch.x;
    auto cz = ch.z < 0 ? ch.z - cfg::RW + 1 : ch.z;
    return region_pos{cx / cfg::RW * cfg::RW, cz / cfg::RW * cfg::RW, ch.dim};
}

void cfg::initColorTable() {
    if (!bl::init_biome_color_palette_from_file(cfg::BIOME_FILE_PATH)) {
        qWarning() << "Can not load biome color file in path: " << BIOME_FILE_PATH.c_str();
    }

    if (!bl::init_block_color_from_file(cfg::BLOCK_FILE_PATH)) {
        qWarning() << "Can not load block color file in path: " << BLOCK_FILE_PATH.c_str();
    }

    bl::setUseColorDebugMode(cfg::LOG_OUT_MISSING_TEXTURE);
}

void cfg::initConfig() {
    qInfo() << "Current working directory: " << QDir::currentPath();
    qInfo() << "Configuration file path: " << CONFIG_FILE_PATH.c_str();
    QSettings setting(CONFIG_FILE_PATH.c_str(), QSettings::IniFormat);

    setting.beginGroup("Gui");
    cfg::COLOR_THEME = setting.value("theme", cfg::COLOR_THEME).toString();
    cfg::FONT_FAMILY = setting.value("font_family", cfg::FONT_FAMILY).toString();
    cfg::FONT_SIZE = setting.value("font_size", cfg::FONT_SIZE).toInt();
    cfg::OPEN_NBT_EDITOR_ONLY = setting.value("nbt_editor_mode", cfg::OPEN_NBT_EDITOR_ONLY).toBool();
    setting.endGroup();

    setting.beginGroup("Map");
    cfg::SHADOW_LEVEL = setting.value("terrian_shadow_level", cfg::SHADOW_LEVEL).toInt();
    cfg::MINIMUM_SCALE_LEVEL = setting.value("min_scale_level", cfg::MINIMUM_SCALE_LEVEL).toInt();
    cfg::MAXIMUM_SCALE_LEVEL = setting.value("max_scale_level", cfg::MAXIMUM_SCALE_LEVEL).toInt();
    cfg::ZOOM_SPEED = setting.value("zoom_speed", cfg::ZOOM_SPEED).toDouble();
    cfg::MAP_RENDER_STYLE = setting.value("render_style", cfg::MAP_RENDER_STYLE).toInt();
    cfg::GRID_LINE_COLOR = setting.value("grid_line_color", cfg::GRID_LINE_COLOR).toString();
    cfg::ACTOR_RENDER_STYLE = setting.value("actor_render_style", cfg::ACTOR_RENDER_STYLE).toInt();
    cfg::ACTOR_BORDER_WIDTH = setting.value("actor_border_width", cfg::ACTOR_BORDER_WIDTH).toInt();
    cfg::ACTOR_BORDER_COLOR = setting.value("actor_border_color", cfg::ACTOR_BORDER_COLOR).toString();
    cfg::VOID_MAP_COLOR = setting.value("void_color", cfg::VOID_MAP_COLOR).toString();
    cfg::TRANSPARENT_WATER = setting.value("transparent_water", cfg::TRANSPARENT_WATER).toBool();
    setting.endGroup();

    setting.beginGroup("Cache");
    cfg::REGION_CACHE_SIZE = setting.value("region_cache_size", cfg::REGION_CACHE_SIZE).toInt();
    cfg::EMPTY_REGION_CACHE_SIZE = setting.value("empty_cache_size", cfg::EMPTY_REGION_CACHE_SIZE).toInt();
    cfg::THREAD_NUM = setting.value("max_thread_num", cfg::THREAD_NUM).toInt();
    setting.endGroup();

    setting.beginGroup("Misc");
    cfg::LOAD_GLOBAL_DATA = setting.value("load_global_data", cfg::LOAD_GLOBAL_DATA).toBool();
    setting.endGroup();

    setting.beginGroup("Debug");
    cfg::LOG_OUT_MISSING_TEXTURE = setting.value("log_out_missng_texture", cfg::LOG_OUT_MISSING_TEXTURE).toBool();
    setting.endGroup();

    if (setting.status() != QSettings::NoError) {
        qDebug() << "Settings read error:" << setting.status();
    }

    qInfo() << "all keys: " << setting.allKeys().size();

    if (THREAD_NUM < 1) {
        THREAD_NUM = 2;
        qWarning() << "Invalid background thread number, reset it to default(2)";
    }

    initColorTable();
}

QString cfg::VERSION_STRING() { return QString(cfg::SOFTWARE_NAME.c_str()) + " " + QString(cfg::SOFTWARE_VERSION.c_str()); }