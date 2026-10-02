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

// bedrock-level's config.h is shadowed by the app config.h on the include path;
// declare the tunables used here (same pattern as chunkcoords.cpp).
namespace bl::config {
    void set_log_mismatched_actor(bool);
    void set_log_missing_block_color(bool);
}  // namespace bl::config

std::optional<AppVersion> AppVersion::parse(const QString& text) {
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

int AppVersion::compare(const AppVersion& other) const {
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
const AppVersion constant::SOFTWARE_VERSION{{1, 0, 0}, 12};
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

const QString constant::MCBE_LEVEL_PATH = "/Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/LocalState/games/com.mojang/minecraftWorlds";

region_pos constant::c2r(const bl::chunk_pos& ch) {
    auto cx = ch.x < 0 ? ch.x - constant::RW + 1 : ch.x;
    auto cz = ch.z < 0 ? ch.z - constant::RW + 1 : ch.z;
    return region_pos{cx / constant::RW * constant::RW, cz / constant::RW * constant::RW, ch.dim};
}

QString constant::VERSION_STRING() {
    return QString(constant::SOFTWARE_NAME.c_str()) + " " + constant::SOFTWARE_VERSION.toString() + "." + QString(GIT_COMMIT_HASH);
}

namespace {
    setting::Settings& mutableSettings() {
        static setting::Settings instance;
        return instance;
    }
}  // namespace

void setting::apply(const Settings& settings) { mutableSettings() = settings; }

const setting::Settings& setting::current() { return mutableSettings(); }

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

    if (!QFile::exists(constant::CONFIG_FILE_PATH.c_str())) LOG_F(INFO, "Config file not found, using defaults");
    setting::load();
}

void setting::load() {
    QSettings s(constant::CONFIG_FILE_PATH.c_str(), QSettings::IniFormat);

    foreach (const auto& key, s.allKeys()) {
        LOG_F(INFO, "Config key: %s, value: %s", key.toStdString().c_str(), s.value(key).toString().toStdString().c_str());
    }

    // Parse into a copy so a partial/failed read can never tear the live snapshot.
    Settings loaded = current();

    s.beginGroup("Gui");
    loaded.COLOR_THEME = s.value("theme", loaded.COLOR_THEME).toString();
    loaded.FONT_FAMILY = s.value("font_family", loaded.FONT_FAMILY).toString();
    loaded.FONT_SIZE = s.value("font_size", loaded.FONT_SIZE).toInt();
    s.endGroup();

    s.beginGroup("Map");
    loaded.MINIMUM_SCALE_LEVEL = s.value("min_scale_level", loaded.MINIMUM_SCALE_LEVEL).toInt();
    loaded.MAXIMUM_SCALE_LEVEL = s.value("max_scale_level", loaded.MAXIMUM_SCALE_LEVEL).toInt();
    loaded.COORDS_MINIMAP_WIDTH = std::clamp(s.value("coords_minimap_width", loaded.COORDS_MINIMAP_WIDTH).toInt(), 64, 1024);
    loaded.COORDS_MINIMAP_HEIGHT = std::clamp(s.value("coords_minimap_height", loaded.COORDS_MINIMAP_HEIGHT).toInt(), 64, 1024);
    loaded.GRID_LINE_COLOR = s.value("grid_line_color", loaded.GRID_LINE_COLOR).toString();
    loaded.ACTOR_RENDER_STYLE = s.value("actor_render_style", loaded.ACTOR_RENDER_STYLE).toInt();
    loaded.ACTOR_BORDER_WIDTH = s.value("actor_border_width", loaded.ACTOR_BORDER_WIDTH).toInt();
    loaded.ACTOR_BORDER_COLOR = s.value("actor_border_color", loaded.ACTOR_BORDER_COLOR).toString();
    loaded.CHUNK_EDITOR_HIGHLIGHT_COLOR = s.value("chunk_editor_highlight_color", loaded.CHUNK_EDITOR_HIGHLIGHT_COLOR).toString();
    loaded.CHUNK_EDITOR_HIGHLIGHT_WIDTH = s.value("chunk_editor_highlight_width", loaded.CHUNK_EDITOR_HIGHLIGHT_WIDTH).toInt();
    loaded.VOID_MAP_COLOR = s.value("void_color", loaded.VOID_MAP_COLOR).toString();
    loaded.VOXEL_SELECTION_COLOR = s.value("voxel_selection_color", loaded.VOXEL_SELECTION_COLOR).toString();
    loaded.TRANSPARENT_WATER = s.value("transparent_water", loaded.TRANSPARENT_WATER).toBool();
    s.endGroup();

    s.beginGroup("Rendering");
    loaded.MAP_RENDER_STYLE = s.value("render_style", loaded.MAP_RENDER_STYLE).toInt();
    loaded.GPU_RENDER_ENABLED = s.value("gpu_render_enabled", loaded.GPU_RENDER_ENABLED).toBool();
    loaded.GPU_ORTHOGRAPHIC_VIEW = s.value("gpu_orthographic_view", loaded.GPU_ORTHOGRAPHIC_VIEW).toBool();
    loaded.GPU_AO_STRENGTH = std::clamp(s.value("gpu_ao_strength", loaded.GPU_AO_STRENGTH).toFloat(), 0.0f, 1.0f);
    loaded.GPU_BEVEL_STRENGTH = std::clamp(s.value("gpu_bevel_strength", loaded.GPU_BEVEL_STRENGTH).toFloat(), 0.0f, 1.0f);
    loaded.GPU_BEVEL_WIDTH = std::clamp(s.value("gpu_bevel_width", loaded.GPU_BEVEL_WIDTH).toFloat(), 0.25f, 2.0f);
    loaded.GPU_SATURATION = std::clamp(s.value("gpu_saturation", loaded.GPU_SATURATION).toFloat(), 0.0f, 2.0f);
    loaded.GPU_BRIGHTNESS = std::clamp(s.value("gpu_brightness", loaded.GPU_BRIGHTNESS).toFloat(), 0.0f, 2.0f);
    loaded.GPU_GRASS_HEIGHT_ENABLED = s.value("gpu_grass_height_enabled", loaded.GPU_GRASS_HEIGHT_ENABLED).toBool();
    loaded.GPU_GRASS_HEIGHT_BASE = s.value("gpu_grass_height_base", loaded.GPU_GRASS_HEIGHT_BASE).toFloat();
    loaded.GPU_GRASS_HEIGHT_COLOR = s.value("gpu_grass_height_color", loaded.GPU_GRASS_HEIGHT_COLOR).toString();
    loaded.GPU_GRASS_HEIGHT_RANGE = std::clamp(s.value("gpu_grass_height_range", loaded.GPU_GRASS_HEIGHT_RANGE).toFloat(), 1.0f, 4096.0f);
    loaded.GPU_SHADOW_STRENGTH = std::clamp(s.value("gpu_shadow_strength", loaded.GPU_SHADOW_STRENGTH).toFloat(), 0.0f, 1.0f);
    const int requested_gpu_shadow_step = std::clamp(s.value("gpu_shadow_step", loaded.GPU_SHADOW_STEP).toInt(), 4, 32);
    loaded.GPU_SHADOW_STEP = requested_gpu_shadow_step <= 4 ? 4 : requested_gpu_shadow_step <= 8 ? 8 : requested_gpu_shadow_step <= 16 ? 16 : 32;
    loaded.GPU_BIOME_BLEND_BLOCKS = std::clamp(s.value("gpu_biome_blend_blocks", loaded.GPU_BIOME_BLEND_BLOCKS).toFloat(), 0.0f, 16.0f);
    loaded.TILE_RENDER_SCALE = std::clamp(s.value("tile_render_scale", loaded.TILE_RENDER_SCALE).toInt(), 1, 16);
    loaded.SHADOW_PCF_RADIUS = std::clamp(s.value("shadow_pcf_radius", loaded.SHADOW_PCF_RADIUS).toInt(), 0, 8);
    loaded.SHADOW_MAP_SCALE = std::clamp(s.value("shadow_map_scale", loaded.SHADOW_MAP_SCALE).toInt(), 1, 8);
    loaded.SHADOW_LEVEL = s.value("terrian_shadow_level", loaded.SHADOW_LEVEL).toInt();
    s.endGroup();

    s.beginGroup("Cache");
    loaded.REGION_CACHE_SIZE = s.value("region_cache_size", loaded.REGION_CACHE_SIZE).toInt();
    loaded.EMPTY_REGION_CACHE_SIZE = s.value("empty_cache_size", loaded.EMPTY_REGION_CACHE_SIZE).toInt();
    loaded.THREAD_NUM = s.value("max_thread_num", loaded.THREAD_NUM).toInt();
    loaded.HEIGHT_MAP_CACHE_SIZE = s.value("height_map_cache_size", loaded.HEIGHT_MAP_CACHE_SIZE).toInt();
    s.endGroup();

    s.beginGroup("Misc");
    loaded.LOAD_GLOBAL_DATA = s.value("load_global_data", loaded.LOAD_GLOBAL_DATA).toBool();
    loaded.MAX_GLOBAL_DATA_LOAD_COUNT = s.value("max_global_data_load_count", loaded.MAX_GLOBAL_DATA_LOAD_COUNT).toInt();
    loaded.ICON_THEME = s.value("icon_theme", loaded.ICON_THEME).toString();
    loaded.CHECK_UPDATE = s.value("check_update", loaded.CHECK_UPDATE).toBool();
    s.endGroup();

    s.beginGroup("ExtraFunctions");
    loaded.PRELOAD_ALL_CHUNK_COORDS = s.value("preload_all_chunk_coords", loaded.PRELOAD_ALL_CHUNK_COORDS).toBool();
    s.endGroup();

    s.beginGroup("LeviLauncher");
    loaded.SCAN_LEVI_PATH = s.value("scan_levi_path", loaded.SCAN_LEVI_PATH).toBool();
    s.endGroup();

    s.beginGroup("Lang");
    loaded.LANGUAGE = s.value("lang", loaded.LANGUAGE).toString();
    s.endGroup();

    if (s.status() != QSettings::NoError) {
        LOG_F(WARNING, "Settings read error: %d", s.status());
    }

    LOG_F(INFO, "all keys: %d", static_cast<int>(s.allKeys().size()));

    if (loaded.THREAD_NUM < 1) {
        loaded.THREAD_NUM = 2;
        LOG_F(WARNING, "Invalid background thread number, reset it to default(2)");
    }

    apply(loaded);
}

void setting::save() { save(current()); }

void setting::save(const Settings& values) {
    QSettings s(constant::CONFIG_FILE_PATH.c_str(), QSettings::IniFormat);

    s.beginGroup("Gui");
    s.setValue("theme", values.COLOR_THEME);
    s.setValue("font_family", values.FONT_FAMILY);
    s.setValue("font_size", values.FONT_SIZE);
    s.endGroup();

    s.beginGroup("Rendering");
    s.setValue("render_style", values.MAP_RENDER_STYLE);
    s.setValue("gpu_render_enabled", values.GPU_RENDER_ENABLED);
    s.setValue("gpu_orthographic_view", values.GPU_ORTHOGRAPHIC_VIEW);
    s.setValue("gpu_ao_strength", values.GPU_AO_STRENGTH);
    s.setValue("gpu_bevel_strength", values.GPU_BEVEL_STRENGTH);
    s.setValue("gpu_bevel_width", values.GPU_BEVEL_WIDTH);
    s.setValue("gpu_saturation", values.GPU_SATURATION);
    s.setValue("gpu_brightness", values.GPU_BRIGHTNESS);
    s.setValue("gpu_grass_height_enabled", values.GPU_GRASS_HEIGHT_ENABLED);
    s.setValue("gpu_grass_height_base", values.GPU_GRASS_HEIGHT_BASE);
    s.setValue("gpu_grass_height_color", values.GPU_GRASS_HEIGHT_COLOR);
    s.setValue("gpu_grass_height_range", values.GPU_GRASS_HEIGHT_RANGE);
    s.setValue("gpu_shadow_strength", values.GPU_SHADOW_STRENGTH);
    s.setValue("gpu_shadow_step", values.GPU_SHADOW_STEP);
    s.setValue("gpu_biome_blend_blocks", values.GPU_BIOME_BLEND_BLOCKS);
    s.setValue("tile_render_scale", values.TILE_RENDER_SCALE);
    s.setValue("shadow_pcf_radius", values.SHADOW_PCF_RADIUS);
    s.setValue("shadow_map_scale", values.SHADOW_MAP_SCALE);
    s.setValue("terrian_shadow_level", values.SHADOW_LEVEL);
    s.endGroup();

    s.beginGroup("Map");
    s.setValue("min_scale_level", values.MINIMUM_SCALE_LEVEL);
    s.setValue("max_scale_level", values.MAXIMUM_SCALE_LEVEL);
    s.setValue("coords_minimap_width", values.COORDS_MINIMAP_WIDTH);
    s.setValue("coords_minimap_height", values.COORDS_MINIMAP_HEIGHT);
    s.setValue("grid_line_color", values.GRID_LINE_COLOR);
    s.setValue("actor_render_style", values.ACTOR_RENDER_STYLE);
    s.setValue("actor_border_width", values.ACTOR_BORDER_WIDTH);
    s.setValue("actor_border_color", values.ACTOR_BORDER_COLOR);
    s.setValue("chunk_editor_highlight_color", values.CHUNK_EDITOR_HIGHLIGHT_COLOR);
    s.setValue("chunk_editor_highlight_width", values.CHUNK_EDITOR_HIGHLIGHT_WIDTH);
    s.setValue("void_color", values.VOID_MAP_COLOR);
    s.setValue("voxel_selection_color", values.VOXEL_SELECTION_COLOR);
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
    s.setValue("max_global_data_load_count", values.MAX_GLOBAL_DATA_LOAD_COUNT);
    s.setValue("icon_theme", values.ICON_THEME);
    s.setValue("check_update", values.CHECK_UPDATE);
    s.endGroup();

    s.beginGroup("ExtraFunctions");
    s.setValue("preload_all_chunk_coords", values.PRELOAD_ALL_CHUNK_COORDS);
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
