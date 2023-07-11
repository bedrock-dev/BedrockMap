#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>

#include "bedrock_key.h"
#include <QImage>

#include <QColor>

typedef bl::chunk_pos region_pos;

namespace cfg {

    constexpr uint8_t RW = 4u;  //(1<<w)
    static_assert((RW & (RW - 1)) == 0, "Invalid Render region width");

    constexpr int REGION_CACHE_SIZE = 4096;
    constexpr int EMPTY_REGION_CACHE_SIZE = 16384;
    constexpr int THREAD_NUM = 4;

    constexpr int BG_GRAY = 20;

    constexpr auto ZOOM_SPEED = 1.15;

    const char SOFTWARE_NAME[] = "Bedrock Map";

    const char SOFTWARE_VERSION[] = "v0.1";


    region_pos c2r(const bl::chunk_pos &ch);

    void initColorTable();

    QImage *BACKGROUND_IMAGE();

    QImage *EMPTY_IMAGE();

}  // namespace cfg

#endif  // CONFIG_H
