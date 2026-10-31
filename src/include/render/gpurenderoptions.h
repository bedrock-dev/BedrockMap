#ifndef BEDROCKMAP_GPURENDEROPTIONS_H
#define BEDROCKMAP_GPURENDEROPTIONS_H

#include <array>

/// Shader tunables for TerrainQuickItem, kept independent of Qt Quick types.
struct GpuRenderOptions {
    std::array<float, 3> water_base_color{0.64f, 0.64f, 0.64f};
    std::array<float, 3> grass_height_color{1.0f, 0.0f, 0.0f};
    bool grass_height_enabled{false};
    float grass_height_base{64.0f};
    float grass_height_range{128.0f};

    bool shadow_enabled{true};
    int shadow_steps{48};
    // World-space distance between samples of the GPU shadow ray.
    float shadow_step{0.0625f};
    float shadow_strength{1.0f};

    float ao_strength{0.10f};
    int ao_steps{8};
    float ao_radius{16.0f};

    float bevel_strength{1.0f};
    float bevel_width{1.0f};
    float saturation{1.0f};
    float brightness{1.0f};
    float biome_blend_blocks{8.0f};
    bool flat_shading{false};
    bool orthographic_view{false};
};

/// Shader tunables exactly as configured in config.ini, so every GPU renderer
/// starts from the same numbers.
[[nodiscard]] GpuRenderOptions gpuRenderOptionsFromSettings();

/// One step of the shadow ray towards the sun, matching
/// constant::SUN_DIRECTION. `stepX`/`stepZ` are world block offsets.
void gpuSunStep(int& stepX, int& stepZ);

#endif  // BEDROCKMAP_GPURENDEROPTIONS_H
