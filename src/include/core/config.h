#ifndef BEDROCKMAP_CONFIG_H
#define BEDROCKMAP_CONFIG_H

#include <qchar.h>
#include <qcontainerfwd.h>
#include <qimage.h>
#include <sys/stat.h>

#include <QColor>
#include <QImage>
#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "bedrock_key.h"

typedef bl::chunk_pos region_pos;

struct AppVersion {
    std::array<int, 3> core{};  // a.b.c
    int beta = -1;              // -1 = no beta suffix
    [[nodiscard]] static std::optional<AppVersion> parse(const QString& text);
    [[nodiscard]] QString toString() const;
    [[nodiscard]] int compare(const AppVersion& other) const;
};

// Compile-time constants (unchanging)
namespace constant {
    extern const std::string SOFTWARE_NAME;
    extern const AppVersion SOFTWARE_VERSION;

    extern const std::string CONFIG_FILE_PATH;
    extern const std::string BLOCK_FILE_PATH;
    extern const std::string BIOME_FILE_PATH;
    extern const QString SHADER_FILE_PATH;
    extern const QString TRANSLATION_FILES_PATH;

    constexpr uint8_t RW = 8u;
    constexpr int COORDS_REGION_SIZE = 128;
    static_assert(COORDS_REGION_SIZE % RW == 0, "COORDS_REGION_SIZE must be a multiple of RW");
    extern const int GRID_WIDTH;

    // Floor zoom for the whole-world overview mode, derived from region size.
    constexpr double MINIMUM_ZOOM_SCALE = 4.0 / COORDS_REGION_SIZE;

    // 45°-only sun direction for basic shadow (renderStyle1)
    enum class SunDir { NW = 0, NE = 1, SW = 2, SE = 3 };
    constexpr SunDir SUN_DIRECTION = SunDir::NW;

    extern const QString MCBE_LEVEL_PATH;

    region_pos c2r(const bl::chunk_pos& ch);
    void initColorTable();
    QString VERSION_STRING();
}  // namespace constant

// Runtime settings loaded from and saved to config.ini.
namespace setting {
    struct Settings {
        // Gui
        QString COLOR_THEME{"system"};
        QString FONT_FAMILY;
        int FONT_SIZE{-1};

        // Rendering
        int MAP_RENDER_STYLE{1};
        // Select the GPU map renderer.
        bool GPU_RENDER_ENABLED{true};
        // Use the GPU renderer's orthographic heightfield camera.
        bool GPU_ORTHOGRAPHIC_VIEW{false};
        // GPU ambient occlusion strength.
        float GPU_AO_STRENGTH{0.15f};
        // GPU bevel depth.
        float GPU_BEVEL_STRENGTH{1.0f};
        // GPU bevel width multiplier.
        float GPU_BEVEL_WIDTH{1.0f};
        // GPU output saturation.
        float GPU_SATURATION{1.05f};
        // GPU output brightness.
        float GPU_BRIGHTNESS{1.1f};
        // Blend grass/foliage colours above a height threshold.
        bool GPU_GRASS_HEIGHT_ENABLED{true};
        float GPU_GRASS_HEIGHT_BASE{80.0f};
        QString GPU_GRASS_HEIGHT_COLOR{"#dd2222"};
        float GPU_GRASS_HEIGHT_RANGE{320.0f};
        // GPU terrain shadow strength; composes with SHADOW_LEVEL.
        float GPU_SHADOW_STRENGTH{0.7f};
        // GPU shadow quality; the shader step is 2.0 / this value.
        int GPU_SHADOW_STEP{8};
        // Width of the GPU biome tint interpolation area in blocks.
        float GPU_BIOME_BLEND_BLOCKS{8.0f};
        // for cpu
        int TILE_RENDER_SCALE{4};
        int SHADOW_PCF_RADIUS{0};
        int SHADOW_MAP_SCALE{2};
        int SHADOW_LEVEL{128};

        // Map
        // Coarsest terrain zoom before the coordinate overview.
        int MINIMUM_SCALE_LEVEL{2};
        int MAXIMUM_SCALE_LEVEL{1024};
        int COORDS_MINIMAP_WIDTH{100};
        int COORDS_MINIMAP_HEIGHT{100};
        QString GRID_LINE_COLOR{"#bbbbbb"};
        int ACTOR_RENDER_STYLE{0};
        int ACTOR_BORDER_WIDTH{2};
        QString ACTOR_BORDER_COLOR{"#ff000000"};
        QString CHUNK_EDITOR_HIGHLIGHT_COLOR{"#ffaa00"};
        int CHUNK_EDITOR_HIGHLIGHT_WIDTH{8};
        QString VOID_MAP_COLOR{"#dddddd"};
        bool TRANSPARENT_WATER{true};
        // 3D preview selection outline, stored as #AARRGGBB or #RRGGBB.
        QString VOXEL_SELECTION_COLOR{"#e641b9dc"};

        // Cache
        int THREAD_NUM{8};
        // Number of populated 8x8-chunk region entries to cache.
        int REGION_CACHE_SIZE{16384};
        // Number of empty-region markers to cache.
        int EMPTY_REGION_CACHE_SIZE{65536};
        int HEIGHT_MAP_CACHE_SIZE{500000};

        // Misc
        bool LOAD_GLOBAL_DATA{true};
        bool PRELOAD_ALL_CHUNK_COORDS{true};
        int MAX_GLOBAL_DATA_LOAD_COUNT{4096};
        QString ICON_THEME{"default"};
        bool CHECK_UPDATE{true};

        // LeviLauncher
        bool SCAN_LEVI_PATH{true};

        // Lang (empty = auto-detect from the system locale)
        QString LANGUAGE;
    };

    void init();
    void load();
    void save();
    void save(const Settings& settings);

    [[nodiscard]] const Settings& current();
    void apply(const Settings& settings);
}  // namespace setting

#endif  // BEDROCKMAP_CONFIG_H
