#ifndef BEDROCKMAP_CONFIG_H
#define BEDROCKMAP_CONFIG_H

#include <qchar.h>
#include <qcontainerfwd.h>
#include <qimage.h>
#include <sys/stat.h>

#include <QColor>
#include <QImage>
#include <QStringList>
#include <array>
#include <cstdint>
#include <optional>

#include "geometry.h"

using region_pos = bl::chunk_pos;

struct AppVersion {
    std::array<int, 3> core{};  // a.b.c
    int beta = -1;              // -1 = no beta suffix
    [[nodiscard]] static std::optional<AppVersion> parse(const QString& text);
    [[nodiscard]] QString toString() const;
    [[nodiscard]] int compare(const AppVersion& other) const;
};

// Compile-time constants (unchanging)
namespace constant {
    inline constexpr const char* SOFTWARE_NAME = "BedrockMap";

    const AppVersion& softwareVersion();

    inline constexpr uint8_t RW = 8u;
    inline constexpr int COORDS_REGION_SIZE = 128;
    static_assert(COORDS_REGION_SIZE % RW == 0, "COORDS_REGION_SIZE must be a multiple of RW");
    inline constexpr int GRID_WIDTH = 32;
    inline constexpr int DEFAULT_FONT_POINT_SIZE = 14;
    inline constexpr const char* ANDROID_UI_FONT_REGULAR_RESOURCE =
        ":/bedrockmap/fonts/MiSans-Regular.ttf";
    inline constexpr const char* MONOSPACE_FONT_RESOURCE = ":/bedrockmap/fonts/JetBrainsMono-Regular.ttf";

    // Floor zoom for the whole-world overview mode, derived from region size.
    inline constexpr double MINIMUM_ZOOM_SCALE = 4.0 / COORDS_REGION_SIZE;
    inline constexpr int MAX_TERRAIN_BLOCKS_PER_TEXEL = 32;
    inline constexpr double MINIMUM_TERRAIN_SCALE = 16.0 / MAX_TERRAIN_BLOCKS_PER_TEXEL;

    // 45-degree sun direction for terrain shading.
    enum class SunDir { NW = 0, NE = 1, SW = 2, SE = 3 };
    inline constexpr SunDir SUN_DIRECTION = SunDir::NW;

    region_pos c2r(const bl::chunk_pos& ch);
    void initColorTable();
    QString VERSION_STRING();
}  // namespace constant

// Runtime settings loaded from and saved to config.ini.
namespace setting {
    struct Settings {
        // GUI
        QString colorTheme{"system"};
        bool collapseSidebarOnStartup{false};  // Start the QML sidebar collapsed when enabled.
        int fontSize{-1};
        QString accentColor;  // Empty keeps the built-in theme accent.
        QString language;     // Empty auto-detects from the system locale.

        // Rendering
        // Qt Quick RHI backend: automatic, opengl, vulkan or d3d11.
        QString renderBackend{"automatic"};
        bool gpuOrthographicView{false};  // Orthographic heightfield camera.
#ifdef Q_OS_ANDROID
        float gpuAoStrength{0.0f};  // Mobile defaults to AO off.
#else
        float gpuAoStrength{0.15f};  // Ambient occlusion strength.
#endif
        int gpuAoSteps{8};  // Samples per direction; 2, 4 or 8.
        float gpuBevelStrength{1.0f};      // Bevel depth.
        float gpuBevelWidth{1.0f};         // Bevel width multiplier.
        float gpuSaturation{1.05f};        // Output saturation.
        float gpuBrightness{1.1f};         // Output brightness.
        bool gpuGrassHeightEnabled{true};  // Blend grass/foliage above a height threshold.
        float gpuGrassHeightBase{80.0f};
        QString gpuGrassHeightColor{"#dd2222"};
        float gpuGrassHeightRange{320.0f};
#ifdef Q_OS_ANDROID
        float gpuShadowStrength{0.0f};  // Mobile defaults to shadows off.
#else
        float gpuShadowStrength{0.7f};  // Composes with cpuShadowLevel.
#endif
        int gpuShadowStep{8};             // Shader step is 2.0 / this value.
        float gpuBiomeBlendBlocks{8.0f};  // Biome tint interpolation width in blocks.
        int gpuTileSnapshotCacheSizeMB{32};
        /// Keep full-resolution map tiles further out when zooming, at the cost
        /// of more GPU memory and tile building. See TerrainQuickItem::blocksPerTexelFor().
        bool gpuHighDetailMap{false};

        // Terrain shading
        int cpuShadowLevel{128};

        // Map
#ifndef NDEBUG
        bool showMapDebugButton{true};
#else
        bool showMapDebugButton{false};
#endif
        int minimumScaleLevel{2};  // Coarsest terrain zoom before the coordinate overview.
        int maximumScaleLevel{1024};
        float panInertia{0.0f};
        QString gridLineColor{"#bbbbbb"};
        QString mapSelectionColor{"#50dafffb"};  // Map selection RGBA, stored as #AARRGGBB; outline alpha is always 255.
        int actorRenderStyle{0};
        int actorBorderWidth{2};
        QString actorBorderColor{"#ff000000"};
        QString chunkEditorHighlightColor{"#ffaa00"};
        int chunkEditorHighlightWidth{8};
        QString voidMapColor{"#dddddd"};
        QString tileColorScheme{"dark"};  // Checkerboard palette, independent of the UI theme.
        bool transparentWater{true};
        QString voxelSelectionColor{"#e641b9dc"};  // 3D preview outline, #AARRGGBB or #RRGGBB.

        // Cache
        int threadCount{8};
        int regionCacheSize{16384};       // Populated 8x8-chunk region entries.
        int emptyRegionCacheSize{65536};  // Empty-region markers.

        // Misc
        bool loadGlobalData{true};
        bool preloadAllChunkCoords{true};
        bool chunkOverviewEnabled{true};    // Requires coordinate preload; sampled when opening a world.
        int leveldbXorMode{2};              // 0 = disabled, 1 = enabled, 2 = auto-detect.
        QString leveldbXorKey{"88329851"};  // Repeating-key XOR used by NetEase LevelDB files.
        int maxGlobalDataLoadCount{4096};
        bool checkUpdate{true};

        // LeviLauncher
        bool scanLeviPath{true};

        // Additional user-selected world directories (desktop today, Android
        // once the all-files source is enabled).
        QStringList customWorldPaths;
    };

    void init();
    /// Replace the active process snapshot from disk. Normally called once.
    void load();
    /// Read settings from disk without changing the active process snapshot.
    [[nodiscard]] Settings read();
    /// Persist settings without changing the active process snapshot.
    [[nodiscard]] QString save();
    [[nodiscard]] QString save(const Settings& settings);

    /// Round down to a supported power of two, clamped to [2, 4096].
    constexpr int normalizeZoomScale(int value) {
        int scale = 2;
        while (scale < 4096 && scale <= value / 2) scale *= 2;
        return scale;
    }

    constexpr int normalizeAoSteps(int value) {
        return value <= 2 ? 2 : value <= 4 ? 4 : 8;
    }

    /// Process-lifetime settings snapshot used by runtime consumers.
    [[nodiscard]] const Settings& current();
    void setResolvedLanguage(const QString& language);
}  // namespace setting

#endif  // BEDROCKMAP_CONFIG_H
