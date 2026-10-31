//
// Created by xhy on 2023/7/11.
//
#include "base/config.h"

#include <qchar.h>
#include <qcolor.h>
#include <qcontainerfwd.h>
#include <qglobal.h>
#include <qimage.h>
#include <qmath.h>
#include <qnamespace.h>
#include <qnumeric.h>
#include <qsettings.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QString>
#include <algorithm>
#include <string>

#include "base/apppaths.h"
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
const AppVersion& constant::softwareVersion() {
    static const AppVersion version = [] {
        const auto parsed = AppVersion::parse(QStringLiteral(BEDROCKMAP_VERSION));
        return parsed.value_or(AppVersion{{0, 0, 0}});
    }();
    return version;
}

region_pos constant::c2r(const bl::chunk_pos& ch) {
    auto cx = ch.x < 0 ? ch.x - constant::RW + 1 : ch.x;
    auto cz = ch.z < 0 ? ch.z - constant::RW + 1 : ch.z;
    return region_pos{cx / constant::RW * constant::RW, cz / constant::RW * constant::RW, ch.dim};
}

QString constant::VERSION_STRING() {
    return QString::fromUtf8(constant::SOFTWARE_NAME) + " " + constant::softwareVersion().toString() + "." + QString(GIT_COMMIT_HASH);
}

namespace {
    setting::Settings& mutableSettings() {
        static setting::Settings instance;
        return instance;
    }

    quint64& mutableRevision() {
        static quint64 revision = 0;
        return revision;
    }
}  // namespace

void setting::apply(const Settings& settings) {
    mutableSettings() = settings;
    ++mutableRevision();
}

const setting::Settings& setting::current() { return mutableSettings(); }

quint64 setting::revision() { return mutableRevision(); }

// Utility functions
void constant::initColorTable() {
    const auto loadResource = [](const QString& path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll().toStdString() : std::string{};
    };
    const auto biomeJson = loadResource(QStringLiteral(":/bedrockmap/data/colors/biome_color.json"));
    const auto blockJson = loadResource(QStringLiteral(":/bedrockmap/data/colors/block_color.json"));
    if (biomeJson.empty() || !bl::init_biome_color_palette_from_json(biomeJson))
        LOG_F(WARNING, "Can not load embedded biome color resource");
    if (blockJson.empty() || !bl::init_block_color_from_json(blockJson)) LOG_F(WARNING, "Can not load embedded block color resource");
    bl::config::set_log_missing_block_color(false);
    bl::config::set_log_mismatched_actor(false);
}

void setting::init() {
    app_paths::initialize();
    const auto path = app_paths::configFile();
    LOG_F(INFO, "Current working directory: %s", QDir::currentPath().toStdString().c_str());
    LOG_F(INFO, "Configuration file path: %s", path.toStdString().c_str());

    if (!QFile::exists(path)) LOG_F(INFO, "Config file not found, using defaults");
    setting::load();
}

void setting::load() {
    QSettings s(app_paths::configFile(), QSettings::IniFormat);

    foreach (const auto& key, s.allKeys()) {
        LOG_F(INFO, "Config key: %s, value: %s", key.toStdString().c_str(), s.value(key).toString().toStdString().c_str());
    }

    // Parse into a copy so a partial/failed read can never tear the live snapshot.
    Settings loaded = current();

    s.beginGroup("Gui");
    loaded.colorTheme = s.value("theme", loaded.colorTheme).toString();
    loaded.collapseSidebarOnStartup = s.value("collapse_sidebar_on_startup", loaded.collapseSidebarOnStartup).toBool();
    loaded.fontSize = s.value("font_size", loaded.fontSize).toInt();
    loaded.accentColor = s.value("accent_color", loaded.accentColor).toString();
    loaded.language = s.value("lang", loaded.language).toString();
    s.endGroup();

    s.beginGroup("Map");
    loaded.showMapDebugButton = s.value("show_debug_button", loaded.showMapDebugButton).toBool();
    loaded.minimumScaleLevel = normalizeZoomScale(s.value("min_scale_level", loaded.minimumScaleLevel).toInt());
    loaded.maximumScaleLevel =
        std::max(loaded.minimumScaleLevel, normalizeZoomScale(s.value("max_scale_level", loaded.maximumScaleLevel).toInt()));
    loaded.panInertia = std::clamp(s.value("pan_inertia", loaded.panInertia).toFloat(), 0.0f, 1.0f);
    loaded.gridLineColor = s.value("grid_line_color", loaded.gridLineColor).toString();
    const QColor map_selection_color(s.value("selection_color", loaded.mapSelectionColor).toString());
    if (map_selection_color.isValid()) loaded.mapSelectionColor = map_selection_color.name(QColor::HexArgb);
    loaded.actorRenderStyle = s.value("actor_render_style", loaded.actorRenderStyle).toInt();
    loaded.actorBorderWidth = s.value("actor_border_width", loaded.actorBorderWidth).toInt();
    loaded.actorBorderColor = s.value("actor_border_color", loaded.actorBorderColor).toString();
    loaded.chunkEditorHighlightColor = s.value("chunk_editor_highlight_color", loaded.chunkEditorHighlightColor).toString();
    loaded.chunkEditorHighlightWidth = s.value("chunk_editor_highlight_width", loaded.chunkEditorHighlightWidth).toInt();
    loaded.voidMapColor = s.value("void_color", loaded.voidMapColor).toString();
    loaded.tileColorScheme = s.value("tile_color_scheme", loaded.tileColorScheme).toString() == QStringLiteral("light")
                                 ? QStringLiteral("light")
                                 : QStringLiteral("dark");
    loaded.voxelSelectionColor = s.value("voxel_selection_color", loaded.voxelSelectionColor).toString();
    loaded.transparentWater = s.value("transparent_water", loaded.transparentWater).toBool();
    s.endGroup();

    s.beginGroup("Rendering");
    loaded.renderBackend = s.value("qsg_rhi_backend", loaded.renderBackend).toString().trimmed().toLower();
    if (loaded.renderBackend != QStringLiteral("automatic") && loaded.renderBackend != QStringLiteral("opengl") &&
        loaded.renderBackend != QStringLiteral("vulkan") && loaded.renderBackend != QStringLiteral("d3d11")) {
        loaded.renderBackend = QStringLiteral("automatic");
    }
#if defined(Q_OS_ANDROID)
    if (loaded.renderBackend == QStringLiteral("d3d11")) loaded.renderBackend = QStringLiteral("opengl");
#elif !defined(Q_OS_WIN)
    if (loaded.renderBackend == QStringLiteral("d3d11")) loaded.renderBackend = QStringLiteral("automatic");
#endif
    loaded.gpuOrthographicView = s.value("gpu_orthographic_view", loaded.gpuOrthographicView).toBool();
    const float stored_ao_strength = s.value("gpu_ao_strength", loaded.gpuAoStrength).toFloat();
    loaded.gpuAoStrength = std::clamp(stored_ao_strength, 0.0f, 1.0f);
    loaded.gpuBevelStrength = std::clamp(s.value("gpu_bevel_strength", loaded.gpuBevelStrength).toFloat(), 0.0f, 1.0f);
    loaded.gpuBevelWidth = std::clamp(s.value("gpu_bevel_width", loaded.gpuBevelWidth).toFloat(), 0.25f, 2.0f);
    loaded.gpuSaturation = std::clamp(s.value("gpu_saturation", loaded.gpuSaturation).toFloat(), 0.0f, 2.0f);
    loaded.gpuBrightness = std::clamp(s.value("gpu_brightness", loaded.gpuBrightness).toFloat(), 0.0f, 2.0f);
    loaded.gpuGrassHeightEnabled = s.value("gpu_grass_height_enabled", loaded.gpuGrassHeightEnabled).toBool();
    loaded.gpuGrassHeightBase = s.value("gpu_grass_height_base", loaded.gpuGrassHeightBase).toFloat();
    loaded.gpuGrassHeightColor = s.value("gpu_grass_height_color", loaded.gpuGrassHeightColor).toString();
    loaded.gpuGrassHeightRange = std::clamp(s.value("gpu_grass_height_range", loaded.gpuGrassHeightRange).toFloat(), 1.0f, 4096.0f);
    const float stored_shadow_strength = s.value("gpu_shadow_strength", loaded.gpuShadowStrength).toFloat();
    loaded.gpuShadowStrength = std::clamp(stored_shadow_strength, 0.0f, 1.0f);
#ifdef Q_OS_ANDROID
    // Existing Android installs may have the old desktop defaults persisted.
    // Migrate only those untouched defaults once; explicit user values remain.
    if (!s.value("android_ao_shadow_defaults_migrated", false).toBool()) {
        if (!s.contains("gpu_ao_strength") || qAbs(stored_ao_strength - 0.15f) < 0.0001f) loaded.gpuAoStrength = 0.0f;
        if (!s.contains("gpu_shadow_strength") || qAbs(stored_shadow_strength - 0.7f) < 0.0001f) loaded.gpuShadowStrength = 0.0f;
        s.setValue("android_ao_shadow_defaults_migrated", true);
        s.sync();
    }
#endif
    const int requested_gpu_shadow_step = std::clamp(s.value("gpu_shadow_step", loaded.gpuShadowStep).toInt(), 4, 32);
    loaded.gpuShadowStep = requested_gpu_shadow_step <= 4    ? 4
                           : requested_gpu_shadow_step <= 8  ? 8
                           : requested_gpu_shadow_step <= 16 ? 16
                                                             : 32;
    loaded.gpuBiomeBlendBlocks = std::clamp(s.value("gpu_biome_blend_blocks", loaded.gpuBiomeBlendBlocks).toFloat(), 0.0f, 16.0f);
    loaded.gpuTileSnapshotCacheSizeMB =
        std::clamp(s.value("gpu_tile_snapshot_cache_mb", loaded.gpuTileSnapshotCacheSizeMB).toInt(), 8, 512);
    loaded.gpuHighDetailMap = s.value("gpu_high_detail_map", loaded.gpuHighDetailMap).toBool();
    loaded.cpuShadowLevel = s.value("cpu_shadow_level", loaded.cpuShadowLevel).toInt();
    s.endGroup();

    s.beginGroup("Cache");
    loaded.regionCacheSize = s.value("region_cache_size", loaded.regionCacheSize).toInt();
    loaded.emptyRegionCacheSize = s.value("empty_cache_size", loaded.emptyRegionCacheSize).toInt();
    loaded.threadCount = s.value("max_thread_num", loaded.threadCount).toInt();
    s.endGroup();

    s.beginGroup("Misc");
    loaded.loadGlobalData = s.value("load_global_data", loaded.loadGlobalData).toBool();
    loaded.preloadAllChunkCoords = s.value("preload_all_chunk_coords", loaded.preloadAllChunkCoords).toBool();
    loaded.chunkOverviewEnabled = s.value("chunk_overview_enabled", loaded.chunkOverviewEnabled).toBool();
    loaded.maxGlobalDataLoadCount = s.value("max_global_data_load_count", loaded.maxGlobalDataLoadCount).toInt();
    loaded.checkUpdate = s.value("check_update", loaded.checkUpdate).toBool();
    loaded.leveldbXorMode = std::clamp(s.value("leveldb_xor_mode", loaded.leveldbXorMode).toInt(), 0, 2);
    loaded.leveldbXorKey = s.value("leveldb_xor_key", loaded.leveldbXorKey).toString();
    s.endGroup();

    s.beginGroup("LeviLauncher");
    loaded.scanLeviPath = s.value("scan_levi_path", loaded.scanLeviPath).toBool();
    s.endGroup();

    s.beginGroup("WorldSources");
    loaded.customWorldPaths = s.value("custom_paths", loaded.customWorldPaths).toStringList();
    s.endGroup();

    if (s.status() != QSettings::NoError) {
        LOG_F(WARNING, "Settings read error: %d", s.status());
    }

    LOG_F(INFO, "all keys: %d", static_cast<int>(s.allKeys().size()));

    if (loaded.threadCount < 1) {
        loaded.threadCount = 2;
        LOG_F(WARNING, "Invalid background thread number, reset it to default(2)");
    }

    apply(loaded);
}

void setting::save() { save(current()); }

void setting::save(const Settings& values) {
    QSettings s(app_paths::configFile(), QSettings::IniFormat);

    s.beginGroup("Gui");
    s.setValue("theme", values.colorTheme);
    s.setValue("collapse_sidebar_on_startup", values.collapseSidebarOnStartup);
    s.remove("font_family");
    s.setValue("font_size", values.fontSize);
    s.setValue("accent_color", values.accentColor);
    s.setValue("lang", values.language);
    s.endGroup();

    s.beginGroup("Rendering");
    s.setValue("qsg_rhi_backend", values.renderBackend);
    s.setValue("gpu_orthographic_view", values.gpuOrthographicView);
    s.setValue("gpu_ao_strength", values.gpuAoStrength);
    s.setValue("gpu_bevel_strength", values.gpuBevelStrength);
    s.setValue("gpu_bevel_width", values.gpuBevelWidth);
    s.setValue("gpu_saturation", values.gpuSaturation);
    s.setValue("gpu_brightness", values.gpuBrightness);
    s.setValue("gpu_grass_height_enabled", values.gpuGrassHeightEnabled);
    s.setValue("gpu_grass_height_base", values.gpuGrassHeightBase);
    s.setValue("gpu_grass_height_color", values.gpuGrassHeightColor);
    s.setValue("gpu_grass_height_range", values.gpuGrassHeightRange);
    s.setValue("gpu_shadow_strength", values.gpuShadowStrength);
    s.setValue("gpu_shadow_step", values.gpuShadowStep);
    s.setValue("gpu_biome_blend_blocks", values.gpuBiomeBlendBlocks);
    s.setValue("gpu_tile_snapshot_cache_mb", values.gpuTileSnapshotCacheSizeMB);
    s.setValue("gpu_high_detail_map", values.gpuHighDetailMap);
    s.setValue("cpu_shadow_level", values.cpuShadowLevel);
    s.endGroup();

    s.beginGroup("Map");
    s.setValue("show_debug_button", values.showMapDebugButton);
    s.setValue("min_scale_level", values.minimumScaleLevel);
    s.setValue("max_scale_level", values.maximumScaleLevel);
    s.setValue("pan_inertia", values.panInertia);
    s.setValue("grid_line_color", values.gridLineColor);
    s.setValue("selection_color", values.mapSelectionColor);
    s.setValue("actor_render_style", values.actorRenderStyle);
    s.setValue("actor_border_width", values.actorBorderWidth);
    s.setValue("actor_border_color", values.actorBorderColor);
    s.setValue("chunk_editor_highlight_color", values.chunkEditorHighlightColor);
    s.setValue("chunk_editor_highlight_width", values.chunkEditorHighlightWidth);
    s.setValue("void_color", values.voidMapColor);
    s.setValue("tile_color_scheme", values.tileColorScheme);
    s.setValue("voxel_selection_color", values.voxelSelectionColor);
    s.setValue("transparent_water", values.transparentWater);
    s.endGroup();

    s.beginGroup("Cache");
    s.setValue("region_cache_size", values.regionCacheSize);
    s.setValue("empty_cache_size", values.emptyRegionCacheSize);
    s.setValue("max_thread_num", values.threadCount);
    s.endGroup();

    s.beginGroup("Misc");
    s.setValue("load_global_data", values.loadGlobalData);
    s.setValue("preload_all_chunk_coords", values.preloadAllChunkCoords);
    s.setValue("chunk_overview_enabled", values.chunkOverviewEnabled);
    s.setValue("max_global_data_load_count", values.maxGlobalDataLoadCount);
    s.setValue("check_update", values.checkUpdate);
    s.setValue("leveldb_xor_mode", values.leveldbXorMode);
    s.setValue("leveldb_xor_key", values.leveldbXorKey);
    s.endGroup();

    s.beginGroup("LeviLauncher");
    s.setValue("scan_levi_path", values.scanLeviPath);
    s.endGroup();

    s.beginGroup("WorldSources");
    s.setValue("custom_paths", values.customWorldPaths);
    s.endGroup();

    s.sync();

    LOG_F(INFO, "Settings saved to %s", app_paths::configFile().toStdString().c_str());
}

void setting::setupUiFont() {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    static const QString uiFamily = [] {
        const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(constant::UI_FONT_RESOURCE));
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (families.isEmpty()) {
            LOG_F(ERROR, "Can not load UI font %s", constant::UI_FONT_RESOURCE);
            return QString{};
        }
        LOG_F(INFO, "Bundled UI font: %s", families.first().toUtf8().constData());
        return families.first();
    }();
#endif
    static const int monospaceFontId = QFontDatabase::addApplicationFont(QString::fromLatin1(constant::MONOSPACE_FONT_RESOURCE));
    if (monospaceFontId == -1) LOG_F(WARNING, "Can not load font %s", constant::MONOSPACE_FONT_RESOURCE);

    QFont font = QGuiApplication::font();
    QString fallbackFamily = font.family();
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (!uiFamily.isEmpty()) {
        font.setFamily(uiFamily);
        fallbackFamily = uiFamily;
    }
#endif
    // Monospace editor text keeps its Latin glyphs and falls back to the normal
    // UI font for Han characters and unsupported punctuation.
    if (!fallbackFamily.isEmpty()) {
        const QStringList fallback{fallbackFamily};
        for (const auto script : {QChar::Script_Han, QChar::Script_Common}) {
            if (QFontDatabase::applicationFallbackFontFamilies(script) != fallback)
                QFontDatabase::setApplicationFallbackFontFamilies(script, fallback);
        }
    }
    font.setPointSize(current().fontSize > 0 ? current().fontSize : constant::DEFAULT_FONT_POINT_SIZE);
    QGuiApplication::setFont(font);
}
