#include "loader/chunkcoordsblocks.h"

#include <cstdint>

#include "binary_io.h"

namespace {
    // A bounded little-endian NBT cursor. Palette entries are variable length;
    // skip their states without allocating the full NBT object tree.
    class PaletteCursor {
       public:
        explicit PaletteCursor(std::string_view data) : data_(data) {}

        bool skip(std::size_t count) {
            if (count > data_.size()) return false;
            data_.remove_prefix(count);
            return true;
        }

        bool byte(std::uint8_t& value) {
            if (data_.empty()) return false;
            value = static_cast<std::uint8_t>(data_.front());
            return skip(1);
        }

        bool string(std::string_view& value) {
            if (data_.size() < 2) return false;
            const auto length = bl::binary::read_u16_le(data_.data());
            if (!skip(2) || length > data_.size()) return false;
            value = data_.substr(0, length);
            return skip(length);
        }

        bool count(std::uint32_t& value) {
            if (data_.size() < 4) return false;
            value = bl::binary::read_u32_le(data_.data());
            return skip(4) && value <= 0x7fffffffu;
        }

        bool payload(std::uint8_t type, unsigned depth = 0) {
            if (depth > 64) return false;
            switch (type) {
                case 1:
                    return skip(1);
                case 2:
                    return skip(2);
                case 3:
                case 5:
                    return skip(4);
                case 4:
                case 6:
                    return skip(8);
                case 8: {
                    std::string_view ignored;
                    return string(ignored);
                }
                case 7:
                case 11:
                case 12: {
                    std::uint32_t length;
                    const std::size_t width = type == 7 ? 1 : (type == 11 ? 4 : 8);
                    return count(length) && length <= data_.size() / width && skip(length * width);
                }
                case 9: {
                    std::uint8_t element;
                    std::uint32_t length;
                    if (!byte(element) || !count(length) || length > data_.size() || (length && (element == 0 || element > 12)))
                        return false;
                    for (std::uint32_t i = 0; i < length; ++i)
                        if (!payload(element, depth + 1)) return false;
                    return true;
                }
                case 10: {
                    std::uint8_t child;
                    std::string_view key;
                    while (byte(child)) {
                        if (child == 0) return true;
                        if (!string(key) || !payload(child, depth + 1)) return false;
                    }
                    return false;
                }
                default:
                    return false;
            }
        }

        bool blockName(std::string_view& name) {
            std::uint8_t type;
            std::string_view key;
            if (!byte(type) || type != 10 || !string(key)) return false;
            while (byte(type)) {
                if (type == 0) return !name.empty();
                if (!string(key)) return false;
                if (type == 8 && key == "name") {
                    if (!string(name)) return false;
                } else if (!payload(type)) {
                    return false;
                }
            }
            return false;
        }

       private:
        std::string_view data_;
    };
}  // namespace

chunkcoords_blocks::Sample chunkcoords_blocks::sampleBlock(std::string_view value, int world_y) {
    if (value.empty()) return {};
    const auto version = static_cast<std::uint8_t>(value[0]);
    if (version != 8 && version != 9) return {Status::Unsupported};
    const std::size_t header = version == 9 ? 3 : 2;
    if (value.size() < header) return {};
    const int local_y = ((world_y % 16) + 16) % 16;
    const int subchunk_y = (world_y - local_y) / 16;
    if (version == 9 && static_cast<std::int8_t>(value[2]) != subchunk_y) return {};
    if (static_cast<std::uint8_t>(value[1]) == 0) return {Status::Empty};
    value.remove_prefix(header);
    if (value.empty()) return {};

    // See bedrock-level/docs/BedrockLevelFormat.md, sections 2.3 and 2.4.
    const auto storage = static_cast<std::uint8_t>(value[0]);
    const unsigned bits = storage >> 1;
    if ((storage & 1) != 0) return {Status::Unsupported};
    if (bits > 16) return {};
    const unsigned per_word = bits ? 32 / bits : 0;
    const std::size_t packed_size = bits ? ((4096 + per_word - 1) / per_word) * 4 : 0;
    if (value.size() < 1 + packed_size + (bits ? 4 : 0)) return {};
    const auto palette_size = bits ? bl::binary::read_u32_le(value.data() + 1 + packed_size) : 1u;
    if (palette_size == 0 || palette_size > 65536) return {};

    std::uint32_t palette_index = 0;
    if (bits) {
        const unsigned index = 8 * 256 + 8 * 16 + local_y;
        const auto word = bl::binary::read_u32_le(value.data() + 1 + (index / per_word) * 4);
        palette_index = (word >> ((index % per_word) * bits)) & ((1u << bits) - 1);
    }
    if (palette_index >= palette_size) return {};

    PaletteCursor cursor(value.substr(1 + packed_size + (bits ? 4 : 0)));
    std::string_view name;
    for (std::uint32_t entry = 0; entry <= palette_index; ++entry) {
        name = {};
        if (!cursor.blockName(name)) return {};
    }
    const bool air = name == "minecraft:air" || name == "minecraft:cave_air" || name == "minecraft:void_air";
    return {air ? Status::Empty : Status::Found, world_y, name};
}
