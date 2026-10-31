#version 440
layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
layout(binding = 1) uniform sampler2DArray uColor;
layout(binding = 2) uniform sampler2DArray uSurface;
layout(binding = 3) uniform sampler2DArray uPageTable;
layout(binding = 4) uniform sampler2D uOverview;
layout(binding = 5) uniform sampler2D uBiomePalette;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 uView;
    vec4 uParams; // tile world size, page-table side, desired LOD index, blocks per texel
    vec4 uShade;  // sun step xz, shadow reach, shadow step
    vec4 uShadow; // darkness, strength, bevel width, biome blend
    vec4 uAo;     // strength, zoom fade, radius, steps
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
    vec4 overview = textureLod(uOverview, uv, 0.0);
    if (overview.a < 0.5) return vec4(emptyColor(world), 1.0);
    return vec4(overview.rgb, 1.0);
}

uint channelByte(float value) {
    return uint(floor(value * 255.0 + 0.5));
}

uint surfaceCode(vec3 uvLayer) {
    vec4 texel = textureLod(uSurface, uvLayer, 0.0);
    return channelByte(texel.r) | (channelByte(texel.g) << 8u) | (channelByte(texel.b) << 16u) |
           (channelByte(texel.a) << 24u);
}

void decodeSurface(uint packed, out vec2 heights, out float biome, out int flags) {
    uint solid = packed & 0x3FFu;
    uint top = (packed >> 10u) & 0x3FFu;
    // A void top stores a marker in the solid bits; both heights are void then.
    heights = vec2(solid == 0u || top == 0u ? -1000.0 : float(solid) - 512.0,
                   top == 0u ? -1000.0 : float(top) - 512.0);
    biome = float((packed >> 20u) & 0xFFu) / 255.0;
    flags = int((packed >> 28u) & 0xFu);
}

const float VOID_HEIGHT = -999.0;
const float EDGE_BRIGHT = 1.18;
const float EDGE_DARK = 0.86;
const float BIOME_TINT_BRIGHTNESS = 1.30;
const int TINT_MASK = 3;
const int WATER_TINT = 1;
const int GRASS_TINT = 2;
const int LEAVES_TINT = 3;
const int WATER_OVERLAY = 4;
const int MISSING_CHUNK = 8;
/// Page-table index of the coarsest detailed level (32 blocks per texel).
const int MAX_LOD_INDEX = 5;

/// Colour and surface live in separate atlas textures, and reused layers are
/// cleared to zero. The tile builder never packs a delivered texel (void ones
/// included) to zero, so zero means the surface upload has not committed yet.
bool readSurface(vec3 uvLayer, out vec2 heights, out float biome, out int flags) {
    uint packed = surfaceCode(uvLayer);
    if (packed == 0u) return false;
    decodeSurface(packed, heights, biome, flags);
    return true;
}

vec3 paletteTint(float biomeId, int row) {
    float id = clamp(floor(biomeId * 255.0 + 0.5), 0.0, 255.0);
    vec3 tint = textureLod(uBiomePalette, vec2((id + 0.5) / 256.0, (float(row) + 0.5) / 3.0), 0.0).rgb;
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

bool targetSample(vec2 block, out vec3 color, out vec2 heights, out float biome, out int flags) {
    int desiredLod = max(0, int(floor(uParams.z + 0.5)));
    int layer = 0;
    vec2 uv;
    if (!residentLayer(block, desiredLod, layer, uv)) return false;
    vec3 uvLayer = vec3(uv, float(layer));
    vec4 texel = textureLod(uColor, uvLayer, 0.0);
    if (texel.a < 0.5) return false;
    color = texel.rgb;
    return readSurface(uvLayer, heights, biome, flags);
}

/// Heights and biome of a neighbouring block at the requested level. Lighting
/// and biome blending never read its colour, so the colour fetch is skipped.
bool neighbourSample(vec2 block, out vec2 heights, out float biome, out int flags) {
    int desiredLod = max(0, int(floor(uParams.z + 0.5)));
    int layer = 0;
    vec2 uv;
    if (!residentLayer(block, desiredLod, layer, uv)) return false;
    return readSurface(vec3(uv, float(layer)), heights, biome, flags);
}

bool neighbourSample(vec2 block, out vec2 heights, out float biome) {
    int flags;
    return neighbourSample(block, heights, biome, flags);
}

float lightingHeight(vec2 heights, int flags) {
    // Only transparent water exposes the bed; opaque water is lit at its surface.
    return (flags & WATER_OVERLAY) != 0 ? heights.r : heights.g;
}

float biomeAt(vec2 block, float fallbackBiome) {
    vec2 heights;
    float biome;
    if (!neighbourSample(block, heights, biome)) return fallbackBiome;
    return heights.g > VOID_HEIGHT ? biome : fallbackBiome;
}

vec3 biomeTint(vec2 world, int row, float fallbackBiome) {
    float texel = max(uParams.w, 1.0);
    float span = max(uShadow.w, texel);
    // Biomes change on a 4-block grid, or on the texel grid at coarser levels.
    // Lattice nodes placed on that grid sit exactly on possible boundaries and
    // sample the positive side, which drags every such blend half a span
    // towards negative x/z. Nodes at cell centres keep the blend centred.
    float phase = 0.5 * max(texel, min(span, 4.0));
    vec2 cell = floor((world - phase) / span) * span + phase;
    vec2 local = clamp((world - cell) / span, 0.0, 1.0);
    float b00 = biomeAt(cell, fallbackBiome);
    float b10 = biomeAt(cell + vec2(span, 0.0), fallbackBiome);
    float b01 = biomeAt(cell + vec2(0.0, span), fallbackBiome);
    float b11 = biomeAt(cell + vec2(span), fallbackBiome);
    vec3 north = mix(paletteTint(b00, row), paletteTint(b10, row), local.x);
    vec3 south = mix(paletteTint(b01, row), paletteTint(b11, row), local.x);
    return mix(north, south, local.y);
}

vec3 surfaceMaterial(vec2 world, vec3 surface, vec2 heights, float biome, int flags) {
    if ((flags & TINT_MASK) == WATER_TINT) surface *= biomeTint(world, 0, biome);
    if ((flags & TINT_MASK) == LEAVES_TINT) surface *= biomeTint(world, 1, biome);
    if ((flags & TINT_MASK) == GRASS_TINT) {
        surface *= biomeTint(world, 2, biome);
        if (uGrass.z > 0.5) {
            float heightMix = clamp((heights.g - uGrass.x) / max(uGrass.y, 1.0), 0.0, 1.0);
            surface = mix(surface, uGrassColor.rgb, heightMix);
        }
    }
    if ((flags & WATER_OVERLAY) != 0) {
        vec3 water = uWater.rgb * biomeTint(world, 0, biome);
        float opacity = clamp(0.30 + 0.15 * max(heights.g - heights.r, 0.0), 0.0, 0.92);
        surface = mix(surface, water, opacity);
    }
    return surface;
}

/// Same material as surfaceMaterial(), but with the tile's own biome instead of
/// a neighbourhood blend, so fallback levels keep their colour at one palette
/// fetch per tint.
vec3 fallbackSurfaceMaterial(vec3 surface, vec2 heights, float biome, int flags) {
    if ((flags & TINT_MASK) == WATER_TINT) surface *= paletteTint(biome, 0);
    if ((flags & TINT_MASK) == LEAVES_TINT) surface *= paletteTint(biome, 1);
    if ((flags & TINT_MASK) == GRASS_TINT) {
        surface *= paletteTint(biome, 2);
        if (uGrass.z > 0.5) {
            float heightMix = clamp((heights.g - uGrass.x) / max(uGrass.y, 1.0), 0.0, 1.0);
            surface = mix(surface, uGrassColor.rgb, heightMix);
        }
    }
    if ((flags & WATER_OVERLAY) != 0) {
        float opacity = clamp(0.30 + 0.15 * max(heights.g - heights.r, 0.0), 0.0, 0.92);
        surface = mix(surface, uWater.rgb * paletteTint(biome, 0), opacity);
    }
    return surface;
}

/// Brightness and saturation shared by the target and fallback paths.
vec3 gradeColor(vec3 color, float factor) {
    color = clamp(color * uTint.y, 0.0, 1.0);
    color = clamp(color * factor, 0.0, 1.0);
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return clamp(mix(vec3(luma), color, uTint.x), 0.0, 1.0);
}

bool fallbackColor(vec2 block, int lod, out vec3 color) {
    int layer = 0;
    vec2 uv;
    if (!residentLayer(block, lod, layer, uv)) return false;
    vec4 texel = textureLod(uColor, vec3(uv, float(layer)), 0.0);
    if (texel.a < 0.5) return false;
    vec2 heights;
    float biome;
    int flags;
    if (!readSurface(vec3(uv, float(layer)), heights, biome, flags)) return false;
    const bool voidSample = heights.g <= VOID_HEIGHT;
    if ((flags & MISSING_CHUNK) != 0 || (uVoidOptions.x > 0.5 && voidSample)) {
        color = emptyColor(block);
    } else {
        // Fallback skips neighbour-based lighting and biome blending while
        // preserving the current tile's water and material tint.
        color = gradeColor(fallbackSurfaceMaterial(texel.rgb, heights, biome, flags), 1.0);
    }
    return true;
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
    float inverseDistances[8];
    float stepFraction = 1.0 / float(steps - 1);
    for (int k = 0; k < 8; ++k) {
        if (k >= steps) break;
        float t = float(k) * stepFraction;
        distances[k] = 1.0 + (radius - 1.0) * t * t;
        inverseDistances[k] = 1.0 / distances[k];
    }
    float sum = 0.0;
    for (int i = 0; i < 8; ++i) {
        float steepest = 0.0;
        for (int k = 0; k < 8; ++k) {
            if (k >= steps) break;
            vec2 h;
            float biome;
            int flags;
            if (neighbourSample(floor(world + AO_DIRECTIONS[i] * distances[k]), h, biome, flags) &&
                (flags & MISSING_CHUNK) == 0 && h.g > VOID_HEIGHT)
                steepest = max(steepest, (lightingHeight(h, flags) - surfaceHeight) * inverseDistances[k]);
        }
        sum += steepest * inversesqrt(1.0 + steepest * steepest);
    }
    return 1.0 - strength * clamp(sum * 0.125, 0.0, 1.0);
}

float shadowOcclusion(vec2 world, float top) {
    if (uShade.z <= 0.0 || uParams.z >= 1.0) return 0.0;
    float stepBlocks = max(uShade.w, 1.0);
    for (int i = 1; i <= 64; ++i) {
        float travelled = float(i) * stepBlocks;
        if (travelled > uShade.z) break;
        vec2 h;
        float biome;
        if (neighbourSample(floor(world + uShade.xy * travelled), h, biome) && h.g > top + travelled) return 1.0;
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
            if ((coarser <= MAX_LOD_INDEX && fallbackColor(world, coarser, color)) ||
                (finer >= 0 && fallbackColor(world, finer, color))) {
                fragColor = vec4(color, 1.0);
                return;
            }
        }
        fragColor = overviewOrEmpty(world);
        return;
    }

    if ((flags & MISSING_CHUNK) != 0) {
        fragColor = vec4(emptyColor(world), 1.0);
        return;
    }
    if (heights.g <= VOID_HEIGHT) {
        fragColor = vec4(uVoidOptions.x > 0.5 ? emptyColor(world) : color, 1.0);
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
