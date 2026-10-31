#ifndef BEDROCKMAP_SETTINGVIEWMODEL_H
#define BEDROCKMAP_SETTINGVIEWMODEL_H

#include <QObject>
#include <QString>
#include <QVariantList>

#include "base/config.h"

/// Settings facade for the Qt Quick application.
class SettingsViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString colorTheme READ colorTheme WRITE setColorTheme NOTIFY colorThemeChanged)
    Q_PROPERTY(bool collapseSidebarOnStartup READ collapseSidebarOnStartup WRITE setCollapseSidebarOnStartup NOTIFY
                   collapseSidebarOnStartupChanged)
    Q_PROPERTY(bool showMapDebugButton READ showMapDebugButton WRITE setShowMapDebugButton NOTIFY showMapDebugButtonChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString renderBackend READ renderBackend WRITE setRenderBackend NOTIFY renderBackendChanged)
    Q_PROPERTY(int threadCount READ threadCount WRITE setThreadCount NOTIFY threadCountChanged)
    Q_PROPERTY(bool preloadAllChunkCoords READ preloadAllChunkCoords WRITE setPreloadAllChunkCoords NOTIFY preloadAllChunkCoordsChanged)
    Q_PROPERTY(bool chunkOverviewEnabled READ chunkOverviewEnabled WRITE setChunkOverviewEnabled NOTIFY chunkOverviewEnabledChanged)
    Q_PROPERTY(int leveldbXorMode READ leveldbXorMode WRITE setLeveldbXorMode NOTIFY leveldbXorModeChanged)
    Q_PROPERTY(QString leveldbXorKey READ leveldbXorKey WRITE setLeveldbXorKey NOTIFY leveldbXorKeyChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    /// Point size the platform falls back to while fontSize leaves the size open
    /// (-1); the UI shows the resulting effective size instead of the sentinel.
    Q_PROPERTY(int defaultFontSize READ defaultFontSize CONSTANT)
    /// Accent override for the whole UI; an empty string keeps the theme default.
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY accentColorChanged)
    Q_PROPERTY(bool gpuOrthographicView READ gpuOrthographicView WRITE setGpuOrthographicView NOTIFY gpuOrthographicViewChanged)
    Q_PROPERTY(double gpuShadowStrength READ gpuShadowStrength WRITE setGpuShadowStrength NOTIFY gpuShadowStrengthChanged)
    Q_PROPERTY(double gpuAoStrength READ gpuAoStrength WRITE setGpuAoStrength NOTIFY gpuAoStrengthChanged)
    Q_PROPERTY(int gpuAoSteps READ gpuAoSteps WRITE setGpuAoSteps NOTIFY gpuAoStepsChanged)
    Q_PROPERTY(double gpuBevelStrength READ gpuBevelStrength WRITE setGpuBevelStrength NOTIFY gpuBevelStrengthChanged)
    Q_PROPERTY(double gpuBevelWidth READ gpuBevelWidth WRITE setGpuBevelWidth NOTIFY gpuBevelWidthChanged)
    Q_PROPERTY(double gpuSaturation READ gpuSaturation WRITE setGpuSaturation NOTIFY gpuSaturationChanged)
    Q_PROPERTY(double gpuBrightness READ gpuBrightness WRITE setGpuBrightness NOTIFY gpuBrightnessChanged)
    Q_PROPERTY(double gpuBiomeBlendBlocks READ gpuBiomeBlendBlocks WRITE setGpuBiomeBlendBlocks NOTIFY gpuBiomeBlendBlocksChanged)
    Q_PROPERTY(int gpuShadowStep READ gpuShadowStep WRITE setGpuShadowStep NOTIFY gpuShadowStepChanged)
    Q_PROPERTY(int gpuTileSnapshotCacheSizeMB READ gpuTileSnapshotCacheSizeMB WRITE setGpuTileSnapshotCacheSizeMB NOTIFY
                   gpuTileSnapshotCacheSizeMBChanged)
    Q_PROPERTY(bool gpuHighDetailMap READ gpuHighDetailMap WRITE setGpuHighDetailMap NOTIFY gpuHighDetailMapChanged)
    Q_PROPERTY(int cpuShadowLevel READ cpuShadowLevel WRITE setCpuShadowLevel NOTIFY cpuShadowLevelChanged)
    Q_PROPERTY(int minimumScaleLevel READ minimumScaleLevel WRITE setMinimumScaleLevel NOTIFY minimumScaleLevelChanged)
    Q_PROPERTY(int maximumScaleLevel READ maximumScaleLevel WRITE setMaximumScaleLevel NOTIFY maximumScaleLevelChanged)
    Q_PROPERTY(double panInertia READ panInertia WRITE setPanInertia NOTIFY panInertiaChanged)
    Q_PROPERTY(QString gridLineColor READ gridLineColor WRITE setGridLineColor NOTIFY gridLineColorChanged)
    Q_PROPERTY(QString mapSelectionColor READ mapSelectionColor WRITE setMapSelectionColor NOTIFY mapSelectionColorChanged)
    Q_PROPERTY(QString voidMapColor READ voidMapColor WRITE setVoidMapColor NOTIFY voidMapColorChanged)
    Q_PROPERTY(QString tileColorScheme READ tileColorScheme WRITE setTileColorScheme NOTIFY tileColorSchemeChanged)
    Q_PROPERTY(int actorRenderStyle READ actorRenderStyle WRITE setActorRenderStyle NOTIFY actorRenderStyleChanged)
    Q_PROPERTY(int actorBorderWidth READ actorBorderWidth WRITE setActorBorderWidth NOTIFY actorBorderWidthChanged)
    Q_PROPERTY(QString actorBorderColor READ actorBorderColor WRITE setActorBorderColor NOTIFY actorBorderColorChanged)
    Q_PROPERTY(QString chunkEditorHighlightColor READ chunkEditorHighlightColor WRITE setChunkEditorHighlightColor NOTIFY
                   chunkEditorHighlightColorChanged)
    Q_PROPERTY(int chunkEditorHighlightWidth READ chunkEditorHighlightWidth WRITE setChunkEditorHighlightWidth NOTIFY
                   chunkEditorHighlightWidthChanged)
    Q_PROPERTY(QString voxelSelectionColor READ voxelSelectionColor WRITE setVoxelSelectionColor NOTIFY voxelSelectionColorChanged)
    Q_PROPERTY(int regionCacheSize READ regionCacheSize WRITE setRegionCacheSize NOTIFY regionCacheSizeChanged)
    Q_PROPERTY(int emptyRegionCacheSize READ emptyRegionCacheSize WRITE setEmptyRegionCacheSize NOTIFY emptyRegionCacheSizeChanged)
    Q_PROPERTY(bool loadGlobalData READ loadGlobalData WRITE setLoadGlobalData NOTIFY loadGlobalDataChanged)
    Q_PROPERTY(int maxGlobalDataLoadCount READ maxGlobalDataLoadCount WRITE setMaxGlobalDataLoadCount NOTIFY maxGlobalDataLoadCountChanged)
    Q_PROPERTY(bool transparentWater READ transparentWater WRITE setTransparentWater NOTIFY transparentWaterChanged)
    Q_PROPERTY(bool gpuGrassHeightEnabled READ gpuGrassHeightEnabled WRITE setGpuGrassHeightEnabled NOTIFY gpuGrassHeightEnabledChanged)
    Q_PROPERTY(double gpuGrassHeightBase READ gpuGrassHeightBase WRITE setGpuGrassHeightBase NOTIFY gpuGrassHeightBaseChanged)
    Q_PROPERTY(double gpuGrassHeightRange READ gpuGrassHeightRange WRITE setGpuGrassHeightRange NOTIFY gpuGrassHeightRangeChanged)
    Q_PROPERTY(QString gpuGrassHeightColor READ gpuGrassHeightColor WRITE setGpuGrassHeightColor NOTIFY gpuGrassHeightColorChanged)
    Q_PROPERTY(bool checkUpdate READ checkUpdate WRITE setCheckUpdate NOTIFY checkUpdateChanged)
    Q_PROPERTY(bool scanLeviPath READ scanLeviPath WRITE setScanLeviPath NOTIFY scanLeviPathChanged)
    Q_PROPERTY(QStringList customWorldPaths READ customWorldPaths NOTIFY customWorldPathsChanged)
    Q_PROPERTY(QVariantList worldSources READ worldSources NOTIFY worldSourcesChanged)

   public:
    explicit SettingsViewModel(QObject* parent);
    QString colorTheme() const { return values_.colorTheme; }
    bool collapseSidebarOnStartup() const { return values_.collapseSidebarOnStartup; }
    bool showMapDebugButton() const { return values_.showMapDebugButton; }
    QString language() const { return values_.language; }
    QString renderBackend() const { return values_.renderBackend; }
    int threadCount() const { return values_.threadCount; }
    bool preloadAllChunkCoords() const { return values_.preloadAllChunkCoords; }
    bool chunkOverviewEnabled() const { return values_.chunkOverviewEnabled; }
    int leveldbXorMode() const { return values_.leveldbXorMode; }
    QString leveldbXorKey() const { return values_.leveldbXorKey; }
    /// Effective point size, i.e. what the settings page shows and edits.
    int fontSize() const { return values_.fontSize > 0 ? values_.fontSize : defaultFontSize(); }
    int defaultFontSize() const;
    QString accentColor() const { return values_.accentColor; }
    bool gpuOrthographicView() const { return values_.gpuOrthographicView; }
    double gpuShadowStrength() const { return values_.gpuShadowStrength; }
    double gpuAoStrength() const { return values_.gpuAoStrength; }
    int gpuAoSteps() const { return values_.gpuAoSteps; }
    double gpuBevelStrength() const { return values_.gpuBevelStrength; }
    double gpuBevelWidth() const { return values_.gpuBevelWidth; }
    double gpuSaturation() const { return values_.gpuSaturation; }
    double gpuBrightness() const { return values_.gpuBrightness; }
    double gpuBiomeBlendBlocks() const { return values_.gpuBiomeBlendBlocks; }
    int gpuShadowStep() const { return values_.gpuShadowStep; }
    int gpuTileSnapshotCacheSizeMB() const { return values_.gpuTileSnapshotCacheSizeMB; }
    bool gpuHighDetailMap() const { return values_.gpuHighDetailMap; }
    int cpuShadowLevel() const { return values_.cpuShadowLevel; }
    int minimumScaleLevel() const { return values_.minimumScaleLevel; }
    int maximumScaleLevel() const { return values_.maximumScaleLevel; }
    double panInertia() const { return values_.panInertia; }
    QString gridLineColor() const { return values_.gridLineColor; }
    QString mapSelectionColor() const { return values_.mapSelectionColor; }
    QString voidMapColor() const { return values_.voidMapColor; }
    QString tileColorScheme() const { return values_.tileColorScheme; }
    int actorRenderStyle() const { return values_.actorRenderStyle; }
    int actorBorderWidth() const { return values_.actorBorderWidth; }
    QString actorBorderColor() const { return values_.actorBorderColor; }
    QString chunkEditorHighlightColor() const { return values_.chunkEditorHighlightColor; }
    int chunkEditorHighlightWidth() const { return values_.chunkEditorHighlightWidth; }
    QString voxelSelectionColor() const { return values_.voxelSelectionColor; }
    int regionCacheSize() const { return values_.regionCacheSize; }
    int emptyRegionCacheSize() const { return values_.emptyRegionCacheSize; }
    bool loadGlobalData() const { return values_.loadGlobalData; }
    int maxGlobalDataLoadCount() const { return values_.maxGlobalDataLoadCount; }
    bool transparentWater() const { return values_.transparentWater; }
    bool gpuGrassHeightEnabled() const { return values_.gpuGrassHeightEnabled; }
    double gpuGrassHeightBase() const { return values_.gpuGrassHeightBase; }
    double gpuGrassHeightRange() const { return values_.gpuGrassHeightRange; }
    QString gpuGrassHeightColor() const { return values_.gpuGrassHeightColor; }
    bool checkUpdate() const { return values_.checkUpdate; }
    bool scanLeviPath() const { return values_.scanLeviPath; }
    QStringList customWorldPaths() const { return values_.customWorldPaths; }
    QVariantList worldSources() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE bool save();
    /// Persists only the custom world folder list, leaving unrelated pending
    /// settings untouched.
    Q_INVOKABLE bool saveWorldSources();
    /// Refreshes the platform-dependent scan source list.
    Q_INVOKABLE void refreshWorldSources();
    Q_INVOKABLE void reset();
    /// Push the configured UI font onto the application, so a QML frontend
    /// picks it up without restarting.
    Q_INVOKABLE void applyFont();
    [[nodiscard]] const setting::Settings& settings() const { return values_; }
    void setSettings(const setting::Settings& values);
    void setColorTheme(const QString& value);
    void setCollapseSidebarOnStartup(bool value);
    void setShowMapDebugButton(bool value);
    void setLanguage(const QString& value);
    void setRenderBackend(const QString& value);
    void setThreadCount(int value);
    void setPreloadAllChunkCoords(bool value);
    void setChunkOverviewEnabled(bool value);
    void setLeveldbXorMode(int value);
    void setLeveldbXorKey(const QString& value);
    void setFontSize(int value);
    void setAccentColor(const QString& value);
    void setGpuOrthographicView(bool value);
    void setGpuShadowStrength(double value);
    void setGpuAoStrength(double value);
    void setGpuAoSteps(int value);
    void setGpuBevelStrength(double value);
    void setGpuBevelWidth(double value);
    void setGpuSaturation(double value);
    void setGpuBrightness(double value);
    void setGpuBiomeBlendBlocks(double value);
    void setGpuShadowStep(int value);
    void setGpuTileSnapshotCacheSizeMB(int value);
    void setGpuHighDetailMap(bool value);
    void setCpuShadowLevel(int value);
    void setMinimumScaleLevel(int value);
    void setMaximumScaleLevel(int value);
    void setPanInertia(double value);
    void setGridLineColor(const QString& value);
    void setMapSelectionColor(const QString& value);
    void setVoidMapColor(const QString& value);
    void setTileColorScheme(const QString& value);
    void setActorRenderStyle(int value);
    void setActorBorderWidth(int value);
    void setActorBorderColor(const QString& value);
    void setChunkEditorHighlightColor(const QString& value);
    void setChunkEditorHighlightWidth(int value);
    void setVoxelSelectionColor(const QString& value);
    void setRegionCacheSize(int value);
    void setEmptyRegionCacheSize(int value);
    void setLoadGlobalData(bool value);
    void setMaxGlobalDataLoadCount(int value);
    void setTransparentWater(bool value);
    void setGpuGrassHeightEnabled(bool value);
    void setGpuGrassHeightBase(double value);
    void setGpuGrassHeightRange(double value);
    void setGpuGrassHeightColor(const QString& value);
    void setHeightMapCacheSize(int value);
    void setCheckUpdate(bool value);
    void setScanLeviPath(bool value);
    Q_INVOKABLE void addCustomWorldPath(const QString& path);
    Q_INVOKABLE void removeCustomWorldPath(const QString& path);

   signals:
    void colorThemeChanged();
    void collapseSidebarOnStartupChanged();
    void showMapDebugButtonChanged();
    void languageChanged();
    void renderBackendChanged();
    void threadCountChanged();
    void preloadAllChunkCoordsChanged();
    void chunkOverviewEnabledChanged();
    void leveldbXorModeChanged();
    void leveldbXorKeyChanged();
    void fontSizeChanged();
    void accentColorChanged();
    void gpuOrthographicViewChanged();
    void gpuShadowStrengthChanged();
    void gpuAoStrengthChanged();
    void gpuAoStepsChanged();
    void gpuBevelStrengthChanged();
    void gpuBevelWidthChanged();
    void gpuSaturationChanged();
    void gpuBrightnessChanged();
    void gpuBiomeBlendBlocksChanged();
    void gpuShadowStepChanged();
    void gpuTileSnapshotCacheSizeMBChanged();
    void gpuHighDetailMapChanged();
    void cpuShadowLevelChanged();
    void minimumScaleLevelChanged();
    void maximumScaleLevelChanged();
    void panInertiaChanged();
    void gridLineColorChanged();
    void mapSelectionColorChanged();
    void voidMapColorChanged();
    void tileColorSchemeChanged();
    void actorRenderStyleChanged();
    void actorBorderWidthChanged();
    void actorBorderColorChanged();
    void chunkEditorHighlightColorChanged();
    void chunkEditorHighlightWidthChanged();
    void voxelSelectionColorChanged();
    void regionCacheSizeChanged();
    void emptyRegionCacheSizeChanged();
    void loadGlobalDataChanged();
    void maxGlobalDataLoadCountChanged();
    void transparentWaterChanged();
    void gpuGrassHeightEnabledChanged();
    void gpuGrassHeightBaseChanged();
    void gpuGrassHeightRangeChanged();
    void gpuGrassHeightColorChanged();
    void checkUpdateChanged();
    void scanLeviPathChanged();
    void customWorldPathsChanged();
    void worldSourcesChanged();
    void saved();
    void errorChanged(const QString& message);

   private:
    void emitAllChanged();
    setting::Settings values_;
};

#endif
