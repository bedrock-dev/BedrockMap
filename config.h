#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>

#include "bedrock_key.h"
#include <QColor>

typedef bl::chunk_pos region_pos;

namespace cfg {

    constexpr uint8_t RW = 4u;  //(1<<w)
    static_assert((RW & (RW - 1)) == 0, "Invalid Render region width");

    constexpr int REGION_CACHE_SIZE = 1024 / RW;
    constexpr int EMPTY_REGION_CACHE_SIZE = 16384 / RW;
    constexpr int LAYER_IMAGE_CACHE_SIZE = 16384 / RW;

    constexpr int BG_GRAY = 20;

    const char SOFTWARE_NAME[] = "Bedrock Map";

    const char SOFTWARE_VERSION[] = "v0.1";

    inline region_pos c2r(const bl::chunk_pos &ch) {
        auto cx = ch.x < 0 ? ch.x - cfg::RW + 1 : ch.x;
        auto cz = ch.z < 0 ? ch.z - cfg::RW + 1 : ch.z;
        return region_pos{cx / cfg::RW * cfg::RW, cz / cfg::RW * cfg::RW, ch.dim};
    }

}  // namespace cfg

#endif  // CONFIG_H
