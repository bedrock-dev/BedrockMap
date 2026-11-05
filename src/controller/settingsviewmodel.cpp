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

SettingsViewModel::SettingsViewModel(QObject* parent) : QObject(parent), values_(setting::current()) {}

void SettingsViewModel::emitAllChanged() {
    emit colorThemeChanged();
    emit languageChanged();
    emit fontSizeChanged();
    emit accentColorChanged();
    emit settingsChanged();
}

int SettingsViewModel::defaultFontSize() const { return constant::DEFAULT_FONT_POINT_SIZE; }

bool SettingsViewModel::save() {
    const QString error = setting::save(values_);
    if (!error.isEmpty()) {
        emit errorChanged(error);
        return false;
    }
    emit settingsSaved();
    return true;
}
bool SettingsViewModel::saveWorldSources() {
    setting::Settings persisted = setting::read();
    persisted.customWorldPaths = values_.customWorldPaths;
    const QString error = setting::save(persisted);
    if (!error.isEmpty()) {
        emit errorChanged(error);
        return false;
    }
    emit settingsSaved();
    return true;
}
void SettingsViewModel::reset() {
    values_ = setting::Settings{};
    emitAllChanged();
}
QVariantList SettingsViewModel::worldSources() const {
    QVariantList result;
    for (const auto& source : makeWorldSources(values_.customWorldPaths, values_.scanLeviPath)) {
        QVariantMap entry;
        entry.insert(QStringLiteral("path"), source.path);
        entry.insert(QStringLiteral("label"), worldSourceLabel(source.kind));
        entry.insert(QStringLiteral("removable"), source.kind == WorldSourceKind::External);
        result.append(entry);
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
    emit settingsChanged();
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
    if (normalized != QStringLiteral("automatic") && normalized != QStringLiteral("opengl") && normalized != QStringLiteral("vulkan") &&
        normalized != QStringLiteral("d3d11")) {
        return;
    }
    if (values_.renderBackend == normalized) return;
    values_.renderBackend = normalized;
    emit settingsChanged();
}
void SettingsViewModel::setThreadCount(int v) {
    v = std::max(1, v);
    if (values_.threadCount == v) return;
    values_.threadCount = v;
    emit settingsChanged();
}
void SettingsViewModel::setMinimumScaleLevel(int v) {
    v = std::min(setting::normalizeZoomScale(v), values_.maximumScaleLevel);
    if (values_.minimumScaleLevel == v) return;
    values_.minimumScaleLevel = v;
    emit settingsChanged();
}
void SettingsViewModel::setMaximumScaleLevel(int v) {
    v = std::max(setting::normalizeZoomScale(v), values_.minimumScaleLevel);
    if (values_.maximumScaleLevel == v) return;
    values_.maximumScaleLevel = v;
    emit settingsChanged();
}
void SettingsViewModel::setPreloadAllChunkCoords(bool v) {
    if (values_.preloadAllChunkCoords == v) return;
    values_.preloadAllChunkCoords = v;
    emit settingsChanged();
}
void SettingsViewModel::setLeveldbXorMode(int v) {
    v = std::clamp(v, 0, 2);
    if (values_.leveldbXorMode == v) return;
    values_.leveldbXorMode = v;
    emit settingsChanged();
}
void SettingsViewModel::setLeveldbXorKey(const QString& v) {
    if (values_.leveldbXorKey == v) return;
    values_.leveldbXorKey = v;
    emit settingsChanged();
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

BM_SET_BOOL(setCollapseSidebarOnStartup, collapseSidebarOnStartup, settingsChanged)
BM_SET_BOOL(setShowMapDebugButton, showMapDebugButton, settingsChanged)
BM_SET_INT(setFontSize, fontSize, fontSizeChanged, 6, 96)
BM_SET_BOOL(setGpuOrthographicView, gpuOrthographicView, settingsChanged)
BM_SET_BOOL(setChunkOverviewEnabled, chunkOverviewEnabled, settingsChanged)
BM_SET_DOUBLE(setGpuShadowStrength, gpuShadowStrength, settingsChanged, 0.0, 1.0)
BM_SET_DOUBLE(setGpuAoStrength, gpuAoStrength, settingsChanged, 0.0, 1.0)
void SettingsViewModel::setGpuAoSteps(int value) {
    value = setting::normalizeAoSteps(value);
    if (values_.gpuAoSteps == value) return;
    values_.gpuAoSteps = value;
    emit settingsChanged();
}

BM_SET_DOUBLE(setGpuBevelStrength, gpuBevelStrength, settingsChanged, 0.0, 1.0)
BM_SET_DOUBLE(setGpuBevelWidth, gpuBevelWidth, settingsChanged, 0.25, 2.0)
BM_SET_DOUBLE(setGpuSaturation, gpuSaturation, settingsChanged, 0.0, 2.0)
BM_SET_DOUBLE(setGpuBrightness, gpuBrightness, settingsChanged, 0.0, 2.0)
BM_SET_DOUBLE(setGpuBiomeBlendBlocks, gpuBiomeBlendBlocks, settingsChanged, 0.0, 16.0)
BM_SET_INT(setGpuShadowStep, gpuShadowStep, settingsChanged, 4, 32)
BM_SET_INT(setGpuTileSnapshotCacheSizeMB, gpuTileSnapshotCacheSizeMB, settingsChanged, 8, 512)
BM_SET_BOOL(setGpuHighDetailMap, gpuHighDetailMap, settingsChanged)
BM_SET_INT(setCpuShadowLevel, cpuShadowLevel, settingsChanged, 0, 255)
BM_SET_DOUBLE(setPanInertia, panInertia, settingsChanged, 0.0, 1.0)
void SettingsViewModel::setMapSelectionColor(const QString& value) {
    const QColor color(value.trimmed());
    if (!color.isValid()) return;
    const QString rgba = color.name(QColor::HexArgb);
    if (values_.mapSelectionColor == rgba) return;
    values_.mapSelectionColor = rgba;
    emit settingsChanged();
}

BM_SET_STRING(setGridLineColor, gridLineColor, settingsChanged)
BM_SET_STRING(setVoidMapColor, voidMapColor, settingsChanged)
BM_SET_INT(setActorRenderStyle, actorRenderStyle, settingsChanged, 0, 1)
BM_SET_INT(setActorBorderWidth, actorBorderWidth, settingsChanged, 0, 32)
BM_SET_STRING(setActorBorderColor, actorBorderColor, settingsChanged)
BM_SET_STRING(setChunkEditorHighlightColor, chunkEditorHighlightColor, settingsChanged)
BM_SET_INT(setChunkEditorHighlightWidth, chunkEditorHighlightWidth, settingsChanged, 0, 64)
BM_SET_STRING(setVoxelSelectionColor, voxelSelectionColor, settingsChanged)
BM_SET_INT(setRegionCacheSize, regionCacheSize, settingsChanged, 0, 10000000)
BM_SET_INT(setEmptyRegionCacheSize, emptyRegionCacheSize, settingsChanged, 0, 10000000)
BM_SET_BOOL(setLoadGlobalData, loadGlobalData, settingsChanged)
BM_SET_INT(setMaxGlobalDataLoadCount, maxGlobalDataLoadCount, settingsChanged, 0, 10000000)
BM_SET_BOOL(setTransparentWater, transparentWater, settingsChanged)
BM_SET_BOOL(setGpuGrassHeightEnabled, gpuGrassHeightEnabled, settingsChanged)
BM_SET_DOUBLE(setGpuGrassHeightBase, gpuGrassHeightBase, settingsChanged, -128.0, 512.0)
BM_SET_DOUBLE(setGpuGrassHeightRange, gpuGrassHeightRange, settingsChanged, 1.0, 512.0)
BM_SET_STRING(setGpuGrassHeightColor, gpuGrassHeightColor, settingsChanged)
BM_SET_BOOL(setCheckUpdate, checkUpdate, settingsChanged)

void SettingsViewModel::setScanLeviPath(bool v) {
    if (values_.scanLeviPath == v) return;
    values_.scanLeviPath = v;
    emit settingsChanged();
}

void SettingsViewModel::addCustomWorldPath(const QString& path) {
    const QString normalized = platform::normalizeLocalPath(path.trimmed());
    if (normalized.isEmpty()) return;
    for (const auto& existing : values_.customWorldPaths) {
        if (platform::normalizeLocalPath(existing) == normalized) return;
    }
    values_.customWorldPaths.append(normalized);
    emit settingsChanged();
}

void SettingsViewModel::removeCustomWorldPath(const QString& path) {
    const QString normalized = platform::normalizeLocalPath(path.trimmed());
    if (normalized.isEmpty()) return;
    for (int i = 0; i < values_.customWorldPaths.size(); ++i) {
        if (platform::normalizeLocalPath(values_.customWorldPaths.at(i)) == normalized) {
            values_.customWorldPaths.removeAt(i);
            emit settingsChanged();
            return;
        }
    }
}

#undef BM_SET_STRING
#undef BM_SET_INT
#undef BM_SET_BOOL
#undef BM_SET_DOUBLE
