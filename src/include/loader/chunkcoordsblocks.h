#ifndef BEDROCKMAP_CHUNKCOORDSBLOCKS_H
#define BEDROCKMAP_CHUNKCOORDSBLOCKS_H

#include <string_view>

namespace chunkcoords_blocks {
    enum class Status { Found, Empty, Unsupported, Malformed };

    struct Sample {
        Status status{Status::Malformed};
        int y{0};
        // Borrows the input value; consume before releasing that value.
        std::string_view name;
    };

    /// Sample column (8, 8) at the trusted height-map Y, base storage only.
    /// Supports the persistent palette formats v8/v9 without expanding blocks.
    [[nodiscard]] Sample sampleBlock(std::string_view value, int world_y);
}  // namespace chunkcoords_blocks

#endif  // BEDROCKMAP_CHUNKCOORDSBLOCKS_H
