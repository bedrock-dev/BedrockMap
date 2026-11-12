#version 440
#extension GL_GOOGLE_include_directive : enable
layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

#include "terrain_common.glsl"
#include "terrain_material.glsl"
#include "terrain_lighting.glsl"
#include "terrain_outline.glsl"

void main() {
    vec2 world = uView.xy + v_texcoord * uView.zw;
    if (uTint.w > 0.5) {
        fragColor = overviewOrEmpty(world);
        return;
    }

    vec3 color;
    vec2 heights;
    float biome;
    int flags;
    if (!targetSample(world, color, heights, biome, flags)) {
        // Keep already-resident levels visible while the requested level is
        // being filled. A fast zoom can skip several levels, so search outward
        // and prefer a coarser level at equal distance so a zoom-out does not
        // sharpen stale fine samples before the coarse tile arrives.
        const int desiredLod = max(0, int(floor(uParams.z + 0.5)));
        for (int distance = 1; distance <= MAX_LOD_INDEX; ++distance) {
            const int coarser = desiredLod + distance;
            const int finer = desiredLod - distance;
            if (coarser > MAX_LOD_INDEX && finer < 0) break;
            bool empty = false;
            if ((coarser <= MAX_LOD_INDEX && fallbackColor(world, coarser, color, empty)) ||
                (finer >= 0 && fallbackColor(world, finer, color, empty))) {
                fragColor = vec4(empty ? withTerrainOutline(color, world) : color, 1.0);
                return;
            }
        }
        vec4 background = overviewOrEmpty(world);
        int layer = 0;
        vec2 uv;
        if (tileState(world, desiredLod, layer, uv) == TILE_EMPTY)
            background.rgb = withTerrainOutline(background.rgb, world);
        fragColor = background;
        return;
    }

    if ((flags & MISSING_CHUNK) != 0) {
        fragColor = vec4(withTerrainOutline(emptyColor(world), world), 1.0);
        return;
    }
    if (heights.g <= VOID_HEIGHT) {
        vec3 background = uVoidOptions.x > 0.5 ? emptyColor(world) : voidColor();
        fragColor = vec4(withTerrainOutline(background, world), 1.0);
        return;
    }
    vec3 surface = surfaceMaterial(world, color, heights, biome, flags);

    bool waterOverlay = (flags & WATER_OVERLAY) != 0;
    float waterFade = waterOverlay ? clamp(1.0 - (heights.g - heights.r) / 5.0, 0.0, 1.0) : 1.0;
    if (waterFade <= 0.0) {
        // Deep transparent water discards all terrain lighting, including AO.
        fragColor = vec4(gradeColor(surface, 1.0), 1.0);
        return;
    }
    float surfaceHeight = lightingHeight(heights, flags);
    float factor = 1.0;
    float bevelFade = bevelLodFade() * uTint.z;
    if (bevelFade > 0.0) {
        vec2 cell = floor(world / max(uParams.w, 1.0)) * max(uParams.w, 1.0);
        vec2 local = (world - cell) / max(uParams.w, 1.0);
        float edgeWidth = max(uShadow.z, 0.0625);
        float tLeft = smoothstep(0.0, 1.0, clamp(1.0 - local.x / edgeWidth, 0.0, 1.0));
        float tTop = smoothstep(0.0, 1.0, clamp(1.0 - local.y / edgeWidth, 0.0, 1.0));

        float texel = max(uParams.w, 1.0);
        vec2 leftHeights;
        vec2 upHeights;
        vec2 diagonalHeights;
        float neighbourBiome;
        int leftFlags;
        int upFlags;
        int diagonalFlags;
        bool hasLeft = neighbourSample(cell + vec2(-texel, 0.0), leftHeights, neighbourBiome, leftFlags);
        bool hasUp = neighbourSample(cell + vec2(0.0, -texel), upHeights, neighbourBiome, upFlags);
        bool hasDiagonal = neighbourSample(cell - vec2(texel), diagonalHeights, neighbourBiome, diagonalFlags);
        if (hasLeft && hasUp && hasDiagonal) {
            float leftHeight = lightingHeight(leftHeights, leftFlags);
            float upHeight = lightingHeight(upHeights, upFlags);
            float diagonalHeight = lightingHeight(diagonalHeights, diagonalFlags);
            float bevel = dominantBevel(bevelFactor(tLeft, surfaceHeight, leftHeight), bevelFactor(tTop, surfaceHeight, upHeight));
            bevel = clamp(bevel, 0.55, 1.58) *
                    lightCornerContinuity(tLeft, tTop, surfaceHeight, leftHeight, upHeight, diagonalHeight);
            factor = 1.0 + (bevel - 1.0) * bevelFade;
        }
    }

    factor *= ambientOcclusion(world, surfaceHeight);
    factor = 1.0 + (factor - 1.0) * waterFade;
    if (!waterOverlay)
        factor *= mix(1.0, uShadow.x, shadowOcclusion(world, heights.g) * uShadow.y);

    fragColor = vec4(gradeColor(surface, factor), 1.0);
}
