// Bevel, ambient occlusion and shadow terms. Requires terrain_common.glsl.

const float EDGE_BRIGHT = 1.18;
const float EDGE_DARK = 0.86;

float lightingHeight(vec2 heights, int flags) {
    // Only transparent water exposes the bed; opaque water is lit at its surface.
    return (flags & WATER_OVERLAY) != 0 ? heights.r : heights.g;
}

float bevelFactor(float t, float cur, float lightSide) {
    if (t <= 0.0) return 1.0;
    if (cur > lightSide) return 1.0 + (EDGE_BRIGHT - 1.0) * t;
    if (cur < lightSide) return 1.0 + (EDGE_DARK - 1.0) * t;
    return 1.0;
}

float dominantBevel(float a, float b) {
    return abs(a - 1.0) >= abs(b - 1.0) ? a : b;
}

float lightCornerContinuity(float tx, float ty, float cur, float left, float up, float diagonal) {
    if (tx <= 0.0 || ty <= 0.0) return 1.0;
    if (left != cur || up != cur) return 1.0;
    float t = tx * ty;
    if (diagonal < cur) return 1.0 + (EDGE_BRIGHT - 1.0) * t;
    if (diagonal > cur) return 1.0 + (EDGE_DARK - 1.0) * t;
    return 1.0;
}

float bevelLodFade() {
    float bp = max(uParams.w, 1.0);
    if (bp > 8.0) return 0.0;
    if (bp > 4.0) return 0.1;
    if (bp > 1.0) return 0.6;
    return 1.0;
}

const vec2 AO_DIRECTIONS[8] = vec2[8](
    vec2(1.0, 0.0), vec2(0.70710678, 0.70710678),
    vec2(0.0, 1.0), vec2(-0.70710678, 0.70710678),
    vec2(-1.0, 0.0), vec2(-0.70710678, -0.70710678),
    vec2(0.0, -1.0), vec2(0.70710678, -0.70710678)
);

float ambientOcclusion(vec2 world, float surfaceHeight) {
    float strength = uAo.x * uAo.y;
    if (strength <= 0.0 || uParams.z >= 1.0) return 1.0;
    float radius = max(uAo.z, 1.0);
    int steps = clamp(int(uAo.w), 2, 8);
    // Preserve adjacent blockers and spend fewer samples on distant terrain.
    float distances[8];
    float stepFraction = 1.0 / float(steps - 1);
    for (int k = 0; k < 8; ++k) {
        if (k >= steps) break;
        float t = float(k) * stepFraction;
        distances[k] = 1.0 + (radius - 1.0) * t * t;
    }
    float sum = 0.0;
    for (int i = 0; i < 8; ++i) {
        float steepest = 0.0;
        for (int k = 0; k < 8; ++k) {
            if (k >= steps) break;
            vec2 h;
            float biome;
            int flags;
            vec2 cell = floor(world + AO_DIRECTIONS[i] * distances[k]);
            if (neighbourSample(cell, h, biome, flags) && (flags & MISSING_CHUNK) == 0 && h.g > VOID_HEIGHT) {
                // Measure to the blocker's nearest edge, not the nominal sample
                // distance, so occlusion varies inside a block.
                float edgeDistance = max(length(world - clamp(world, cell, cell + 1.0)), 0.25);
                steepest = max(steepest, (lightingHeight(h, flags) - surfaceHeight) / edgeDistance);
            }
        }
        sum += steepest * inversesqrt(1.0 + steepest * steepest);
    }
    return 1.0 - strength * clamp(sum * 0.125, 0.0, 1.0);
}

float shadowOcclusion(vec2 world, float top) {
    if (uShade.z <= 0.0 || uParams.z >= 1.0) return 0.0;
    // Sub-block steps are required: with whole-block steps every pixel of a
    // block samples the same columns, so shadow edges snap to block borders.
    float stepBlocks = max(uShade.w, 0.03125);
    for (int i = 1; i <= 1024; ++i) {
        float travelled = float(i) * stepBlocks;
        if (travelled > uShade.z) break;
        vec2 h;
        float biome;
        if (neighbourSample(floor(world + uShade.xy * travelled), h, biome) && h.g > top + travelled) return 1.0;
    }
    return 0.0;
}
