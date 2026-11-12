// Resources, surface decoding and page-table lookup shared by the terrain
// fragment shader.

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
    vec4 uVoidOptions; // transparent void, outline width in blocks (0 = off), blocks per pixel
};

const float VOID_HEIGHT = -999.0;
const int TINT_MASK = 3;
const int WATER_TINT = 1;
const int GRASS_TINT = 2;
const int LEAVES_TINT = 3;
const int WATER_OVERLAY = 4;
const int MISSING_CHUNK = 8;
/// Page-table index of the coarsest detailed level (32 blocks per texel).
const int MAX_LOD_INDEX = 5;

const int TILE_UNKNOWN = 0;
const int TILE_EMPTY = 1;
const int TILE_RESIDENT = 2;

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

vec3 voidColor() {
    // Void regions use a solid tone opposite the empty-tile palette.
    vec3 emptyAverage = (uChecker.rgb + uCheckerAlternate.rgb) * 0.5;
    float luminance = dot(emptyAverage, vec3(0.2126, 0.7152, 0.0722));
    return luminance > 0.5 ? vec3(20.0 / 255.0) : vec3(221.0 / 255.0);
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

/// Colour and surface live in separate atlas textures, and reused layers are
/// cleared to zero. The tile builder never packs a delivered texel (void ones
/// included) to zero, so zero means the surface upload has not committed yet.
bool readSurface(vec3 uvLayer, out vec2 heights, out float biome, out int flags) {
    uint packed = surfaceCode(uvLayer);
    if (packed == 0u) return false;
    decodeSurface(packed, heights, biome, flags);
    return true;
}

/// Page-table state of the tile holding block. Empty tiles are resident but
/// contain no chunks, so they carry no layer.
int tileState(vec2 block, int lod, out int layer, out vec2 uv) {
    layer = 0;
    uv = vec2(0.0);
    float tileWorld = max(uParams.x, 1.0) * exp2(float(lod));
    vec2 tileCoord = floor(block / tileWorld);
    float side = max(uParams.y, 1.0);
    ivec2 vc = ivec2(mod(mod(tileCoord, side) + side, side));
    vec4 entry = texelFetch(uPageTable, ivec3(vc, lod), 0);
    int flags = int(floor(entry.b * 255.0 + 0.5));
    if ((flags & 0x80) == 0) return TILE_UNKNOWN;
    uint storedHash = (uint(flags & 0x3F) << 8) | uint(floor(entry.a * 255.0 + 0.5));
    if (storedHash != regionHash(ivec2(tileCoord))) return TILE_UNKNOWN;
    if ((flags & 0x40) != 0) return TILE_EMPTY;
    layer = int(floor(entry.r * 255.0 + 0.5)) + int(floor(entry.g * 255.0 + 0.5)) * 256;
    uv = fract(block / tileWorld);
    return TILE_RESIDENT;
}

bool residentLayer(vec2 block, int lod, out int layer, out vec2 uv) {
    return tileState(block, lod, layer, uv) == TILE_RESIDENT;
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
