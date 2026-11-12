// Contrasting outline where terrain meets missing chunks or void. Requires
// terrain_common.glsl.

vec3 contrastingOutlineColor(vec3 background) {
    float luminance = dot(background, vec3(0.2126, 0.7152, 0.0722));
    return luminance > 0.5 ? vec3(0.0) : vec3(1.0);
}

/// True only when the block is known to contain terrain. Empty tiles, missing
/// chunks, void and tiles that are not resident yet do not count as terrain.
bool terrainNeighbour(vec2 block) {
    int layer;
    vec2 uv;
    int state = tileState(block, max(0, int(floor(uParams.z + 0.5))), layer, uv);
    if (state != TILE_RESIDENT) return false;
    vec2 heights;
    float biome;
    int flags;
    if (!readSurface(vec3(uv, float(layer)), heights, biome, flags)) return false;
    return (flags & MISSING_CHUNK) == 0 && heights.g > VOID_HEIGHT;
}

/// Coverage of the contrasting outline drawn on the empty side of every edge
/// that borders terrain.
float outlineCoverage(vec2 world) {
    float width = uVoidOptions.y;
    if (width <= 0.0) return 0.0;
    float texel = max(uParams.w, 1.0);
    vec2 cell = floor(world / texel) * texel;
    vec2 local = world - cell;
    vec4 distances = vec4(local.x, texel - local.x, local.y, texel - local.y);
    float nearest = 1.0e9;
    if (distances.x < width && terrainNeighbour(cell + vec2(-texel, 0.0))) nearest = min(nearest, distances.x);
    if (distances.y < width && terrainNeighbour(cell + vec2(texel, 0.0))) nearest = min(nearest, distances.y);
    if (distances.z < width && terrainNeighbour(cell + vec2(0.0, -texel))) nearest = min(nearest, distances.z);
    if (distances.w < width && terrainNeighbour(cell + vec2(0.0, texel))) nearest = min(nearest, distances.w);
    return clamp((width - nearest) / max(uVoidOptions.z, 1.0e-4), 0.0, 1.0);
}

vec3 withTerrainOutline(vec3 background, vec2 world) {
    return mix(background, contrastingOutlineColor(background), outlineCoverage(world));
}
