#version 440
layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2DArray uColor;
layout(binding = 2) uniform sampler2DArray uHeight;
layout(binding = 3) uniform sampler2DArray uMaterial;
layout(binding = 4) uniform sampler2DArray uPageTable;
layout(binding = 5) uniform sampler2D uOverview;
layout(binding = 6) uniform sampler2D uBiomePalette;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 uView;
    vec4 uParams; // tile world size, page-table side, desired LOD index, blocks per texel
    vec4 uShade;  // sun step xz, shadow reach, shadow step
    vec4 uShadow; // darkness, strength, bevel width, biome blend
    vec4 uAo;     // strength, directions, steps, radius
    vec4 uTint;   // saturation, brightness, bevel strength, overview-only
    vec4 uWater;  // water base color
    vec4 uGrass;  // base, range, enabled, biome blend
    vec4 uGrassColor;
    vec4 uOverviewRect;
    vec4 uChecker;
    vec4 uCheckerAlternate;
    vec4 uVoidOptions; // transparent void
};

uint regionHash(ivec2 region) {
    uint h = uint(region.x & 65535) * 40503u;
    h ^= uint(region.y & 65535) * 60307u;
    h ^= h >> 8u;
    h &= 0x3FFFu;
    return h == 0u ? 1u : h;
}

vec3 emptyColor(vec2 world) {
    int checker = (int(floor(world.x / 64.0)) ^ int(floor(world.y / 64.0))) & 1;
    return checker != 0 ? uCheckerAlternate.rgb : uChecker.rgb;
}

vec4 overviewOrEmpty(vec2 world) {
    if (uTint.w < 0.5 || any(lessThanEqual(uOverviewRect.zw, vec2(0.0)))) return vec4(emptyColor(world), 1.0);
    vec2 uv = (world - uOverviewRect.xy) / uOverviewRect.zw;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) return vec4(emptyColor(world), 1.0);
    vec4 overview = texture(uOverview, uv);
    if (overview.a < 0.5) return vec4(emptyColor(world), 1.0);
    return vec4(overview.rgb, 1.0);
}

float decodeHeight(vec2 lowHigh) {
    float code = floor(lowHigh.x * 255.0 + 0.5) + floor(lowHigh.y * 255.0 + 0.5) * 256.0;
    return code < 0.5 ? -1000.0 : code - 512.0;
}

vec2 heightAt(vec3 uvLayer) {
    vec4 texel = texture(uHeight, uvLayer);
    return vec2(decodeHeight(texel.rg), decodeHeight(texel.ba));
}

const float VOID_HEIGHT = -999.0;
const float EDGE_BRIGHT = 1.18;
const float EDGE_DARK = 0.86;
const float BIOME_TINT_BRIGHTNESS = 1.30;
const int WATER_TINT = 1;
const int GRASS_TINT = 2;
const int LEAVES_TINT = 4;
const int WATER_OVERLAY = 8;
const int TERRAIN_SAMPLE = 16;
const int MISSING_CHUNK = 32;

vec3 paletteTint(float biomeId, int row) {
    float id = clamp(floor(biomeId * 255.0 + 0.5), 0.0, 255.0);
    vec3 tint = texture(uBiomePalette, vec2((id + 0.5) / 256.0, (float(row) + 0.5) / 3.0)).rgb;
    return clamp(tint * BIOME_TINT_BRIGHTNESS, 0.0, 1.0);
}

bool residentLayer(vec2 block, int lod, out int layer, out vec2 uv) {
    float tileWorld = max(uParams.x, 1.0) * exp2(float(lod));
    vec2 tileCoord = floor(block / tileWorld);
    float side = max(uParams.y, 1.0);
    ivec2 vc = ivec2(mod(mod(tileCoord, side) + side, side));
    vec4 entry = texelFetch(uPageTable, ivec3(vc, lod), 0);
    int flags = int(floor(entry.b * 255.0 + 0.5));
    if ((flags & 0x80) == 0 || (flags & 0x40) != 0) return false;
    uint storedHash = (uint(flags & 0x3F) << 8) | uint(floor(entry.a * 255.0 + 0.5));
    if (storedHash != regionHash(ivec2(tileCoord))) return false;
    layer = int(floor(entry.r * 255.0 + 0.5)) + int(floor(entry.g * 255.0 + 0.5)) * 256;
    uv = fract(block / tileWorld);
    return true;
}

bool targetSample(vec2 block, out vec3 color, out vec2 heights, out vec2 material, out vec2 uv) {
    int desiredLod = max(0, int(floor(uParams.z + 0.5)));
    int layer = 0;
    if (!residentLayer(block, desiredLod, layer, uv)) return false;
    vec3 uvLayer = vec3(uv, float(layer));
    vec4 texel = texture(uColor, uvLayer);
    if (texel.a < 0.5) return false;
    color = texel.rgb;
    heights = heightAt(uvLayer);
    material = texture(uMaterial, uvLayer).rg;
    return true;
}

bool fallbackColor(vec2 block, int lod, out vec3 color) {
    int layer = 0;
    vec2 uv;
    if (!residentLayer(block, lod, layer, uv)) return false;
    vec4 texel = texture(uColor, vec3(uv, float(layer)));
    if (texel.a < 0.5) return false;
    int flags = int(floor(texture(uMaterial, vec3(uv, float(layer))).g * 255.0 + 0.5));
    bool voidSample = heightAt(vec3(uv, float(layer))).g <= VOID_HEIGHT || (flags & TERRAIN_SAMPLE) == 0;
    color = (flags & MISSING_CHUNK) != 0 || (uVoidOptions.x > 0.5 && voidSample) ? emptyColor(block) : texel.rgb;
    return true;
}

float biomeAt(vec2 block, float fallbackBiome) {
    vec3 c;
    vec2 heights;
    vec2 material;
    vec2 uv;
    if (!targetSample(block, c, heights, material, uv)) return fallbackBiome;
    int flags = int(floor(material.g * 255.0 + 0.5));
    return (flags & TERRAIN_SAMPLE) != 0 ? material.r : fallbackBiome;
}

vec3 biomeTint(vec2 world, int row, float fallbackBiome) {
    float span = max(uShadow.w, max(uParams.w, 1.0));
    vec2 cell = floor((world + span * 0.5) / span) * span - span * 0.5;
    vec2 local = clamp((world - cell) / span, 0.0, 1.0);
    float b00 = biomeAt(cell, fallbackBiome);
    float b10 = biomeAt(cell + vec2(span, 0.0), fallbackBiome);
    float b01 = biomeAt(cell + vec2(0.0, span), fallbackBiome);
    float b11 = biomeAt(cell + vec2(span), fallbackBiome);
    vec3 north = mix(paletteTint(b00, row), paletteTint(b10, row), local.x);
    vec3 south = mix(paletteTint(b01, row), paletteTint(b11, row), local.x);
    return mix(north, south, local.y);
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

float ambientOcclusion(vec2 world, float solid) {
    if (uAo.x <= 0.0 || uParams.z >= 1.0) return 1.0;
    int directions = clamp(int(uAo.y), 1, 8);
    int steps = clamp(int(uAo.z), 1, 8);
    float sum = 0.0;
    for (int i = 0; i < 8; ++i) {
        if (i >= directions) break;
        float angle = (float(i) + 0.5) * 6.28318530718 / float(directions);
        vec2 dir = vec2(cos(angle), sin(angle));
        float distance = max(1.0, uAo.w / float(steps));
        float steepest = 0.0;
        for (int k = 0; k < 8; ++k) {
            if (k >= steps) break;
            vec3 c;
            vec2 h;
            vec2 m;
            vec2 uv;
            if (targetSample(floor(world + dir * distance), c, h, m, uv))
                steepest = max(steepest, (h.r - solid) / max(distance, 1.0));
            distance += max(1.0, uAo.w / float(steps));
        }
        sum += steepest / sqrt(1.0 + steepest * steepest);
    }
    return 1.0 - uAo.x * clamp(sum / float(directions), 0.0, 1.0);
}

float shadowOcclusion(vec2 world, float top) {
    if (uShade.z <= 0.0 || uParams.z >= 1.0) return 0.0;
    float stepBlocks = max(uShade.w, 1.0);
    for (int i = 1; i <= 64; ++i) {
        float travelled = float(i) * stepBlocks;
        if (travelled > uShade.z) break;
        vec3 c;
        vec2 h;
        vec2 m;
        vec2 uv;
        if (targetSample(floor(world + uShade.xy * travelled), c, h, m, uv) && h.g > top + travelled) return 1.0;
    }
    return 0.0;
}

void main() {
    vec2 world = uView.xy + v_texcoord * uView.zw;
    if (uTint.w > 0.5) {
        fragColor = overviewOrEmpty(world);
        return;
    }

    vec3 color;
    vec2 heights;
    vec2 material;
    vec2 uv;
    if (!targetSample(world, color, heights, material, uv)) {
        // Keep already-resident neighboring levels visible while the requested
        // level is being filled. Prefer a coarser level at equal distance so a
        // zoom-out does not sharpen stale fine samples before the coarse tile
        // arrives.
        const int desiredLod = max(0, int(floor(uParams.z + 0.5)));
        bool fallbackFound = false;
        for (int distance = 1; distance <= 7 && !fallbackFound; ++distance) {
            const int coarser = desiredLod + distance;
            if (coarser <= 7) fallbackFound = fallbackColor(world, coarser, color);
            if (!fallbackFound) {
                const int finer = desiredLod - distance;
                if (finer >= 0) fallbackFound = fallbackColor(world, finer, color);
            }
        }
        if (fallbackFound) {
            fragColor = vec4(color, 1.0);
            return;
        }
        fragColor = overviewOrEmpty(world);
        return;
    }

    int flags = int(floor(material.g * 255.0 + 0.5));
    if ((flags & MISSING_CHUNK) != 0) {
        fragColor = vec4(emptyColor(world), 1.0);
        return;
    }
    if (heights.g <= VOID_HEIGHT || (flags & TERRAIN_SAMPLE) == 0) {
        fragColor = vec4(uVoidOptions.x > 0.5 ? emptyColor(world) : color, 1.0);
        return;
    }
    vec3 surface = color;
    if ((flags & WATER_TINT) != 0) surface *= biomeTint(world, 0, material.r);
    if ((flags & LEAVES_TINT) != 0) surface *= biomeTint(world, 1, material.r);
    if ((flags & GRASS_TINT) != 0) {
        surface *= biomeTint(world, 2, material.r);
        if (uGrass.z > 0.5) {
            float heightMix = clamp((heights.g - uGrass.x) / max(uGrass.y, 1.0), 0.0, 1.0);
            surface = mix(surface, uGrassColor.rgb, heightMix);
        }
    }
    if ((flags & WATER_OVERLAY) != 0) {
        vec3 water = uWater.rgb * biomeTint(world, 0, material.r);
        float opacity = clamp(0.30 + 0.15 * max(heights.g - heights.r, 0.0), 0.0, 0.92);
        surface = mix(surface, water, opacity);
    }

    float factor = 1.0;
    float bevelFade = bevelLodFade() * uTint.z;
    if (bevelFade > 0.0) {
        vec2 cell = floor(world / max(uParams.w, 1.0)) * max(uParams.w, 1.0);
        vec2 local = (world - cell) / max(uParams.w, 1.0);
        float edgeWidth = max(uShadow.z, 0.0625);
        float tLeft = smoothstep(0.0, 1.0, clamp(1.0 - local.x / edgeWidth, 0.0, 1.0));
        float tTop = smoothstep(0.0, 1.0, clamp(1.0 - local.y / edgeWidth, 0.0, 1.0));

        vec3 neighbourColor;
        vec2 leftHeights;
        vec2 upHeights;
        vec2 diagonalHeights;
        vec2 neighbourMaterial;
        vec2 neighbourUv;
        bool hasLeft = targetSample(cell + vec2(-max(uParams.w, 1.0), 0.0), neighbourColor, leftHeights, neighbourMaterial, neighbourUv);
        bool hasUp = targetSample(cell + vec2(0.0, -max(uParams.w, 1.0)), neighbourColor, upHeights, neighbourMaterial, neighbourUv);
        bool hasDiagonal = targetSample(cell + vec2(-max(uParams.w, 1.0), -max(uParams.w, 1.0)), neighbourColor, diagonalHeights,
                                         neighbourMaterial, neighbourUv);
        if (hasLeft && hasUp && hasDiagonal) {
            float bevel = dominantBevel(bevelFactor(tLeft, heights.r, leftHeights.r), bevelFactor(tTop, heights.r, upHeights.r));
            bevel = clamp(bevel, 0.55, 1.58) *
                    lightCornerContinuity(tLeft, tTop, heights.r, leftHeights.r, upHeights.r, diagonalHeights.r);
            factor = 1.0 + (bevel - 1.0) * bevelFade;
        }
    }

    factor *= ambientOcclusion(world, heights.r);
    if ((flags & WATER_OVERLAY) != 0) {
        float waterFade = clamp(1.0 - (heights.g - heights.r) / 5.0, 0.0, 1.0);
        factor = 1.0 + (factor - 1.0) * waterFade;
    }
    if ((flags & WATER_OVERLAY) == 0)
        factor *= mix(1.0, uShadow.x, shadowOcclusion(world, heights.g) * uShadow.y);

    color = clamp(surface * uTint.y, 0.0, 1.0);
    color = clamp(color * factor, 0.0, 1.0);
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = clamp(mix(vec3(luma), color, uTint.x), 0.0, 1.0);
    fragColor = vec4(color, 1.0);
}
