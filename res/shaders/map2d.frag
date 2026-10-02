#version 330 core

// Per-pixel shading for the 2D map. The whole world is a pair of textures with
// one texel per block:
//   uColor  : rgb = raw block palette colour, a = 255 with a water overlay
//   uHeight : r   = solid surface Y (bevel comparison),
//             g   = top surface Y (shadow occluder)
//   uMaterial: r = categorical biome id, g = water/grass/leaves/overlay/terrain bits
// Both are stored in one world-aligned atlas, so a neighbour lookup anywhere -
// including across region borders - is a plain texture fetch. Bevel and shadow
// are evaluated per screen pixel, which is why neither the tile resolution nor
// the shadow map resolution appear here as knobs.

uniform sampler2D uColor;
uniform sampler2D uHeight;
uniform sampler2D uMaterial;
uniform sampler2D uBiomePalette;

uniform vec2 uViewOrigin;      // world (x, z) at gl_FragCoord (0, 0), i.e. the bottom-left pixel
uniform float uPxPerBlock;     // device pixels per block
uniform float uAtlasTexels;    // atlas edge length, in texels
uniform float uBlocksPerTexel; // world blocks covered by one atlas texel (>= 1)
uniform vec2 uSunStep;         // one ray step towards the sun, in blocks (45 deg)
uniform float uShadowDarkness; // colour multiplier where the shadow is fully dark
uniform float uShadowStrength; // how much of that darkening the shadow applies
uniform float uShadowReach;    // ray march length, in blocks
uniform float uShadowStep;     // distance between shadow samples, in blocks
uniform float uEdgeWidth;      // bevel width, as a fraction of one texel
uniform float uAoStrength;     // ambient occlusion depth; 0 disables it
uniform int uAoDirections;     // azimuths marched for the ambient occlusion
uniform int uAoSteps;          // samples taken along each azimuth
uniform float uAoStep0;        // distance to the first sample, in blocks
uniform float uAoRadius;       // distance to the last sample, in blocks
uniform float uBevelStrength;  // bevel depth; 0 removes it, 1 is the full bevel
uniform float uSaturation;     // 0 = greyscale, 1 = the colours as stored, 2 = boosted
uniform float uBrightness;     // 1 = neutral, 0 = black, 2 = twice as bright
uniform float uBiomeBlendBlocks; // biome tint interpolation span in blocks
uniform float uGrassHeightEnabled;
uniform float uGrassHeightBase;
uniform float uGrassHeightRange;
uniform vec3 uGrassHeightColor;
uniform float uFlatShading;    // 1 = output the raw colour, for A/B comparison
uniform vec3 uWaterBaseColor;  // unblended minecraft:water palette colour

out vec4 fragColor;

const float VOID_HEIGHT = -999.0;  // column with no blocks at all
const float EDGE_BRIGHT = 1.18;
// The dark half of the bevel is only a crisp line on the edge itself; the wider
// occlusion gradient is ambientOcclusion()'s job now, so this is kept shallow
// rather than doing both jobs as it did before.
const float EDGE_DARK = 0.86;
const int MAX_SHADOW_STEPS = 2048;
// Upper bounds for the ambient occlusion march; the uniforms select how much of it
// is used, so the cost is adjustable without recompiling.
const int MAX_AO_DIRECTIONS = 16;
const int MAX_AO_STEPS = 32;
// This compensation affects only biome-tinted materials. Global brightness
// would also wash out sand, snow and other already-bright blocks.
const float BIOME_TINT_BRIGHTNESS = 1.30;
// Biome tint is deliberately filtered over world blocks rather than framebuffer
// pixels. At full atlas resolution it makes each transition sixteen blocks wide;
// a coarse atlas cannot resolve a footprint smaller than one of its texels.
// One atlas texel covers uBlocksPerTexel blocks, so sampling snaps to the texel
// grid. At 1 that grid is the block grid itself.
vec2 atlasUV(vec2 block) { return fract((floor(block / uBlocksPerTexel) + 0.5) / uAtlasTexels); }

vec4 colorAt(vec2 block) { return texture(uColor, atlasUV(block)); }
vec2 materialAt(vec2 block) { return texture(uMaterial, atlasUV(block)).rg; }

int materialFlags(vec2 material) { return int(floor(material.g * 255.0 + 0.5)); }

const int TERRAIN_SAMPLE = 16;

bool hasBiomeSample(vec2 block) { return (materialFlags(materialAt(block)) & TERRAIN_SAMPLE) != 0; }

vec3 paletteTint(float biome_id, int row) {
    int id = clamp(int(floor(biome_id * 255.0 + 0.5)), 0, 255);
    vec3 tint = texelFetch(uBiomePalette, ivec2(id, row), 0).rgb;
    return clamp(tint * BIOME_TINT_BRIGHTNESS, 0.0, 1.0);
}

// Biomes are categorical IDs: interpolate their resolved RGB tint colours, not
// the IDs themselves. Neighbours without terrain use the shaded texel's biome.
vec3 interpolatedBiomeTint(vec2 world, int row, float fallback_biome) {
    float span = max(uBiomeBlendBlocks, uBlocksPerTexel);
    vec2 cell = floor(world / span) * span;
    vec2 local = (world - cell) / span;
    vec2 east = cell + vec2(span, 0.0);
    vec2 south = cell + vec2(0.0, span);
    vec2 south_east = cell + vec2(span);
    float b00 = hasBiomeSample(cell) ? materialAt(cell).r : fallback_biome;
    float b10 = hasBiomeSample(east) ? materialAt(east).r : fallback_biome;
    float b01 = hasBiomeSample(south) ? materialAt(south).r : fallback_biome;
    float b11 = hasBiomeSample(south_east) ? materialAt(south_east).r : fallback_biome;
    vec3 north = mix(paletteTint(b00, row), paletteTint(b10, row), local.x);
    vec3 south_tint = mix(paletteTint(b01, row), paletteTint(b11, row), local.x);
    return mix(north, south_tint, local.y);
}

// (solid surface, top surface); solid == VOID_HEIGHT for a block-less column.
vec2 heightAt(vec2 block) { return texture(uHeight, atlasUV(block)).rg; }

// Height of a column as an occluder. A column with no blocks occludes nothing, so it
// reads as the height of the column being shaded instead of the void sentinel.
float occluderHeight(vec2 block, float fallback) {
    float h = heightAt(block).r;
    return (h <= VOID_HEIGHT) ? fallback : h;
}

// Bevel from the two neighbours the light comes from: world -x and -z, i.e. the
// top-left of the map.
//
// Only those two are compared, and the tint follows the comparison rather than
// the direction of the step, which is what puts the light somewhere specific:
//   - a raised area is lit along its west and north edges (this block is the
//     higher one there), and
//   - the shadow falls on the *lower* column's west and north edge, i.e. just
//     outside a raised area's east and south sides.
// Comparing all four neighbours instead - the previous behaviour - shaded every
// step the same way, so the light direction cancelled out and a raised area came
// out outlined on all four sides. It is also the rule renderStyle1 bakes into
// the CPU tiles, which samples only the sun-side neighbours for the same reason.
float bevelFactor(float t, float cur, float lightSide) {
    if (t <= 0.0) return 1.0;
    if (cur > lightSide) return 1.0 + (EDGE_BRIGHT - 1.0) * t;
    if (cur < lightSide) return 1.0 + (EDGE_DARK - 1.0) * t;
    return 1.0;
}

// At a corner two bevel ramps can cover the same pixel. Keep the strongest
// single edge contribution instead of multiplying both ramps, otherwise a
// corner becomes brighter/darker than either of its adjoining edges.
float dominantBevel(float a, float b) {
    return abs(a - 1.0) >= abs(b - 1.0) ? a : b;
}

// Continuation of the bevel across the light-side corner.
//
// The bevel only looks west and north, so where the only height difference is the
// north-west diagonal both bands stop one block short of the corner and the block
// between them gets neither: a raised area's south-east corner leaves a gap in the
// shadow outside it, and a pit's south-east corner leaves one in the light inside
// it.
//
// This applies the same tint at that block's north-west corner with the same width
// and constants, so the two bands meet instead of stopping. Both conditions have
// to be a corner patch rather than a band:
//   - `tx * ty`, so it falls off along both edges away from the corner and equals
//     the neighbouring block's band along each shared border (one factor is 1
//     there);
//   - both ramps in range, so it cannot reach the far end of either edge.
// Either one alone still lets it run: a max() weight held full strength along both
// whole edges, and testing the max of the two ramps let it stay on past the point
// where one edge had already left the corner - in both cases the shadow went on for
// a full block over ground with no step at all.
float lightCornerContinuity(float tx, float ty, float cur, float left, float up, float diagonal) {
    // Either ramp leaving this block means the patch is over: it belongs to the
    // corner, and both axes have to be in range for the pixel to be in it.
    if (tx <= 0.0 || ty <= 0.0) return 1.0;
    // A step along an edge is the bevel's job; only a purely diagonal one is left.
    if (left != cur || up != cur) return 1.0;
    float t = tx * ty;
    if (diagonal < cur) return 1.0 + (EDGE_BRIGHT - 1.0) * t;
    if (diagonal > cur) return 1.0 + (EDGE_DARK - 1.0) * t;
    return 1.0;
}

// Ambient occlusion, marched along a set of azimuths rather than read off the eight
// neighbouring cells.
//
// For each direction, march outwards and keep the steepest elevation angle any
// occluder subtends - that direction's horizon. A horizon at elevation a hides
// just in every direction and evaluated at the pixel instead of along one ray.
//
// It also removes the lines that the eight-neighbour form kept drawing. Those came
// from the field depending on the *pattern* of neighbour heights, which changes
// abruptly when a block border is crossed even where the terrain does not: a band that
// ended in the middle of the plate had to be smoothed by hand, and every smoothing
// attempt traded one line for another (a wider band, then a band that ended at the
// step's ends). Here the occlusion is a function of the terrain around the pixel and
// the sample distances are a continuous function of the pixel position, so two blocks
// at the same height genuinely see the same thing and there is nothing left to trade.
float ambientOcclusion(vec2 world, float solid, float strength) {
    if (strength <= 0.0) return 1.0;

    float sum = 0.0;
    const float tau = 6.28318530718;
    // Samples are spread geometrically between the first step and the radius, so the
    // near field - where a wall half a block away subtends the steepest angle - is
    // resolved finely and the far field is not sampled more than it is worth. Deriving
    // the growth from the two distances keeps the reach fixed as the step count changes.
    float growth = 1.0;
    if (uAoSteps > 1 && uAoStep0 > 0.0) growth = pow(max(uAoRadius, uAoStep0) / uAoStep0, 1.0 / float(uAoSteps - 1));

    for (int i = 0; i < MAX_AO_DIRECTIONS; ++i) {
        if (i >= uAoDirections) break;
        // Half a step off the axes, so no direction runs exactly along the block grid
        // and a wall is never measured only by the row of cells it stands on.
        float azimuth = (float(i) + 0.5) * tau / float(uAoDirections);
        vec2 dir = vec2(cos(azimuth), sin(azimuth));

        float distance = uAoStep0;
        float steepest = 0.0;
        for (int k = 0; k < MAX_AO_STEPS; ++k) {
            if (k >= uAoSteps) break;
            // Height of the cell the sample landed in, against the pixel's own height.
            float height = occluderHeight(floor(world + dir * distance), solid);
            steepest = max(steepest, (height - solid) / max(distance, 0.125));
            distance *= growth;
        }
        // sin(atan(steepest)) - the share of this direction's sky the horizon covers.
        sum += steepest / sqrt(1.0 + steepest * steepest);
    }

    float occlusion = sum / float(max(uAoDirections, 1));
    return 1.0 - strength * clamp(occlusion, 0.0, 1.0);
}

float bevelLodFade() {
    if (uBlocksPerTexel > 8.0) return 0.;
    if (uBlocksPerTexel > 4.0) return 0.1;
    if (uBlocksPerTexel > 1.0) return 0.6;
    return 1.0;
}

float aoLodFade() {
    if (uBlocksPerTexel >= 4.0) return 0.2;
    if (uBlocksPerTexel >= 2.0) return 0.4;
    return 1.0;
}

// A high-resolution world-space shadow ray. The height atlas is still sampled
// with nearest filtering, but the receiver and the ray position stay continuous
// between atlas texels, so the shadow boundary is not forced onto whole-texel
// squares. uShadowStep controls the quality/performance trade-off.
float shadowOcclusion(vec2 world, float topY) {
    float step_blocks = max(uShadowStep, 0.0625);
    for (int i = 1; i <= MAX_SHADOW_STEPS; i++) {
        float travelled = float(i) * step_blocks;
        if (travelled > uShadowReach) break;
        float occluder = heightAt(world + uSunStep * travelled).g;
        // A genuine empty column is transparent to directional light; a later
        // hill can still shadow across it.
        if (occluder <= VOID_HEIGHT) continue;
        if (occluder > topY + travelled) return 1.0;
    }
    return 0.0;
}

void main() {
    // gl_FragCoord.y counts up from the bottom of the widget, while world z grows
    // downwards on screen, so the two run in opposite directions.
    vec2 world = vec2(uViewOrigin.x + gl_FragCoord.x / uPxPerBlock, uViewOrigin.y - gl_FragCoord.y / uPxPerBlock);

    // The texel this pixel belongs to, and where inside it the pixel sits.
    vec2 texel = floor(world / uBlocksPerTexel);
    vec2 cell = texel * uBlocksPerTexel;
    vec2 local = (world - cell) / uBlocksPerTexel;

    vec4 surface = colorAt(cell);
    vec2 heights = heightAt(cell);
    float solid = heights.r;
    float top = heights.g;

    // The colour texture carries 0 in alpha for dry columns (the flag the bevel
    // reads as "not under water"), so alpha is forced here: a column with no
    // blocks should show the region background, not a hole in the viewport.
    if (uFlatShading > 0.5 || top <= VOID_HEIGHT) {
        fragColor = vec4(surface.rgb, 1.0);
        return;
    }

    vec2 material = materialAt(cell);
    const int WATER_TINT = 1;
    const int GRASS_TINT = 2;
    const int LEAVES_TINT = 4;
    const int WATER_OVERLAY = 8;
    int flags = materialFlags(material);
    vec3 surface_rgb = surface.rgb;
    //blending
    if ((flags & (WATER_TINT | GRASS_TINT | LEAVES_TINT | WATER_OVERLAY)) != 0) {
        if ((flags & WATER_TINT) != 0) surface_rgb *= interpolatedBiomeTint(world, 0, material.r);
        if ((flags & LEAVES_TINT) != 0) surface_rgb *= interpolatedBiomeTint(world, 1, material.r);
        if ((flags & GRASS_TINT) != 0) surface_rgb *= interpolatedBiomeTint(world, 2, material.r);
        if ((flags & GRASS_TINT) != 0 && uGrassHeightEnabled > 0.5) {
            // Grass already carries its biome tint. Mix that resolved colour
            // towards the configured marker over the configured range above the base,
            // leaving lower terrain unchanged.
            float height_mix = clamp((top - uGrassHeightBase) / max(uGrassHeightRange, 1.0), 0.0, 1.0);
            surface_rgb = mix(surface_rgb, uGrassHeightColor, height_mix);
        }
        if ((flags & WATER_OVERLAY) != 0) {
            vec3 water = uWaterBaseColor * interpolatedBiomeTint(world, 0, material.r);
            float opacity = clamp(0.30 + 0.15 * max(top - solid, 0.0), 0.0, 0.92);
            surface_rgb = mix(surface_rgb, water, opacity);
        }
    }

    float factor = 1.0;

    //beval
    float bevel_fade = bevelLodFade() * uBevelStrength;
    if (bevel_fade > 0.0) {
        float w = uEdgeWidth * bevelLodFade();
        // Smooth the ramp at both ends: the bevel blends naturally into the
        // surface rather than changing slope abruptly at its inner boundary.
        float tLeft = smoothstep(0.0, 1.0, clamp(1.0 - local.x / w, 0.0, 1.0));
        float tTop = smoothstep(0.0, 1.0, clamp(1.0 - local.y / w, 0.0, 1.0));

        float left = heightAt(cell + vec2(-uBlocksPerTexel, 0.0)).r;
        float up = heightAt(cell + vec2(0.0, -uBlocksPerTexel)).r;
        float up_left = heightAt(cell + vec2(-uBlocksPerTexel, -uBlocksPerTexel)).r;

        float bevel = dominantBevel(bevelFactor(tLeft, solid, left), bevelFactor(tTop, solid, up));
        bevel = clamp(bevel, 0.55, 1.58) * lightCornerContinuity(tLeft, tTop, solid, left, up, up_left);
        factor = 1.0 + (bevel - 1.0) * bevel_fade;
    }

    // --- ambient occlusion (outside the bevel clamp: it is its own shading term) ---
    factor *= ambientOcclusion(world, solid, uAoStrength * aoLodFade());

    // Bevels and occlusion fade out under water the same way the CPU styles do:
    // the sea floor is what is being shaded, and a depth of five blocks hides it.
    if (surface.a > 0.5) {
        float waterFade = clamp(1.0 - (top - solid) / 5.0, 0.0, 1.0);
        factor = 1.0 + (factor - 1.0) * waterFade;
    }

    // --- shadow ---
    float occlusion = 0.0;
    // Water is rendered from the sea floor and must not become a second
    // shadow-casting surface; this matches the CPU bake's water rule.
    if (surface.a <= 0.5 && uShadowStrength > 0.0 && uShadowReach > 0.0)
        occlusion = shadowOcclusion(world, top) * uShadowStrength;
    factor *= mix(1.0, uShadowDarkness, clamp(occlusion, 0.0, 1.0));

    // --- base exposure and saturation ---
    vec3 rgb = clamp(surface_rgb * uBrightness, 0.0, 1.0);
    if (uSaturation != 1.0) {
        float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        rgb = clamp(mix(vec3(luma), rgb, uSaturation), 0.0, 1.0);
    }
    rgb = clamp(rgb * factor, 0.0, 1.0);
    fragColor = vec4(rgb, 1.0);
}
