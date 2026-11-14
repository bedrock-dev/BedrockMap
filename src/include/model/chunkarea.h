#ifndef BEDROCKMAP_CHUNKAREA_H
#define BEDROCKMAP_CHUNKAREA_H

#include <algorithm>
#include <cstdint>

/// An inclusive rectangular range of chunks in one dimension.
struct ChunkArea {
    int minChunkX{0};
    int minChunkZ{0};
    int maxChunkX{-1};
    int maxChunkZ{-1};
    int dimension{0};

    [[nodiscard]] bool valid() const { return dimension >= 0 && maxChunkX >= minChunkX && maxChunkZ >= minChunkZ; }

    [[nodiscard]] int width() const { return valid() ? maxChunkX - minChunkX + 1 : 0; }

    [[nodiscard]] int height() const { return valid() ? maxChunkZ - minChunkZ + 1 : 0; }

    [[nodiscard]] std::int64_t chunkCount() const {
        return valid() ? static_cast<std::int64_t>(width()) * static_cast<std::int64_t>(height()) : 0;
    }

    [[nodiscard]] bool contains(int chunkX, int chunkZ) const {
        return valid() && chunkX >= minChunkX && chunkX <= maxChunkX && chunkZ >= minChunkZ && chunkZ <= maxChunkZ;
    }

    [[nodiscard]] bool intersects(const ChunkArea& other) const {
        return valid() && other.valid() && dimension == other.dimension && minChunkX <= other.maxChunkX && maxChunkX >= other.minChunkX &&
               minChunkZ <= other.maxChunkZ && maxChunkZ >= other.minChunkZ;
    }

    [[nodiscard]] ChunkArea intersected(const ChunkArea& other) const {
        if (!intersects(other)) return ChunkArea{0, 0, -1, -1, dimension};
        return {std::max(minChunkX, other.minChunkX), std::max(minChunkZ, other.minChunkZ), std::min(maxChunkX, other.maxChunkX),
                std::min(maxChunkZ, other.maxChunkZ), dimension};
    }

    [[nodiscard]] bool operator==(const ChunkArea& other) const {
        return minChunkX == other.minChunkX && minChunkZ == other.minChunkZ && maxChunkX == other.maxChunkX &&
               maxChunkZ == other.maxChunkZ && dimension == other.dimension;
    }
};

#endif  // BEDROCKMAP_CHUNKAREA_H
