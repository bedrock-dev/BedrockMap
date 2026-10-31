#include "render/gpurenderoptions.h"

#include <QColor>
#include <algorithm>

#include "base/config.h"
#include "color.h"

GpuRenderOptions gpuRenderOptionsFromSettings() {
    const setting::Settings& s = setting::current();
    GpuRenderOptions options;
    options.orthographic_view = s.gpuOrthographicView;
    options.ao_strength = std::clamp(s.gpuAoStrength, 0.0f, 1.0f);
    options.ao_steps = setting::normalizeAoSteps(s.gpuAoSteps);
    options.bevel_strength = std::clamp(s.gpuBevelStrength, 0.0f, 1.0f);
    options.bevel_width = std::clamp(s.gpuBevelWidth, 0.25f, 2.0f);
    options.saturation = std::clamp(s.gpuSaturation, 0.0f, 2.0f);
    options.brightness = std::clamp(s.gpuBrightness, 0.0f, 2.0f);
    options.shadow_strength = std::clamp(s.gpuShadowStrength, 0.0f, 1.0f);
    options.shadow_step = 2.0f / static_cast<float>(std::clamp(s.gpuShadowStep, 4, 32));
    options.biome_blend_blocks = std::clamp(s.gpuBiomeBlendBlocks, 0.0f, 16.0f);

    const auto water_base = bl::get_block_color("minecraft:water");
    options.water_base_color = {water_base.r / 255.0f, water_base.g / 255.0f, water_base.b / 255.0f};
    const QColor configured_grass_color(s.gpuGrassHeightColor);
    const QColor grass_color = configured_grass_color.isValid() ? configured_grass_color : QColor(Qt::red);
    options.grass_height_color = {grass_color.redF(), grass_color.greenF(), grass_color.blueF()};
    options.grass_height_enabled = s.gpuGrassHeightEnabled;
    options.grass_height_base = s.gpuGrassHeightBase;
    options.grass_height_range = std::max(1.0f, s.gpuGrassHeightRange);
    return options;
}

void gpuSunStep(int& stepX, int& stepZ) {
    // Matches the configured sun direction used by the terrain shader.
    switch (constant::SUN_DIRECTION) {
        case constant::SunDir::NW:
            stepX = -1;
            stepZ = -1;
            break;
        case constant::SunDir::NE:
            stepX = 1;
            stepZ = -1;
            break;
        case constant::SunDir::SW:
            stepX = -1;
            stepZ = 1;
            break;
        case constant::SunDir::SE:
            stepX = 1;
            stepZ = 1;
            break;
    }
}
