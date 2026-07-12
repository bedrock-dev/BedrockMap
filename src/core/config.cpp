//
// Created by xhy on 2023/7/11.
//
#include "config.h"

#include <qchar.h>
#include <qcolor.h>
#include <qcontainerfwd.h>
#include <qglobal.h>
#include <qimage.h>
#include <qmath.h>
#include <qnamespace.h>
#include <qnumeric.h>
#include <qsettings.h>

#include <QDir>
#include <QFile>
#include <algorithm>
#include <string>

#include "color.h"
#include "loguru/loguru.hpp"

// constant namespace
const std::string constant::SOFTWARE_NAME = "BedrockMap";
const std::string constant::SOFTWARE_VERSION = "v1.0.0-beta4";
const int constant::GRID_WIDTH = 32;

#ifdef QT_DEBUG
const std::string constant::CONFIG_FILE_PATH = R"(../config.ini)";
const std::string constant::BLOCK_FILE_PATH = R"(../bedrock-level/data/colors/block_color.json)";
const std::string constant::BIOME_FILE_PATH = R"(../bedrock-level/data/colors/biome_color.json)";
const QString constant::SHADER_FILE_PATH = R"(../res/shaders/voxel)";
const QString constant::TRANSLATION_FILES_PATH = R"(./)";
#else
const std::string constant::CONFIG_FILE_PATH = "config.ini";
const std::string constant::BLOCK_FILE_PATH = "block_color.json";
const std::string constant::BIOME_FILE_PATH = "biome_color.json";
const QString constant::SHADER_FILE_PATH = "shaders/voxel";
const QString constant::TRANSLATION_FILES_PATH = R"(./translations)";
#endif

QString constant::MCBE_LEVEL_PATH = "/Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/LocalState/games/com.mojang/minecraftWorlds";

region_pos constant::c2r(const bl::chunk_pos &ch) {
    auto cx = ch.x < 0 ? ch.x - constant::RW + 1 : ch.x;
    auto cz = ch.z < 0 ? ch.z - constant::RW + 1 : ch.z;
    return region_pos{cx / constant::RW * constant::RW, cz / constant::RW * constant::RW, ch.dim};
}

QString constant::VERSION_STRING() {
    return QString(constant::SOFTWARE_NAME.c_str()) + " " + QString(constant::SOFTWARE_VERSION.c_str()) + "." + QString(GIT_COMMIT_HASH);
}

// setting namespace — defaults
// Gui
QString setting::COLOR_THEME = "system";
QString setting::FONT_FAMILY;
int setting::FONT_SIZE = -1;
bool setting::OPEN_NBT_EDITOR_ONLY = false;

// Map
int setting::MAP_RENDER_STYLE = 1;
int setting::SHADOW_RENDER_SCALE = 2;
int setting::SHADOW_PCF_RADIUS = 1;
int setting::SHADOW_LEVEL = 128;
int setting::MINIMUM_SCALE_LEVEL = 4;
int setting::MAXIMUM_SCALE_LEVEL = 1024;
float setting::ZOOM_SPEED = 1.2f;
QString setting::GRID_LINE_COLOR = "#bbbbbb";
int setting::ACTOR_RENDER_STYLE = 0;
int setting::ACTOR_BORDER_WIDTH = 2;
QString setting::ACTOR_BORDER_COLOR = "#ff000000";
QString setting::CHUNK_EDITOR_HIGHLIGHT_COLOR = "#ffaa00";
int setting::CHUNK_EDITOR_HIGHLIGHT_WIDTH = 8;
QString setting::VOID_MAP_COLOR = "#dddddd";
bool setting::TRANSPARENT_WATER = true;
bool setting::ENABLE_THUMBNAIL_MODE = true;

// Cache
int setting::THREAD_NUM = 8;
int setting::REGION_CACHE_SIZE = 4096;
int setting::EMPTY_REGION_CACHE_SIZE = 16384;
int setting::THUMBNAIL_REION_CACHE_SIZE = 65536;

// Misc
bool setting::LOAD_GLOBAL_DATA = true;
int setting::MAX_GLOBAL_DATA_LOAD_COUNT = 4096;
QString setting::ICON_THEME = "classic";

// LeviLauncher
bool setting::SCAN_LEVI_PATH = true;

// Debug
bool setting::LOG_OUT_MISSING_TEXTURE = false;

// Lang
QString setting::LANGUAGE = "zh_CN";

// Utility functions
void constant::initColorTable() {
    if (!bl::init_biome_color_palette_from_file(constant::BIOME_FILE_PATH)) {
        LOG_F(WARNING, "Can not load biome color file in path: %s", BIOME_FILE_PATH.c_str());
    }
    if (!bl::init_block_color_from_file(constant::BLOCK_FILE_PATH)) {
        LOG_F(WARNING, "Can not load block color file in path: %s", BLOCK_FILE_PATH.c_str());
    }
    bl::setUseColorDebugMode(setting::LOG_OUT_MISSING_TEXTURE);
}

void setting::init() {
    LOG_F(INFO, "Current working directory: %s", QDir::currentPath().toStdString().c_str());
    LOG_F(INFO, "Configuration file path: %s", constant::CONFIG_FILE_PATH.c_str());

    if (!QFile::exists(constant::CONFIG_FILE_PATH.c_str())) {
        LOG_F(INFO, "Config file not found, writing defaults");
        setting::save();
    }

    setting::load();
}

void setting::load() {
    QSettings s(constant::CONFIG_FILE_PATH.c_str(), QSettings::IniFormat);

    foreach (const auto &key, s.allKeys()) {
        LOG_F(INFO, "Config key: %s, value: %s", key.toStdString().c_str(), s.value(key).toString().toStdString().c_str());
    }

    s.beginGroup("Gui");
    setting::COLOR_THEME = s.value("theme", setting::COLOR_THEME).toString();
    setting::FONT_FAMILY = s.value("font_family", setting::FONT_FAMILY).toString();
    setting::FONT_SIZE = s.value("font_size", setting::FONT_SIZE).toInt();
    setting::OPEN_NBT_EDITOR_ONLY = s.value("nbt_editor_mode", setting::OPEN_NBT_EDITOR_ONLY).toBool();
    s.endGroup();

    s.beginGroup("Map");
    setting::SHADOW_LEVEL = s.value("terrian_shadow_level", setting::SHADOW_LEVEL).toInt();
    setting::MINIMUM_SCALE_LEVEL = s.value("min_scale_level", setting::MINIMUM_SCALE_LEVEL).toInt();
    setting::MAXIMUM_SCALE_LEVEL = s.value("max_scale_level", setting::MAXIMUM_SCALE_LEVEL).toInt();
    setting::ZOOM_SPEED = s.value("zoom_speed", setting::ZOOM_SPEED).toDouble();
    setting::MAP_RENDER_STYLE = s.value("render_style", setting::MAP_RENDER_STYLE).toInt();
    setting::SHADOW_RENDER_SCALE = std::clamp(s.value("shadow_render_scale", setting::SHADOW_RENDER_SCALE).toInt(), 1, 16);
    setting::SHADOW_PCF_RADIUS = std::clamp(s.value("shadow_pcf_radius", setting::SHADOW_PCF_RADIUS).toInt(), 0, 8);
    setting::GRID_LINE_COLOR = s.value("grid_line_color", setting::GRID_LINE_COLOR).toString();
    setting::ACTOR_RENDER_STYLE = s.value("actor_render_style", setting::ACTOR_RENDER_STYLE).toInt();
    setting::ACTOR_BORDER_WIDTH = s.value("actor_border_width", setting::ACTOR_BORDER_WIDTH).toInt();
    setting::ACTOR_BORDER_COLOR = s.value("actor_border_color", setting::ACTOR_BORDER_COLOR).toString();
    setting::CHUNK_EDITOR_HIGHLIGHT_COLOR = s.value("chunk_editor_highlight_color", setting::CHUNK_EDITOR_HIGHLIGHT_COLOR).toString();
    setting::CHUNK_EDITOR_HIGHLIGHT_WIDTH = s.value("chunk_editor_highlight_width", setting::CHUNK_EDITOR_HIGHLIGHT_WIDTH).toInt();
    setting::VOID_MAP_COLOR = s.value("void_color", setting::VOID_MAP_COLOR).toString();
    setting::TRANSPARENT_WATER = s.value("transparent_water", setting::TRANSPARENT_WATER).toBool();
    setting::ENABLE_THUMBNAIL_MODE = s.value("enable_thumbnail_mode", setting::ENABLE_THUMBNAIL_MODE).toBool();
    s.endGroup();

    s.beginGroup("Cache");
    setting::REGION_CACHE_SIZE = s.value("region_cache_size", setting::REGION_CACHE_SIZE).toInt();
    setting::EMPTY_REGION_CACHE_SIZE = s.value("empty_cache_size", setting::EMPTY_REGION_CACHE_SIZE).toInt();
    setting::THREAD_NUM = s.value("max_thread_num", setting::THREAD_NUM).toInt();
    s.endGroup();

    s.beginGroup("Misc");
    setting::LOAD_GLOBAL_DATA = s.value("load_global_data", setting::LOAD_GLOBAL_DATA).toBool();
    setting::MAX_GLOBAL_DATA_LOAD_COUNT = s.value("max_global_data_load_count", setting::MAX_GLOBAL_DATA_LOAD_COUNT).toInt();
    setting::ICON_THEME = s.value("icon_theme", setting::ICON_THEME).toString();
    s.endGroup();

    s.beginGroup("Debug");
    setting::LOG_OUT_MISSING_TEXTURE = s.value("log_out_missng_texture", setting::LOG_OUT_MISSING_TEXTURE).toBool();
    s.endGroup();

    s.beginGroup("LeviLauncher");
    setting::SCAN_LEVI_PATH = s.value("scan_levi_path", setting::SCAN_LEVI_PATH).toBool();
    s.endGroup();

    s.beginGroup("Lang");
    setting::LANGUAGE = s.value("lang", setting::LANGUAGE).toString();
    s.endGroup();

    if (s.status() != QSettings::NoError) {
        LOG_F(WARNING, "Settings read error: %d", s.status());
    }

    LOG_F(INFO, "all keys: %d", static_cast<int>(s.allKeys().size()));

    if (setting::THREAD_NUM < 1) {
        setting::THREAD_NUM = 2;
        LOG_F(WARNING, "Invalid background thread number, reset it to default(2)");
    }
}

void setting::save() {
    QSettings s(constant::CONFIG_FILE_PATH.c_str(), QSettings::IniFormat);

    s.beginGroup("Gui");
    s.setValue("theme", setting::COLOR_THEME);
    s.setValue("font_family", setting::FONT_FAMILY);
    s.setValue("font_size", setting::FONT_SIZE);
    s.setValue("nbt_editor_mode", setting::OPEN_NBT_EDITOR_ONLY);
    s.endGroup();

    s.beginGroup("Map");
    s.setValue("render_style", setting::MAP_RENDER_STYLE);
    s.setValue("shadow_render_scale", setting::SHADOW_RENDER_SCALE);
    s.setValue("shadow_pcf_radius", setting::SHADOW_PCF_RADIUS);
    s.setValue("terrian_shadow_level", setting::SHADOW_LEVEL);
    s.setValue("min_scale_level", setting::MINIMUM_SCALE_LEVEL);
    s.setValue("max_scale_level", setting::MAXIMUM_SCALE_LEVEL);
    s.setValue("zoom_speed", setting::ZOOM_SPEED);
    s.setValue("grid_line_color", setting::GRID_LINE_COLOR);
    s.setValue("actor_render_style", setting::ACTOR_RENDER_STYLE);
    s.setValue("actor_border_width", setting::ACTOR_BORDER_WIDTH);
    s.setValue("actor_border_color", setting::ACTOR_BORDER_COLOR);
    s.setValue("chunk_editor_highlight_color", setting::CHUNK_EDITOR_HIGHLIGHT_COLOR);
    s.setValue("chunk_editor_highlight_width", setting::CHUNK_EDITOR_HIGHLIGHT_WIDTH);
    s.setValue("void_color", setting::VOID_MAP_COLOR);
    s.setValue("transparent_water", setting::TRANSPARENT_WATER);
    s.setValue("enable_thumbnail_mode", setting::ENABLE_THUMBNAIL_MODE);
    s.endGroup();

    s.beginGroup("Cache");
    s.setValue("region_cache_size", setting::REGION_CACHE_SIZE);
    s.setValue("empty_cache_size", setting::EMPTY_REGION_CACHE_SIZE);
    s.setValue("max_thread_num", setting::THREAD_NUM);
    s.endGroup();

    s.beginGroup("Misc");
    s.setValue("load_global_data", setting::LOAD_GLOBAL_DATA);
    s.setValue("max_global_data_load_count", setting::MAX_GLOBAL_DATA_LOAD_COUNT);
    s.setValue("icon_theme", setting::ICON_THEME);
    s.endGroup();

    s.beginGroup("Debug");
    s.setValue("log_out_missng_texture", setting::LOG_OUT_MISSING_TEXTURE);
    s.endGroup();

    s.beginGroup("LeviLauncher");
    s.setValue("scan_levi_path", setting::SCAN_LEVI_PATH);
    s.endGroup();

    s.beginGroup("Lang");
    s.setValue("lang", setting::LANGUAGE);
    s.endGroup();

    s.sync();

    LOG_F(INFO, "Settings saved to %s", constant::CONFIG_FILE_PATH.c_str());
}
