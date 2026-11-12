// Biome tints, material colour and grading. Requires terrain_common.glsl.

const float BIOME_TINT_BRIGHTNESS = 1.30;

vec3 paletteTint(float biomeId, int row) {
    float id = clamp(floor(biomeId * 255.0 + 0.5), 0.0, 255.0);
    vec3 tint = textureLod(uBiomePalette, vec2((id + 0.5) / 256.0, (float(row) + 0.5) / 3.0), 0.0).rgb;
    return clamp(tint * BIOME_TINT_BRIGHTNESS, 0.0, 1.0);
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

bool fallbackColor(vec2 block, int lod, out vec3 color, out bool empty) {
    empty = false;
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
    if ((flags & MISSING_CHUNK) != 0) {
        color = emptyColor(block);
        empty = true;
    } else if (voidSample) {
        color = uVoidOptions.x > 0.5 ? emptyColor(block) : voidColor();
        empty = true;
    } else {
        // Fallback skips neighbour-based lighting and biome blending while
        // preserving the current tile's water and material tint.
        color = gradeColor(fallbackSurfaceMaterial(texel.rgb, heights, biome, flags), 1.0);
    }
    return true;
}
