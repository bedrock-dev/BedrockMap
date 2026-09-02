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

std::optional<AppVersion> AppVersion::parse(const QString &text) {
    QString s = text.trimmed();
    if (s.startsWith('v') || s.startsWith('V')) s = s.mid(1);

    AppVersion ver;
    const int dash = s.indexOf('-');
    const QString core = dash >= 0 ? s.left(dash) : s;
    if (dash >= 0) {
        const QString suffix = s.mid(dash + 1);
        if (!suffix.startsWith("beta", Qt::CaseInsensitive)) return std::nullopt;  // unknown suffix, cannot compare
        bool ok = false;
        const int n = suffix.mid(4).toInt(&ok);
        if (!ok || n < 0) return std::nullopt;
        ver.beta = n;
    }

    const QStringList parts = core.split('.');
    if (parts.isEmpty() || parts.size() > static_cast<int>(ver.core.size())) return std::nullopt;
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        const int n = parts[i].toInt(&ok);
        if (!ok || n < 0) return std::nullopt;
        ver.core[i] = n;
    }
    return ver;
}

QString AppVersion::toString() const {
    QString s = QString("v%1.%2.%3").arg(core[0]).arg(core[1]).arg(core[2]);
    if (beta >= 0) s += QString("-beta%1").arg(beta);
    return s;
}

int AppVersion::compare(const AppVersion &other) const {
    for (size_t i = 0; i < core.size(); ++i) {
        if (core[i] != other.core[i]) return core[i] < other.core[i] ? -1 : 1;
    }
    if (beta != other.beta) {
        if (beta >= 0 && other.beta >= 0) return beta < other.beta ? -1 : 1;
        return beta < 0 ? 1 : -1;
    }
    return 0;
}

// constant namespace
const std::string constant::SOFTWARE_NAME = "BedrockMap";
const AppVersion constant::SOFTWARE_VERSION{{1, 0, 0}, 8};
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
    return QString(constant::SOFTWARE_NAME.c_str()) + " " + constant::SOFTWARE_VERSION.toString() + "." + QString(GIT_COMMIT_HASH);
}

// setting namespace — defaults
// Gui
QString setting::COLOR_THEME = "system";
QString setting::FONT_FAMILY;
int setting::FONT_SIZE = -1;

// Map
int setting::MAP_RENDER_STYLE = 1;
int setting::TILE_RENDER_SCALE = 4;
int setting::SHADOW_PCF_RADIUS = 0;
int setting::SHADOW_MAP_SCALE = 2;
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

// Cache
int setting::THREAD_NUM = 8;
int setting::REGION_CACHE_SIZE = 1024;
int setting::EMPTY_REGION_CACHE_SIZE = 8192;
int setting::HEIGHT_MAP_CACHE_SIZE = 500000;

// Misc
bool setting::LOAD_GLOBAL_DATA = true;
bool setting::PRELOAD_ALL_CHUNK_COORDS = false;
int setting::MAX_GLOBAL_DATA_LOAD_COUNT = 4096;
QString setting::ICON_THEME = "new";
bool setting::CHECK_UPDATE = true;

// LeviLauncher
bool setting::SCAN_LEVI_PATH = true;

// Lang (empty = auto-detect from the system locale)
QString setting::LANGUAGE = "";

// Utility functions
void constant::initColorTable() {
    if (!bl::init_biome_color_palette_from_file(constant::BIOME_FILE_PATH)) {
        LOG_F(WARNING, "Can not load biome color file in path: %s", BIOME_FILE_PATH.c_str());
    }
    if (!bl::init_block_color_from_file(constant::BLOCK_FILE_PATH)) {
        LOG_F(WARNING, "Can not load block color file in path: %s", BLOCK_FILE_PATH.c_str());
    }
    bl::config::set_log_missing_block_color(false);
    bl::config::set_log_mismatched_actor(false);
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
    s.endGroup();

    s.beginGroup("Map");
    setting::SHADOW_LEVEL = s.value("terrian_shadow_level", setting::SHADOW_LEVEL).toInt();
    setting::MINIMUM_SCALE_LEVEL = s.value("min_scale_level", setting::MINIMUM_SCALE_LEVEL).toInt();
    setting::MAXIMUM_SCALE_LEVEL = s.value("max_scale_level", setting::MAXIMUM_SCALE_LEVEL).toInt();
    setting::ZOOM_SPEED = std::max(0.1f, static_cast<float>(s.value("zoom_speed", setting::ZOOM_SPEED).toDouble()));
    setting::MAP_RENDER_STYLE = s.value("render_style", setting::MAP_RENDER_STYLE).toInt();
    setting::TILE_RENDER_SCALE = std::clamp(s.value("tile_render_scale", setting::TILE_RENDER_SCALE).toInt(), 1, 16);
    setting::SHADOW_PCF_RADIUS = std::clamp(s.value("shadow_pcf_radius", setting::SHADOW_PCF_RADIUS).toInt(), 0, 8);
    setting::SHADOW_MAP_SCALE = std::clamp(s.value("shadow_map_scale", setting::SHADOW_MAP_SCALE).toInt(), 1, 8);
    setting::GRID_LINE_COLOR = s.value("grid_line_color", setting::GRID_LINE_COLOR).toString();
    setting::ACTOR_RENDER_STYLE = s.value("actor_render_style", setting::ACTOR_RENDER_STYLE).toInt();
    setting::ACTOR_BORDER_WIDTH = s.value("actor_border_width", setting::ACTOR_BORDER_WIDTH).toInt();
    setting::ACTOR_BORDER_COLOR = s.value("actor_border_color", setting::ACTOR_BORDER_COLOR).toString();
    setting::CHUNK_EDITOR_HIGHLIGHT_COLOR = s.value("chunk_editor_highlight_color", setting::CHUNK_EDITOR_HIGHLIGHT_COLOR).toString();
    setting::CHUNK_EDITOR_HIGHLIGHT_WIDTH = s.value("chunk_editor_highlight_width", setting::CHUNK_EDITOR_HIGHLIGHT_WIDTH).toInt();
    setting::VOID_MAP_COLOR = s.value("void_color", setting::VOID_MAP_COLOR).toString();
    setting::TRANSPARENT_WATER = s.value("transparent_water", setting::TRANSPARENT_WATER).toBool();
    s.endGroup();

    s.beginGroup("Cache");
    setting::REGION_CACHE_SIZE = s.value("region_cache_size", setting::REGION_CACHE_SIZE).toInt();
    setting::EMPTY_REGION_CACHE_SIZE = s.value("empty_cache_size", setting::EMPTY_REGION_CACHE_SIZE).toInt();
    setting::THREAD_NUM = s.value("max_thread_num", setting::THREAD_NUM).toInt();
    setting::HEIGHT_MAP_CACHE_SIZE = s.value("height_map_cache_size", setting::HEIGHT_MAP_CACHE_SIZE).toInt();
    s.endGroup();

    s.beginGroup("Misc");
    setting::LOAD_GLOBAL_DATA = s.value("load_global_data", setting::LOAD_GLOBAL_DATA).toBool();
    setting::PRELOAD_ALL_CHUNK_COORDS = s.value("preload_all_chunk_coords", setting::PRELOAD_ALL_CHUNK_COORDS).toBool();
    setting::MAX_GLOBAL_DATA_LOAD_COUNT = s.value("max_global_data_load_count", setting::MAX_GLOBAL_DATA_LOAD_COUNT).toInt();
    setting::ICON_THEME = s.value("icon_theme", setting::ICON_THEME).toString();
    setting::CHECK_UPDATE = s.value("check_update", setting::CHECK_UPDATE).toBool();
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

setting::SettingsSnapshot setting::snapshot() {
    SettingsSnapshot values;
    values.COLOR_THEME = COLOR_THEME;
    values.FONT_FAMILY = FONT_FAMILY;
    values.FONT_SIZE = FONT_SIZE;
    values.MAP_RENDER_STYLE = MAP_RENDER_STYLE;
    values.TILE_RENDER_SCALE = TILE_RENDER_SCALE;
    values.SHADOW_PCF_RADIUS = SHADOW_PCF_RADIUS;
    values.SHADOW_MAP_SCALE = SHADOW_MAP_SCALE;
    values.SHADOW_LEVEL = SHADOW_LEVEL;
    values.MINIMUM_SCALE_LEVEL = MINIMUM_SCALE_LEVEL;
    values.MAXIMUM_SCALE_LEVEL = MAXIMUM_SCALE_LEVEL;
    values.ZOOM_SPEED = ZOOM_SPEED;
    values.GRID_LINE_COLOR = GRID_LINE_COLOR;
    values.ACTOR_RENDER_STYLE = ACTOR_RENDER_STYLE;
    values.ACTOR_BORDER_WIDTH = ACTOR_BORDER_WIDTH;
    values.ACTOR_BORDER_COLOR = ACTOR_BORDER_COLOR;
    values.CHUNK_EDITOR_HIGHLIGHT_COLOR = CHUNK_EDITOR_HIGHLIGHT_COLOR;
    values.CHUNK_EDITOR_HIGHLIGHT_WIDTH = CHUNK_EDITOR_HIGHLIGHT_WIDTH;
    values.VOID_MAP_COLOR = VOID_MAP_COLOR;
    values.TRANSPARENT_WATER = TRANSPARENT_WATER;
    values.THREAD_NUM = THREAD_NUM;
    values.REGION_CACHE_SIZE = REGION_CACHE_SIZE;
    values.EMPTY_REGION_CACHE_SIZE = EMPTY_REGION_CACHE_SIZE;
    values.HEIGHT_MAP_CACHE_SIZE = HEIGHT_MAP_CACHE_SIZE;
    values.LOAD_GLOBAL_DATA = LOAD_GLOBAL_DATA;
    values.PRELOAD_ALL_CHUNK_COORDS = PRELOAD_ALL_CHUNK_COORDS;
    values.MAX_GLOBAL_DATA_LOAD_COUNT = MAX_GLOBAL_DATA_LOAD_COUNT;
    values.ICON_THEME = ICON_THEME;
    values.CHECK_UPDATE = CHECK_UPDATE;
    values.SCAN_LEVI_PATH = SCAN_LEVI_PATH;
    values.LANGUAGE = LANGUAGE;
    return values;
}

void setting::save() { save(snapshot()); }

void setting::save(const SettingsSnapshot &values) {
    QSettings s(constant::CONFIG_FILE_PATH.c_str(), QSettings::IniFormat);

    s.beginGroup("Gui");
    s.setValue("theme", values.COLOR_THEME);
    s.setValue("font_family", values.FONT_FAMILY);
    s.setValue("font_size", values.FONT_SIZE);
    s.endGroup();

    s.beginGroup("Map");
    s.setValue("render_style", values.MAP_RENDER_STYLE);
    s.setValue("tile_render_scale", values.TILE_RENDER_SCALE);
    s.setValue("shadow_pcf_radius", values.SHADOW_PCF_RADIUS);
    s.setValue("shadow_map_scale", values.SHADOW_MAP_SCALE);
    s.setValue("terrian_shadow_level", values.SHADOW_LEVEL);
    s.setValue("min_scale_level", values.MINIMUM_SCALE_LEVEL);
    s.setValue("max_scale_level", values.MAXIMUM_SCALE_LEVEL);
    s.setValue("zoom_speed", values.ZOOM_SPEED);
    s.setValue("grid_line_color", values.GRID_LINE_COLOR);
    s.setValue("actor_render_style", values.ACTOR_RENDER_STYLE);
    s.setValue("actor_border_width", values.ACTOR_BORDER_WIDTH);
    s.setValue("actor_border_color", values.ACTOR_BORDER_COLOR);
    s.setValue("chunk_editor_highlight_color", values.CHUNK_EDITOR_HIGHLIGHT_COLOR);
    s.setValue("chunk_editor_highlight_width", values.CHUNK_EDITOR_HIGHLIGHT_WIDTH);
    s.setValue("void_color", values.VOID_MAP_COLOR);
    s.setValue("transparent_water", values.TRANSPARENT_WATER);
    s.endGroup();

    s.beginGroup("Cache");
    s.setValue("region_cache_size", values.REGION_CACHE_SIZE);
    s.setValue("empty_cache_size", values.EMPTY_REGION_CACHE_SIZE);
    s.setValue("max_thread_num", values.THREAD_NUM);
    s.setValue("height_map_cache_size", values.HEIGHT_MAP_CACHE_SIZE);
    s.endGroup();

    s.beginGroup("Misc");
    s.setValue("load_global_data", values.LOAD_GLOBAL_DATA);
    s.setValue("preload_all_chunk_coords", values.PRELOAD_ALL_CHUNK_COORDS);
    s.setValue("max_global_data_load_count", values.MAX_GLOBAL_DATA_LOAD_COUNT);
    s.setValue("icon_theme", values.ICON_THEME);
    s.setValue("check_update", values.CHECK_UPDATE);
    s.endGroup();

    s.beginGroup("LeviLauncher");
    s.setValue("scan_levi_path", values.SCAN_LEVI_PATH);
    s.endGroup();

    s.beginGroup("Lang");
    s.setValue("lang", values.LANGUAGE);
    s.endGroup();

    s.sync();

    LOG_F(INFO, "Settings saved to %s", constant::CONFIG_FILE_PATH.c_str());
}
