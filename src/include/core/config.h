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

// Runtime settings (read from / written to config.ini). The active values live
// in one immutable snapshot; readers go through setting::current() so render
// workers always observe a single consistent configuration. The snapshot is
// only replaced during startup (load/apply), never mutated field-by-field.
namespace setting {
    struct Settings {
        // Gui
        QString COLOR_THEME{"system"};
        QString FONT_FAMILY;
        int FONT_SIZE{-1};

        // Rendering
        int MAP_RENDER_STYLE{1};
        // Draw the map with the GPU renderer instead of the CPU one. Only one of
        // the two renderers is active, and they are equivalent in behaviour.
        bool GPU_RENDER_ENABLED{false};
        // Strength of the GPU renderer's ambient occlusion in concave corners.
        // Its only consumer is the shader, which is why it is a GPU setting
        // rather than a map one: the CPU styles bake AO into the tiles instead.
        float GPU_AO_STRENGTH{0.22f};
        // Depth of the GPU renderer's bevel: 0 removes the bevel entirely, 1 is the
        // full bevel and the default. Shader-only, like the ambient occlusion.
        float GPU_BEVEL_STRENGTH{1.0f};
        // Width multiplier for the GPU renderer's bevel. 1 preserves the automatic
        // screen-space width; values below/above it make the bevel narrower/wider.
        float GPU_BEVEL_WIDTH{1.0f};
        // Saturation of the GPU renderer's output: 0 is greyscale, 1 leaves the
        // stored colours alone, above 1 boosts them.
        float GPU_SATURATION{1.0f};
        // Brightness multiplier for the GPU renderer: 1 leaves the shaded image
        // unchanged, 0 is black, and values above 1 lift it.
        float GPU_BRIGHTNESS{1.0f};
        // Blend grass/foliage biome colours towards a configured warning colour
        // as the surface rises above the configured height baseline.
        bool GPU_GRASS_HEIGHT_ENABLED{false};
        float GPU_GRASS_HEIGHT_BASE{80.0f};
        QString GPU_GRASS_HEIGHT_COLOR{"#ff2222"};
        float GPU_GRASS_HEIGHT_RANGE{180.0f};
        // How much of the GPU renderer's terrain shadow is applied. 0 removes it and
        // 1 is the full shadow. It scales the shadow the ray march produces, so it
        // composes with SHADOW_LEVEL rather than replacing it.
        float GPU_SHADOW_STRENGTH{1.0f};
        int TILE_RENDER_SCALE{4};
        int SHADOW_PCF_RADIUS{0};
        int SHADOW_MAP_SCALE{2};
        int SHADOW_LEVEL{128};

        // Map
        // The coarsest scale terrain is still drawn at, in pixels per chunk: below
        // it the view switches to the chunk coordinate overview (and, when the
        // coordinate index is off, it is simply the zoom floor).
        //
        // What bounds it is the region cache (see below), not memory in general: an
        // 8x8-chunk region only costs its ~710 KiB when it holds chunks, so the entry
        // count has to cover the *populated* regions in view. The other cost scales
        // with the raw visible count, because collectVisibleRegions() asks the loader
        // about every visible region each frame: about 8k regions at scale 2 on a
        // 1920-wide pane (~1.6 ms/frame) and 33k at scale 1 (~6.5 ms, four times that
        // on a 2560-wide pane).
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
        // Color of the 3D preview's selection box outline, as #AARRGGBB (or #RRGGBB for an
        // opaque color). The fill is the same color darkened and keeps a fraction of the alpha.
        // Named for the voxel widget so it is not mistaken for the 2D map's chunk selection.
        // The drag handles are not affected: they follow their axis color instead.
        QString VOXEL_SELECTION_COLOR{"#e641b9dc"};

        // Cache
        int THREAD_NUM{8};
        // Entries, not bytes: one entry per *baked* 8x8-chunk region, each about
        // 710 KiB (256 KiB tips_info_ + 3 x 64 KiB images + 256 KiB LOD pyramid).
        // A region that holds no chunks never gets a ChunkRegion at all - it takes a
        // single byte over in the empty cache - so this only has to cover the world's
        // populated regions that are on screen at once. Populated to the brim that is
        // 16k x 710 KiB = ~11 GiB, so raise it to fit the worlds you open rather than
        // to the maximum.
        int REGION_CACHE_SIZE{16384};
        // Empty-region markers, one byte each plus the cache's own node. A wide view
        // spans tens of thousands of regions and most of them are empty, so if this
        // is below that count the markers are evicted and re-derived from the chunk
        // index every frame.
        int EMPTY_REGION_CACHE_SIZE{65536};
        int HEIGHT_MAP_CACHE_SIZE{500000};

        // Misc
        bool LOAD_GLOBAL_DATA{true};
        bool PRELOAD_ALL_CHUNK_COORDS{false};
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
