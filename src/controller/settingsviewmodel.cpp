#include "controller/settingsviewmodel.h"

#include <QColor>
#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>
#include <QVariantMap>
#include <algorithm>

#include "base/config.h"
#include "base/platform.h"
#include "loader/worldsource.h"
#include "model/worldpath.h"

namespace {
    QString sourceAvailabilityName(WorldSourceAvailability availability) {
        switch (availability) {
            case WorldSourceAvailability::Available:
                return QStringLiteral("Available");
            case WorldSourceAvailability::NeedsAllFiles:
                return QStringLiteral("NeedsAllFiles");
            case WorldSourceAvailability::NeedsPrivilege:
                return QStringLiteral("NeedsPrivilege");
            case WorldSourceAvailability::Unavailable:
                return QStringLiteral("Unavailable");
        }
        return {};
    }
}  // namespace

SettingsViewModel::SettingsViewModel(QObject* parent) : QObject(parent), values_(setting::current()) {}

void SettingsViewModel::emitAllChanged() {
    emit colorThemeChanged();
    emit collapseSidebarOnStartupChanged();
    emit languageChanged();
    emit renderBackendChanged();
    emit threadCountChanged();
    emit preloadAllChunkCoordsChanged();
    emit chunkOverviewEnabledChanged();
    emit leveldbXorModeChanged();
    emit leveldbXorKeyChanged();
    emit fontSizeChanged();
    emit accentColorChanged();
    emit gpuOrthographicViewChanged();
    emit gpuShadowStrengthChanged();
    emit gpuAoStrengthChanged();
    emit gpuBevelStrengthChanged();
    emit gpuBevelWidthChanged();
    emit gpuSaturationChanged();
    emit gpuBrightnessChanged();
    emit gpuBiomeBlendBlocksChanged();
    emit gpuShadowStepChanged();
    emit gpuTileSnapshotCacheSizeMBChanged();
    emit cpuShadowLevelChanged();
    emit minimumScaleLevelChanged();
    emit maximumScaleLevelChanged();
    emit panInertiaChanged();
    emit gridLineColorChanged();
    emit mapSelectionColorChanged();
    emit voidMapColorChanged();
    emit tileColorSchemeChanged();
    emit actorRenderStyleChanged();
    emit actorBorderWidthChanged();
    emit actorBorderColorChanged();
    emit chunkEditorHighlightColorChanged();
    emit chunkEditorHighlightWidthChanged();
    emit voxelSelectionColorChanged();
    emit regionCacheSizeChanged();
    emit emptyRegionCacheSizeChanged();
    emit loadGlobalDataChanged();
    emit maxGlobalDataLoadCountChanged();
    emit transparentWaterChanged();
    emit gpuGrassHeightEnabledChanged();
    emit gpuGrassHeightBaseChanged();
    emit gpuGrassHeightRangeChanged();
    emit gpuGrassHeightColorChanged();
    emit heightMapCacheSizeChanged();
    emit checkUpdateChanged();
    emit scanLeviPathChanged();
    emit customWorldPathsChanged();
    emit worldSourcesChanged();
}

int SettingsViewModel::defaultFontSize() const { return constant::DEFAULT_FONT_POINT_SIZE; }

void SettingsViewModel::applyFont() {
    // setting::apply() has already published the new values.
    setting::setupUiFont();
}

void SettingsViewModel::setSettings(const setting::Settings& values) {
    values_ = values;
    values_.minimumScaleLevel = setting::normalizeZoomScale(values_.minimumScaleLevel);
    values_.maximumScaleLevel = std::max(values_.minimumScaleLevel, setting::normalizeZoomScale(values_.maximumScaleLevel));
    emitAllChanged();
}
void SettingsViewModel::reload() {
    values_ = setting::current();
    emitAllChanged();
}
bool SettingsViewModel::save() {
    setting::apply(values_);
    setting::save(values_);
    applyFont();
    emit saved();
    return true;
}
bool SettingsViewModel::saveWorldSources() {
    setting::Settings persisted = setting::current();
    persisted.customWorldPaths = values_.customWorldPaths;
    setting::apply(persisted);
    setting::save(persisted);
    emit saved();
    return true;
}
void SettingsViewModel::refreshWorldSources() { emit worldSourcesChanged(); }
void SettingsViewModel::reset() {
    values_ = setting::Settings{};
    emitAllChanged();
}
QVariantList SettingsViewModel::worldSources() const {
    QVariantList result;
    for (const auto& source : makeWorldSources(values_.customWorldPaths, values_.scanLeviPath,
                                               platform::hasAllFilesAccess())) {
        for (const auto& path : source.roots) {
            QVariantMap entry;
            entry.insert(QStringLiteral("path"), path);
            entry.insert(QStringLiteral("availability"), sourceAvailabilityName(source.availability));
            entry.insert(QStringLiteral("removable"), source.kind == WorldSourceKind::UserFolder);
            result.append(entry);
        }
    }
    return result;
}
void SettingsViewModel::setColorTheme(const QString& v) {
    if (values_.colorTheme == v) return;
    values_.colorTheme = v;
    emit colorThemeChanged();
}
void SettingsViewModel::setTileColorScheme(const QString& v) {
    if ((v != QStringLiteral("dark") && v != QStringLiteral("light")) || values_.tileColorScheme == v) return;
    values_.tileColorScheme = v;
    emit tileColorSchemeChanged();
}
void SettingsViewModel::setAccentColor(const QString& v) {
    // An empty value clears the override; anything else has to be a colour the
    // QML side can use, so invalid text is ignored instead of being persisted.
    const QString trimmed = v.trimmed();
    if (trimmed.isEmpty()) {
        if (values_.accentColor.isEmpty()) return;
        values_.accentColor.clear();
        emit accentColorChanged();
        return;
    }
    if (!QColor::isValidColorName(trimmed) || values_.accentColor == trimmed) return;
    values_.accentColor = trimmed;
    emit accentColorChanged();
}
void SettingsViewModel::setLanguage(const QString& v) {
    if (values_.language == v) return;
    values_.language = v;
    emit languageChanged();
}
void SettingsViewModel::setRenderBackend(const QString& v) {
    const QString normalized = v.trimmed().toLower();
    if (normalized != QStringLiteral("automatic") && normalized != QStringLiteral("opengl") &&
        normalized != QStringLiteral("vulkan") && normalized != QStringLiteral("d3d11")) {
        return;
    }
    if (values_.renderBackend == normalized) return;
    values_.renderBackend = normalized;
    emit renderBackendChanged();
}
void SettingsViewModel::setThreadCount(int v) {
    v = std::max(1, v);
    if (values_.threadCount == v) return;
    values_.threadCount = v;
    emit threadCountChanged();
}
void SettingsViewModel::setMinimumScaleLevel(int v) {
    v = std::min(setting::normalizeZoomScale(v), values_.maximumScaleLevel);
    if (values_.minimumScaleLevel == v) return;
    values_.minimumScaleLevel = v;
    emit minimumScaleLevelChanged();
}
void SettingsViewModel::setMaximumScaleLevel(int v) {
    v = std::max(setting::normalizeZoomScale(v), values_.minimumScaleLevel);
    if (values_.maximumScaleLevel == v) return;
    values_.maximumScaleLevel = v;
    emit maximumScaleLevelChanged();
}
void SettingsViewModel::setPreloadAllChunkCoords(bool v) {
    if (values_.preloadAllChunkCoords == v) return;
    values_.preloadAllChunkCoords = v;
    emit preloadAllChunkCoordsChanged();
}
void SettingsViewModel::setLeveldbXorMode(int v) {
    v = std::clamp(v, 0, 2);
    if (values_.leveldbXorMode == v) return;
    values_.leveldbXorMode = v;
    emit leveldbXorModeChanged();
}
void SettingsViewModel::setLeveldbXorKey(const QString& v) {
    if (values_.leveldbXorKey == v) return;
    values_.leveldbXorKey = v;
    emit leveldbXorKeyChanged();
}

#define BM_SET_STRING(method, field, signal)           \
    void SettingsViewModel::method(const QString& v) { \
        if (values_.field == v) return;                \
        values_.field = v;                             \
        emit signal();                                 \
    }
#define BM_SET_INT(method, field, signal, minValue, maxValue) \
    void SettingsViewModel::method(int v) {                   \
        v = std::clamp(v, minValue, maxValue);                \
        if (values_.field == v) return;                       \
        values_.field = v;                                    \
        emit signal();                                        \
    }
#define BM_SET_BOOL(method, field, signal)   \
    void SettingsViewModel::method(bool v) { \
        if (values_.field == v) return;      \
        values_.field = v;                   \
        emit signal();                       \
    }
#define BM_SET_DOUBLE(method, field, signal, minValue, maxValue) \
    void SettingsViewModel::method(double v) {                   \
        v = std::clamp(v, minValue, maxValue);                   \
        if (values_.field == static_cast<float>(v)) return;      \
        values_.field = static_cast<float>(v);                   \
        emit signal();                                           \
    }

BM_SET_BOOL(setCollapseSidebarOnStartup, collapseSidebarOnStartup, collapseSidebarOnStartupChanged)
BM_SET_INT(setFontSize, fontSize, fontSizeChanged, 6, 96)
BM_SET_BOOL(setGpuOrthographicView, gpuOrthographicView, gpuOrthographicViewChanged)
BM_SET_BOOL(setChunkOverviewEnabled, chunkOverviewEnabled, chunkOverviewEnabledChanged)
BM_SET_DOUBLE(setGpuShadowStrength, gpuShadowStrength, gpuShadowStrengthChanged, 0.0, 1.0)
BM_SET_DOUBLE(setGpuAoStrength, gpuAoStrength, gpuAoStrengthChanged, 0.0, 1.0)
BM_SET_DOUBLE(setGpuBevelStrength, gpuBevelStrength, gpuBevelStrengthChanged, 0.0, 1.0)
BM_SET_DOUBLE(setGpuBevelWidth, gpuBevelWidth, gpuBevelWidthChanged, 0.25, 2.0)
BM_SET_DOUBLE(setGpuSaturation, gpuSaturation, gpuSaturationChanged, 0.0, 2.0)
BM_SET_DOUBLE(setGpuBrightness, gpuBrightness, gpuBrightnessChanged, 0.0, 2.0)
BM_SET_DOUBLE(setGpuBiomeBlendBlocks, gpuBiomeBlendBlocks, gpuBiomeBlendBlocksChanged, 0.0, 16.0)
BM_SET_INT(setGpuShadowStep, gpuShadowStep, gpuShadowStepChanged, 4, 32)
BM_SET_INT(setGpuTileSnapshotCacheSizeMB, gpuTileSnapshotCacheSizeMB, gpuTileSnapshotCacheSizeMBChanged, 8, 512)
BM_SET_INT(setCpuShadowLevel, cpuShadowLevel, cpuShadowLevelChanged, 0, 255)
BM_SET_DOUBLE(setPanInertia, panInertia, panInertiaChanged, 0.0, 1.0)
void SettingsViewModel::setMapSelectionColor(const QString& value) {
    const QColor color(value.trimmed());
    if (!color.isValid()) return;
    const QString rgba = color.name(QColor::HexArgb);
    if (values_.mapSelectionColor == rgba) return;
    values_.mapSelectionColor = rgba;
    emit mapSelectionColorChanged();
}

BM_SET_STRING(setGridLineColor, gridLineColor, gridLineColorChanged)
BM_SET_STRING(setVoidMapColor, voidMapColor, voidMapColorChanged)
BM_SET_INT(setActorRenderStyle, actorRenderStyle, actorRenderStyleChanged, 0, 1)
BM_SET_INT(setActorBorderWidth, actorBorderWidth, actorBorderWidthChanged, 0, 32)
BM_SET_STRING(setActorBorderColor, actorBorderColor, actorBorderColorChanged)
BM_SET_STRING(setChunkEditorHighlightColor, chunkEditorHighlightColor, chunkEditorHighlightColorChanged)
BM_SET_INT(setChunkEditorHighlightWidth, chunkEditorHighlightWidth, chunkEditorHighlightWidthChanged, 0, 64)
BM_SET_STRING(setVoxelSelectionColor, voxelSelectionColor, voxelSelectionColorChanged)
BM_SET_INT(setRegionCacheSize, regionCacheSize, regionCacheSizeChanged, 0, 10000000)
BM_SET_INT(setEmptyRegionCacheSize, emptyRegionCacheSize, emptyRegionCacheSizeChanged, 0, 10000000)
BM_SET_BOOL(setLoadGlobalData, loadGlobalData, loadGlobalDataChanged)
BM_SET_INT(setMaxGlobalDataLoadCount, maxGlobalDataLoadCount, maxGlobalDataLoadCountChanged, 0, 10000000)
BM_SET_BOOL(setTransparentWater, transparentWater, transparentWaterChanged)
BM_SET_BOOL(setGpuGrassHeightEnabled, gpuGrassHeightEnabled, gpuGrassHeightEnabledChanged)
BM_SET_DOUBLE(setGpuGrassHeightBase, gpuGrassHeightBase, gpuGrassHeightBaseChanged, -128.0, 512.0)
BM_SET_DOUBLE(setGpuGrassHeightRange, gpuGrassHeightRange, gpuGrassHeightRangeChanged, 1.0, 512.0)
BM_SET_STRING(setGpuGrassHeightColor, gpuGrassHeightColor, gpuGrassHeightColorChanged)
BM_SET_INT(setHeightMapCacheSize, heightMapCacheSize, heightMapCacheSizeChanged, 0, 10000000)
BM_SET_BOOL(setCheckUpdate, checkUpdate, checkUpdateChanged)

void SettingsViewModel::setScanLeviPath(bool v) {
    if (values_.scanLeviPath == v) return;
    values_.scanLeviPath = v;
    emit scanLeviPathChanged();
    emit worldSourcesChanged();
}

void SettingsViewModel::addCustomWorldPath(const QString& path) {
    const QString normalized = world_path::normalize(path.trimmed());
    if (normalized.isEmpty()) return;
    const QString key = world_path::identityKey(normalized);
    for (const auto& existing : values_.customWorldPaths) {
        if (world_path::identityKey(world_path::normalize(existing)) == key) return;
    }
    values_.customWorldPaths.append(normalized);
    emit customWorldPathsChanged();
    emit worldSourcesChanged();
}

void SettingsViewModel::removeCustomWorldPath(const QString& path) {
    const QString normalized = world_path::normalize(path.trimmed());
    if (normalized.isEmpty()) return;
    const QString key = world_path::identityKey(normalized);
    for (int i = 0; i < values_.customWorldPaths.size(); ++i) {
        if (world_path::identityKey(world_path::normalize(values_.customWorldPaths.at(i))) == key) {
            values_.customWorldPaths.removeAt(i);
            emit customWorldPathsChanged();
            emit worldSourcesChanged();
            return;
        }
    }
}

#undef BM_SET_STRING
#undef BM_SET_INT
#undef BM_SET_BOOL
#undef BM_SET_DOUBLE
