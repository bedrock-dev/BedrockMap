#version 330 core

// Per-pixel shading for the 2D map. The whole world is a pair of textures with
// one texel per block:
//   uColor  : rgb = final block colour (biome tint + water already blended),
//             a   = 255 when the column has a water surface, 0 otherwise
//   uHeight : r   = solid surface Y (bevel comparison),
//             g   = top surface Y (shadow occluder)
// Both are stored in one world-aligned atlas, so a neighbour lookup anywhere -
// including across region borders - is a plain texture fetch. Bevel and shadow
// are evaluated per screen pixel, which is why neither the tile resolution nor
// the shadow map resolution appear here as knobs.

uniform sampler2D uColor;
uniform sampler2D uHeight;

uniform vec2 uViewOrigin;      // world (x, z) at gl_FragCoord (0, 0), i.e. the bottom-left pixel
uniform float uPxPerBlock;     // device pixels per block
uniform float uAtlasTexels;    // atlas edge length, in texels
uniform float uBlocksPerTexel; // world blocks covered by one atlas texel (>= 1)
uniform vec2 uSunStep;         // one ray step towards the sun, in blocks (45 deg)
uniform float uShadowDarkness; // colour multiplier where the shadow is fully dark
uniform float uShadowStrength; // how much of that darkening the shadow applies
uniform float uShadowReach;    // ray march length, in blocks
uniform float uEdgeWidth;      // bevel width, as a fraction of one texel
uniform float uPenumbra;       // penumbra slope: 0 = hard shadow, larger = softer
uniform float uAoStrength;     // ambient occlusion depth; 0 disables it
uniform int uAoDirections;     // azimuths marched for the ambient occlusion
uniform int uAoSteps;          // samples taken along each azimuth
uniform float uAoStep0;        // distance to the first sample, in blocks
uniform float uAoRadius;       // distance to the last sample, in blocks
uniform float uBevelStrength;  // bevel depth; 0 removes it, 1 is the full bevel
uniform float uSaturation;     // 0 = greyscale, 1 = the colours as stored, 2 = boosted
uniform float uBrightness;     // 1 = neutral, 0 = black, 2 = twice as bright
uniform float uFlatShading;    // 1 = output the raw colour, for A/B comparison

out vec4 fragColor;

const float VOID_HEIGHT = -999.0;  // column with no blocks at all
const float EDGE_BRIGHT = 1.18;
// The dark half of the bevel is only a crisp line on the edge itself; the wider
// occlusion gradient is ambientOcclusion()'s job now, so this is kept shallow
// rather than doing both jobs as it did before.
const float EDGE_DARK = 0.86;
const int MAX_SHADOW_STEPS = 256;
// Upper bounds for the ambient occlusion march; the uniforms select how much of it
// is used, so the cost is adjustable without recompiling.
const int MAX_AO_DIRECTIONS = 16;
const int MAX_AO_STEPS = 32;

// One atlas texel covers uBlocksPerTexel blocks, so sampling snaps to the texel
// grid. At 1 that grid is the block grid itself.
vec2 atlasUV(vec2 block) { return fract((floor(block / uBlocksPerTexel) + 0.5) / uAtlasTexels); }

vec4 colorAt(vec2 block) { return texture(uColor, atlasUV(block)); }

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
float ambientOcclusion(vec2 world, float solid) {
    if (uAoStrength <= 0.0) return 1.0;

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
    return 1.0 - uAoStrength * clamp(occlusion, 0.0, 1.0);
}

// Marches towards the sun and accumulates the strongest partial occlusion. A
// column counts as an occluder only in proportion to how far its height rises
// above the ray, which is the same relation the game's soft shadow has: an
// occluder barely above the line darkens weakly and the penumbra widens with
// distance, so shadow edges stay soft without jittering the ray.
//
// The ray starts at the pixel rather than at its block's centre, and steps by half
// a texel. Both matter: with a block-anchored ray every pixel of a block sampled the
// same points, so the occlusion was one value per block and the shadow's outline was
// quantised to the block grid. Anchored at the pixel, how much of the occluder stands
// above the ray changes continuously as the pixel moves, so the shadow's edge is a
// line at sub-block accuracy and the half-texel step keeps the near end of it from
// shifting a whole texel at a time.
float shadowOcclusion(vec2 world, float topY) {
    float occlusion = 0.0;
    float step = max(0.5 * uBlocksPerTexel, 0.0625);
    float rayHeight = topY + 1.0;
    vec2 p = world;
    for (int i = 1; i <= MAX_SHADOW_STEPS; i++) {
        float travelled = float(i) * step;
        if (travelled > uShadowReach) break;
        p += uSunStep * step;
        float occluder = heightAt(floor(p)).g;
        if (occluder <= VOID_HEIGHT) break;
        float rise = occluder - (rayHeight + travelled);
        if (rise > 0.0) {
            occlusion = max(occlusion, clamp(rise / (uPenumbra * travelled + 1.0), 0.0, 1.0));
            if (occlusion >= 0.999) break;
        }
    }
    return occlusion;
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

    // --- bevel from the two neighbours towards the light, continuous in screen
    // space ---
    float w = uEdgeWidth;
    float tLeft = clamp(1.0 - local.x / w, 0.0, 1.0);
    float tTop = clamp(1.0 - local.y / w, 0.0, 1.0);

    float left = heightAt(cell + vec2(-uBlocksPerTexel, 0.0)).r;
    float up = heightAt(cell + vec2(0.0, -uBlocksPerTexel)).r;
    float up_left = heightAt(cell + vec2(-uBlocksPerTexel, -uBlocksPerTexel)).r;

    float bevel = bevelFactor(tLeft, solid, left) * bevelFactor(tTop, solid, up);
    bevel = clamp(bevel, 0.55, 1.58) * lightCornerContinuity(tLeft, tTop, solid, left, up, up_left);
    // Scaling the bevel's distance from 1 rather than the factor itself keeps 0 an
    // exact switch: the shading is then just the flat colour.
    float factor = 1.0 + (bevel - 1.0) * uBevelStrength;

    // --- ambient occlusion (outside the bevel clamp: it is its own shading term) ---
    factor *= ambientOcclusion(world, solid);

    // Bevels and occlusion fade out under water the same way the CPU styles do:
    // the sea floor is what is being shaded, and a depth of five blocks hides it.
    if (surface.a > 0.5) {
        float waterFade = clamp(1.0 - (top - solid) / 5.0, 0.0, 1.0);
        factor = 1.0 + (factor - 1.0) * waterFade;
    }

    // --- shadow ---
    // The strength scales the occlusion rather than the colour, so 0 leaves the
    // shaded colour exactly as the other terms produced it.
    // A zero strength is an exact visual no-op.  Avoid the ray march entirely
    // in that case; otherwise the setting removes the shadow from the output
    // while still paying for every height lookup.
    float occlusion = 0.0;
    if (uShadowStrength > 0.0 && uShadowReach > 0.0) occlusion = shadowOcclusion(world, top) * uShadowStrength;
    factor *= mix(1.0, uShadowDarkness, clamp(occlusion, 0.0, 1.0));

    // --- saturation ---
    // Applied after the shading so the greys it mixes towards are the shaded ones.
    vec3 rgb = clamp(surface.rgb * factor * uBrightness, 0.0, 1.0);
    if (uSaturation != 1.0) {
        float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        rgb = clamp(mix(vec3(luma), rgb, uSaturation), 0.0, 1.0);
    }
    fragColor = vec4(rgb, 1.0);
}
