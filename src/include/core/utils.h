#ifndef BEDROCKMAP_UTILS_H
#define BEDROCKMAP_UTILS_H

#include <algorithm>
#include <utility>

#include "chunk.h"

// Normalize two chunk positions so that start <= end for x and z.
// Returns a pair: {min_pos, max_pos} so callers can use C++17 structured bindings
inline std::pair<bl::chunk_pos, bl::chunk_pos> normalize_chunk_range(const bl::chunk_pos &a, const bl::chunk_pos &b) {
    bl::chunk_pos out_min, out_max;
    out_min.dim = out_max.dim = a.dim;
    out_min.x = std::min(a.x, b.x);
    out_max.x = std::max(a.x, b.x);
    out_min.z = std::min(a.z, b.z);
    out_max.z = std::max(a.z, b.z);
    return {out_min, out_max};
}

#endif  // BEDROCKMAP_UTILS_H
