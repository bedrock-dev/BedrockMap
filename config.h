#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>

#include "bedrock_key.h"
#include <QImage>

#include <QColor>

typedef bl::chunk_pos region_pos;

struct cfg {

    static constexpr uint8_t RW = 4u;  //(1<<w)
    static constexpr int BG_GRAY = 20;

    static_assert((RW & (RW - 1)) == 0, "Invalid Render region width");

    const static std::string SOFTWARE_NAME;

    const static std::string SOFTWARE_VERSION;

    //下面是可配置的
    static int REGION_CACHE_SIZE;
    static int EMPTY_REGION_CACHE_SIZE;
    static int THREAD_NUM;
    static float ZOOM_SPEED;
    static std::string COLOR_THEME;

    static region_pos c2r(const bl::chunk_pos &ch);

    static void initColorTable();

    static void initConfig();

    static QImage *BACKGROUND_IMAGE();

    static QImage *EMPTY_IMAGE();

    static QImage *BACKGROUND_IMAGE_COPY();

    static QImage *BG();
};

#endif  // CONFIG_H
