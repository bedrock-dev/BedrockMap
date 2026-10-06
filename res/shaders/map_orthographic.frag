#version 330 core

// Fixed orthographic height-field view. The camera sits on the (1, 1, 1) axis and
// looks along (-1, -1, -1); the screen basis is
//     right = ( 1, 0, -1) / sqrt(2)
//     up    = (-1, 2, -1) / sqrt(6)
// both orthogonal to the view direction. North (-z) and west (-x) therefore run
// up the screen, exactly as they do on the 2D map. A screen pixel is resolved by
// a 2D DDA over the atlas texel grid, comparing the ray height against each
// column's top, which keeps the renderer independent of a CPU-side mesh and
// preserves the existing asynchronous region uploads.
uniform sampler2D uColor;
uniform sampler2D uHeight;
uniform sampler2D uMaterial;
uniform sampler2D uBiomePalette;

uniform vec2 uIsoCenter;             // world x/z at the screen centre
uniform float uIsoReferenceHeight;   // world height at the screen centre
uniform vec2 uIsoViewport;           // device pixels
uniform float uIsoRayTop;            // height the march starts from
uniform float uIsoRayBottom;         // height the march gives up at
uniform float uPxPerBlock;
uniform float uAtlasTexels;
uniform float uBlocksPerTexel;
uniform float uAoStrength;
uniform int uAoDirections;
uniform int uAoSteps;
uniform float uAoStep0;
uniform float uAoRadius;
uniform float uSaturation;
uniform float uBrightness;
uniform float uBiomeBlendBlocks;
uniform float uGrassHeightEnabled;
uniform float uGrassHeightBase;
uniform float uGrassHeightRange;
uniform vec3 uGrassHeightColor;

out vec4 fragColor;

const float VOID_HEIGHT = -999.0;
const float SQRT2 = 1.4142135623730951;
const float SQRT6 = 2.449489742783178;
// The march covers (uIsoRayTop - uIsoRayBottom) of height, and each step moves
// one texel on one axis, so at one block per texel it needs twice that many.
const int MAX_RAY_STEPS = 1024;
const int MAX_AO_DIRECTIONS = 16;
const int MAX_AO_STEPS = 32;
// Face shading: the top is brightest, the +z side catches the sun and the +x side
// faces away from it.
const vec3 SUN = normalize(vec3(-0.45, 0.85, 0.28));
const int TERRAIN_SAMPLE = 16;
const int WATER_TINT = 1;
const int GRASS_TINT = 2;
const int LEAVES_TINT = 4;
const int WATER_OVERLAY = 8;
const float BIOME_TINT_BRIGHTNESS = 1.30;
vec2 atlasUV(vec2 block) { return fract((floor(block / uBlocksPerTexel) + 0.5) / uAtlasTexels); }
vec4 colorAt(vec2 block) { return texture(uColor, atlasUV(block)); }
vec2 heightAt(vec2 block) { return texture(uHeight, atlasUV(block)).rg; }
vec2 materialAt(vec2 block) { return texture(uMaterial, atlasUV(block)).rg; }
int materialFlags(vec2 material) { return int(floor(material.g * 255.0 + 0.5)); }
bool hasBiomeSample(vec2 block) { return (materialFlags(materialAt(block)) & TERRAIN_SAMPLE) != 0; }

vec3 paletteTint(float biome_id, int row) {
    int id = clamp(int(floor(biome_id * 255.0 + 0.5)), 0, 255);
    return clamp(texelFetch(uBiomePalette, ivec2(id, row), 0).rgb * BIOME_TINT_BRIGHTNESS, 0.0, 1.0);
}

vec3 interpolatedBiomeTint(vec2 world, int row, float fallback_biome) {
    float span = max(uBiomeBlendBlocks, uBlocksPerTexel);
    // Keep the blend grid centred on the actual boundary instead of placing
    // its sample points at the north-west corner of each blend cell.
    vec2 cell = floor((world + 0.5 * span) / span) * span - 0.5 * span;
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

float occluderHeight(vec2 block, float fallback) {
    float h = heightAt(block).r;
    return h <= VOID_HEIGHT ? fallback : h;
}

float ambientOcclusion(vec2 world, float solid) {
    if (uAoStrength <= 0.0) return 1.0;
    float sum = 0.0;
    const float tau = 6.28318530718;
    float growth = 1.0;
    if (uAoSteps > 1 && uAoStep0 > 0.0)
        growth = pow(max(uAoRadius, uAoStep0) / uAoStep0, 1.0 / float(uAoSteps - 1));
    for (int i = 0; i < MAX_AO_DIRECTIONS; ++i) {
        if (i >= uAoDirections) break;
        float azimuth = (float(i) + 0.5) * tau / float(max(uAoDirections, 1));
        vec2 dir = vec2(cos(azimuth), sin(azimuth));
        float distance = uAoStep0;
        float steepest = 0.0;
        for (int k = 0; k < MAX_AO_STEPS; ++k) {
            if (k >= uAoSteps) break;
            float h = occluderHeight(floor(world + dir * distance), solid);
            steepest = max(steepest, (h - solid) / max(distance, 0.125));
            distance *= growth;
        }
        sum += steepest / sqrt(1.0 + steepest * steepest);
    }
    return 1.0 - uAoStrength * clamp(sum / float(max(uAoDirections, 1)), 0.0, 1.0);
}

void main() {
    vec2 screen = (gl_FragCoord.xy - 0.5 * uIsoViewport) / uPxPerBlock;

    // A pixel's ray is the set of world points that satisfy
    //     screen.x = (x - z) / sqrt(2)
    //     screen.y = (-x + 2*y - z) / sqrt(6)
    // relative to uIsoCenter. Solving the second row for y inverts the projection.
    float difference = screen.x * SQRT2;
    float start_sum = -screen.y * SQRT6 + 2.0 * (uIsoRayTop - uIsoReferenceHeight);
    vec2 ray_start = uIsoCenter + 0.5 * vec2(start_sum + difference, start_sum - difference);

    // DDA over the texel grid. The ray descends (-1, -1, -1), so both world axes
    // lose one block per block of height descended and the march parameter is
    // simply that descent: world = ray_start - t.
    float blocks_per_texel = uBlocksPerTexel;
    vec2 cell = floor(ray_start / blocks_per_texel);
    vec2 cell_exit = ray_start - cell * blocks_per_texel;

    vec2 hit_world = vec2(0.0);
    vec2 hit_heights = vec2(VOID_HEIGHT);
    vec3 hit_normal = vec3(0.0, 1.0, 0.0);
    bool hit = false;
    int from_axis = 0;  // 0: entered across an x boundary, 1: across a z boundary
    bool left_loaded = true;  // the ray starts above everything, outside any column
    float t = 0.0;

    for (int i = 0; i < MAX_RAY_STEPS; ++i) {
        float y = uIsoRayTop - t;
        if (y < uIsoRayBottom) break;

        vec2 heights = heightAt(cell * blocks_per_texel);
        float top = heights.g;
        bool loaded = top > VOID_HEIGHT;
        if (loaded && y <= top) {
            // The ray is inside the column, so this cell is the visible wall. Its
            // colour is read from the cell itself rather than from the entry
            // point, which sits on the boundary shared with the column just left:
            // floor() there resolves back to that neighbour - an empty column at a
            // wall's foot, whose placeholder tile strobes along the side.
            hit_world = cell * blocks_per_texel;
            hit_heights = heights;
            // Stepping in off empty space is the border of the baked area, not a
            // cliff, so it keeps the column's own top colour; a region therefore
            // gets no curtain of shaded walls around it.
            hit_normal = !left_loaded                ? vec3(0.0, 1.0, 0.0)
                         : (from_axis == 0)          ? vec3(1.0, 0.0, 0.0)
                                                     : vec3(0.0, 0.0, 1.0);
            hit = true;
            break;
        }
        if (loaded) {
            // Otherwise it can only land on the top, and only inside this cell:
            // a crossing before the entry belongs to a column further back.
            float t_top = uIsoRayTop - top;
            if (t_top >= t && t_top < min(cell_exit.x, cell_exit.y)) {
                hit_world = ray_start - vec2(t_top);
                hit_heights = heights;
                hit = true;
                break;
            }
        }
        left_loaded = loaded;

        if (cell_exit.x < cell_exit.y) {
            cell.x -= 1.0;
            t = cell_exit.x;
            cell_exit.x += blocks_per_texel;
            from_axis = 0;
        } else {
            cell.y -= 1.0;
            t = cell_exit.y;
            cell_exit.y += blocks_per_texel;
            from_axis = 1;
        }
    }

    if (!hit) {
        fragColor = vec4(0.08, 0.08, 0.09, 1.0);
        return;
    }

    vec4 surface = colorAt(hit_world);
    vec2 material = materialAt(hit_world);
    int flags = materialFlags(material);
    vec3 rgb = surface.rgb;
    if ((flags & (WATER_TINT | GRASS_TINT | LEAVES_TINT | WATER_OVERLAY)) != 0) {
        if ((flags & WATER_TINT) != 0) rgb *= interpolatedBiomeTint(hit_world, 0, material.r);
        if ((flags & LEAVES_TINT) != 0) rgb *= interpolatedBiomeTint(hit_world, 1, material.r);
        if ((flags & GRASS_TINT) != 0) {
            rgb *= interpolatedBiomeTint(hit_world, 2, material.r);
            if (uGrassHeightEnabled > 0.5)
                rgb = mix(rgb, uGrassHeightColor,
                          clamp((hit_heights.g - uGrassHeightBase) / max(uGrassHeightRange, 1.0), 0.0, 1.0));
        }
        if ((flags & WATER_OVERLAY) != 0) {
            vec3 water = interpolatedBiomeTint(hit_world, 0, material.r);
            rgb = mix(rgb, water, 0.30);
        }
    }

    float factor = ambientOcclusion(hit_world, hit_heights.r);
    factor *= 0.72 + 0.30 * max(dot(hit_normal, SUN), 0.0);
    rgb = clamp(rgb * uBrightness, 0.0, 1.0);
    if (uSaturation != 1.0) {
        float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        rgb = clamp(mix(vec3(luma), rgb, uSaturation), 0.0, 1.0);
    }
    fragColor = vec4(clamp(rgb * factor, 0.0, 1.0), 1.0);
}
