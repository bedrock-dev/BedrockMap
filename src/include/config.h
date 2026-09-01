#ifndef CONFIG_H
#define CONFIG_H

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

    // 45°-only sun direction for basic shadow (renderStyle1)
    enum class SunDir { NW = 0, NE = 1, SW = 2, SE = 3 };
    constexpr SunDir SUN_DIRECTION = SunDir::NW;

    extern QString MCBE_LEVEL_PATH;

    region_pos c2r(const bl::chunk_pos &ch);
    void initColorTable();
    QString VERSION_STRING();
}  // namespace constant

// Runtime settings (read from / written to config.ini)
namespace setting {
    void init();
    void load();
    void save();

    constexpr double MINIMUM_ZOOM_SCALE = 4.0 / constant::COORDS_REGION_SIZE;

    // Gui
    extern QString COLOR_THEME;
    extern QString FONT_FAMILY;
    extern int FONT_SIZE;

    // Map
    extern int MAP_RENDER_STYLE;
    extern int TILE_RENDER_SCALE;
    extern int SHADOW_PCF_RADIUS;
    extern int SHADOW_MAP_SCALE;
    extern int SHADOW_LEVEL;
    extern int MINIMUM_SCALE_LEVEL;
    extern int MAXIMUM_SCALE_LEVEL;
    extern float ZOOM_SPEED;
    extern QString GRID_LINE_COLOR;
    extern int ACTOR_RENDER_STYLE;
    extern int ACTOR_BORDER_WIDTH;
    extern QString ACTOR_BORDER_COLOR;
    extern QString CHUNK_EDITOR_HIGHLIGHT_COLOR;
    extern int CHUNK_EDITOR_HIGHLIGHT_WIDTH;
    extern QString VOID_MAP_COLOR;
    extern bool TRANSPARENT_WATER;
    extern bool ENABLE_THUMBNAIL_MODE;

    // Cache
    extern int THREAD_NUM;
    extern int REGION_CACHE_SIZE;
    extern int EMPTY_REGION_CACHE_SIZE;
    extern int THUMBNAIL_REION_CACHE_SIZE;
    extern int HEIGHT_MAP_CACHE_SIZE;

    // Misc
    extern bool LOAD_GLOBAL_DATA;
    extern bool PRELOAD_ALL_CHUNK_COORDS;
    extern int MAX_GLOBAL_DATA_LOAD_COUNT;
    extern QString ICON_THEME;
    extern bool CHECK_UPDATE;

    // LeviLauncher
    extern bool SCAN_LEVI_PATH;

    // Debug
    extern bool LOG_OUT_MISSING_TEXTURE;

    // Lang
    extern QString LANGUAGE;
}  // namespace setting

#endif  // CONFIG_H
