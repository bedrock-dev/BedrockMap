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
    [[nodiscard]] static std::optional<AppVersion> parse(const QString &text);
    [[nodiscard]] QString toString() const;
    [[nodiscard]] int compare(const AppVersion &other) const;
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

    region_pos c2r(const bl::chunk_pos &ch);
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

        // Map
        int MAP_RENDER_STYLE{1};
        int TILE_RENDER_SCALE{4};
        int SHADOW_PCF_RADIUS{0};
        int SHADOW_MAP_SCALE{2};
        int SHADOW_LEVEL{128};
        int MINIMUM_SCALE_LEVEL{4};
        int MAXIMUM_SCALE_LEVEL{1024};
        int COORDS_MINIMAP_WIDTH{100};
        int COORDS_MINIMAP_HEIGHT{100};
        float ZOOM_SPEED{1.2f};
        QString GRID_LINE_COLOR{"#bbbbbb"};
        int ACTOR_RENDER_STYLE{0};
        int ACTOR_BORDER_WIDTH{2};
        QString ACTOR_BORDER_COLOR{"#ff000000"};
        QString CHUNK_EDITOR_HIGHLIGHT_COLOR{"#ffaa00"};
        int CHUNK_EDITOR_HIGHLIGHT_WIDTH{8};
        QString VOID_MAP_COLOR{"#dddddd"};
        bool TRANSPARENT_WATER{true};

        // Cache
        int THREAD_NUM{8};
        int REGION_CACHE_SIZE{1024};
        int EMPTY_REGION_CACHE_SIZE{8192};
        int HEIGHT_MAP_CACHE_SIZE{500000};

        // Misc
        bool LOAD_GLOBAL_DATA{true};
        bool PRELOAD_ALL_CHUNK_COORDS{false};
        int MAX_GLOBAL_DATA_LOAD_COUNT{4096};
        QString ICON_THEME{"new"};
        bool CHECK_UPDATE{true};

        // LeviLauncher
        bool SCAN_LEVI_PATH{true};

        // Lang (empty = auto-detect from the system locale)
        QString LANGUAGE;
    };

    void init();
    void load();
    void save();
    void save(const Settings &settings);

    [[nodiscard]] const Settings &current();
    void apply(const Settings &settings);
}  // namespace setting

#endif  // BEDROCKMAP_CONFIG_H
