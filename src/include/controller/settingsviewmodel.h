#ifndef BEDROCKMAP_SETTINGVIEWMODEL_H
#define BEDROCKMAP_SETTINGVIEWMODEL_H

#include <QObject>
#include <QString>
#include <QVariantList>

#include "base/config.h"

/// Editable settings draft for the Qt Quick application. Persisted values only
/// become active after the application restarts.
class SettingsViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString colorTheme READ colorTheme WRITE setColorTheme NOTIFY colorThemeChanged)
    Q_PROPERTY(bool collapseSidebarOnStartup READ collapseSidebarOnStartup WRITE setCollapseSidebarOnStartup NOTIFY settingsChanged)
    Q_PROPERTY(bool showMapDebugButton READ showMapDebugButton WRITE setShowMapDebugButton NOTIFY settingsChanged)
    Q_PROPERTY(bool showMapStatusBar READ showMapStatusBar WRITE setShowMapStatusBar NOTIFY settingsChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString renderBackend READ renderBackend WRITE setRenderBackend NOTIFY settingsChanged)
    Q_PROPERTY(int threadCount READ threadCount WRITE setThreadCount NOTIFY settingsChanged)
    Q_PROPERTY(bool preloadAllChunkCoords READ preloadAllChunkCoords WRITE setPreloadAllChunkCoords NOTIFY settingsChanged)
    Q_PROPERTY(bool chunkOverviewEnabled READ chunkOverviewEnabled WRITE setChunkOverviewEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int leveldbXorMode READ leveldbXorMode WRITE setLeveldbXorMode NOTIFY settingsChanged)
    Q_PROPERTY(QString leveldbXorKey READ leveldbXorKey WRITE setLeveldbXorKey NOTIFY settingsChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    /// Point size the platform falls back to while fontSize leaves the size open
    /// (-1); the UI shows the resulting effective size instead of the sentinel.
    Q_PROPERTY(int defaultFontSize READ defaultFontSize CONSTANT)
    /// Accent override for the whole UI; an empty string keeps the theme default.
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY accentColorChanged)
    Q_PROPERTY(bool gpuOrthographicView READ gpuOrthographicView WRITE setGpuOrthographicView NOTIFY settingsChanged)
    Q_PROPERTY(double gpuShadowStrength READ gpuShadowStrength WRITE setGpuShadowStrength NOTIFY settingsChanged)
    Q_PROPERTY(double gpuAoStrength READ gpuAoStrength WRITE setGpuAoStrength NOTIFY settingsChanged)
    Q_PROPERTY(int gpuAoSteps READ gpuAoSteps WRITE setGpuAoSteps NOTIFY settingsChanged)
    Q_PROPERTY(double gpuBevelStrength READ gpuBevelStrength WRITE setGpuBevelStrength NOTIFY settingsChanged)
    Q_PROPERTY(double gpuBevelWidth READ gpuBevelWidth WRITE setGpuBevelWidth NOTIFY settingsChanged)
    Q_PROPERTY(double gpuSaturation READ gpuSaturation WRITE setGpuSaturation NOTIFY settingsChanged)
    Q_PROPERTY(double gpuBrightness READ gpuBrightness WRITE setGpuBrightness NOTIFY settingsChanged)
    Q_PROPERTY(double gpuBiomeBlendBlocks READ gpuBiomeBlendBlocks WRITE setGpuBiomeBlendBlocks NOTIFY settingsChanged)
    Q_PROPERTY(int gpuShadowStep READ gpuShadowStep WRITE setGpuShadowStep NOTIFY settingsChanged)
    Q_PROPERTY(int gpuTileSnapshotCacheSizeMB READ gpuTileSnapshotCacheSizeMB WRITE setGpuTileSnapshotCacheSizeMB NOTIFY settingsChanged)
    Q_PROPERTY(bool gpuHighDetailMap READ gpuHighDetailMap WRITE setGpuHighDetailMap NOTIFY settingsChanged)
    Q_PROPERTY(int cpuShadowLevel READ cpuShadowLevel WRITE setCpuShadowLevel NOTIFY settingsChanged)
    Q_PROPERTY(int minimumScaleLevel READ minimumScaleLevel WRITE setMinimumScaleLevel NOTIFY settingsChanged)
    Q_PROPERTY(int maximumScaleLevel READ maximumScaleLevel WRITE setMaximumScaleLevel NOTIFY settingsChanged)
    Q_PROPERTY(double panInertia READ panInertia WRITE setPanInertia NOTIFY settingsChanged)
    Q_PROPERTY(QString gridLineColor READ gridLineColor WRITE setGridLineColor NOTIFY settingsChanged)
    Q_PROPERTY(QString mapSelectionColor READ mapSelectionColor WRITE setMapSelectionColor NOTIFY settingsChanged)
    Q_PROPERTY(QString voidMapColor READ voidMapColor WRITE setVoidMapColor NOTIFY settingsChanged)
    Q_PROPERTY(QString tileColorScheme READ tileColorScheme WRITE setTileColorScheme NOTIFY settingsChanged)
    Q_PROPERTY(int actorRenderStyle READ actorRenderStyle WRITE setActorRenderStyle NOTIFY settingsChanged)
    Q_PROPERTY(int actorBorderWidth READ actorBorderWidth WRITE setActorBorderWidth NOTIFY settingsChanged)
    Q_PROPERTY(QString actorBorderColor READ actorBorderColor WRITE setActorBorderColor NOTIFY settingsChanged)
    Q_PROPERTY(QString chunkEditorHighlightColor READ chunkEditorHighlightColor WRITE setChunkEditorHighlightColor NOTIFY settingsChanged)
    Q_PROPERTY(int chunkEditorHighlightWidth READ chunkEditorHighlightWidth WRITE setChunkEditorHighlightWidth NOTIFY settingsChanged)
    Q_PROPERTY(QString voxelSelectionColor READ voxelSelectionColor WRITE setVoxelSelectionColor NOTIFY settingsChanged)
    Q_PROPERTY(int regionCacheSize READ regionCacheSize WRITE setRegionCacheSize NOTIFY settingsChanged)
    Q_PROPERTY(int emptyRegionCacheSize READ emptyRegionCacheSize WRITE setEmptyRegionCacheSize NOTIFY settingsChanged)
    Q_PROPERTY(bool loadGlobalData READ loadGlobalData WRITE setLoadGlobalData NOTIFY settingsChanged)
    Q_PROPERTY(int maxGlobalDataLoadCount READ maxGlobalDataLoadCount WRITE setMaxGlobalDataLoadCount NOTIFY settingsChanged)
    Q_PROPERTY(bool transparentWater READ transparentWater WRITE setTransparentWater NOTIFY settingsChanged)
    Q_PROPERTY(bool gpuGrassHeightEnabled READ gpuGrassHeightEnabled WRITE setGpuGrassHeightEnabled NOTIFY settingsChanged)
    Q_PROPERTY(double gpuGrassHeightBase READ gpuGrassHeightBase WRITE setGpuGrassHeightBase NOTIFY settingsChanged)
    Q_PROPERTY(double gpuGrassHeightRange READ gpuGrassHeightRange WRITE setGpuGrassHeightRange NOTIFY settingsChanged)
    Q_PROPERTY(QString gpuGrassHeightColor READ gpuGrassHeightColor WRITE setGpuGrassHeightColor NOTIFY settingsChanged)
    Q_PROPERTY(bool checkUpdate READ checkUpdate WRITE setCheckUpdate NOTIFY settingsChanged)
    Q_PROPERTY(bool scanLeviPath READ scanLeviPath WRITE setScanLeviPath NOTIFY settingsChanged)
    Q_PROPERTY(QStringList customWorldPaths READ customWorldPaths NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList worldSources READ worldSources NOTIFY settingsChanged)

   public:
    explicit SettingsViewModel(QObject* parent);
    QString colorTheme() const { return values_.colorTheme; }
    bool collapseSidebarOnStartup() const { return values_.collapseSidebarOnStartup; }
    bool showMapDebugButton() const { return values_.showMapDebugButton; }
    bool showMapStatusBar() const { return values_.showMapStatusBar; }
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

    Q_INVOKABLE bool save();
    /// Persists only the custom world folder list, leaving unrelated pending
    /// settings untouched.
    Q_INVOKABLE bool saveWorldSources();
    Q_INVOKABLE void reset();
    void setColorTheme(const QString& value);
    void setCollapseSidebarOnStartup(bool value);
    void setShowMapDebugButton(bool value);
    void setShowMapStatusBar(bool value);
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
    void languageChanged();
    void fontSizeChanged();
    void accentColorChanged();
    void settingsChanged();
    void settingsSaved();
    void errorChanged(const QString& message);

   private:
    void emitAllChanged();
    setting::Settings values_;
};

#endif
