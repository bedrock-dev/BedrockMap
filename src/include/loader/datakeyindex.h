#ifndef BEDROCKMAP_DATAKEYINDEX_H
#define BEDROCKMAP_DATAKEYINDEX_H

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "geometry.h"

namespace bl {
    class bedrock_level;
}

enum class DataKeyCategory : std::uint8_t {
    Chunks,
    Villages,
    Players,
    MapItems,
    Structures,
    RealmsStoriesData,
    TickingAreas,
    Actors,
    Digp,
    Others,
    Unknown
};

/// Metadata for one LevelDB key. Values are deliberately not retained here;
/// the data manager reads them only after the user selects a leaf node.
struct DataKeyEntry {
    std::uint64_t key_offset{0};
    std::uint32_t key_size{0};
    std::size_t parent_index{std::numeric_limits<std::size_t>::max()};
    DataKeyCategory category{DataKeyCategory::Unknown};
    std::int32_t dimension{0};
    std::int32_t group_x{0};
    std::int32_t group_z{0};
    bool has_dimension{false};
    bool value_empty{false};
    bool raw_value{false};
    bool nbt_value{true};

    [[nodiscard]] bool hasParent() const noexcept { return parent_index != std::numeric_limits<std::size_t>::max(); }
};

/// Full-key index used by the archive data manager. This is separate from the
/// chunk-coordinate index because it retains every key, including global keys.
class DataKeyIndex {
   public:
    using ProgressCallback = std::function<void(std::uint64_t scannedKeys, std::uint64_t indexedKeys)>;

    bool load(bl::bedrock_level& level, const std::atomic_bool& stop, ProgressCallback progress = {});

    void clear() noexcept {
        std::vector<DataKeyEntry>().swap(entries_);
        std::string().swap(key_storage_);
        category_counts_.fill(0);
        std::vector<bl::chunk_pos>().swap(chunk_groups_with_main_keys_);
        for (auto& dimensions : category_dimensions_) dimensions.clear();
    }

    [[nodiscard]] const std::vector<DataKeyEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::string_view keyForEntry(std::size_t entry_index) const noexcept {
        if (entry_index >= entries_.size()) return {};
        const auto& entry = entries_[entry_index];
        return std::string_view(key_storage_.data() + entry.key_offset, entry.key_size);
    }
    [[nodiscard]] std::string labelForEntry(std::size_t entry_index) const;
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::uint64_t categoryCount(DataKeyCategory category) const noexcept {
        if (category == DataKeyCategory::Chunks) {
            return category_counts_[static_cast<std::size_t>(DataKeyCategory::Chunks)] +
                   category_counts_[static_cast<std::size_t>(DataKeyCategory::Digp)];
        }
        return category_counts_[static_cast<std::size_t>(category)];
    }
    [[nodiscard]] const std::vector<std::int32_t>& categoryDimensions(DataKeyCategory category) const noexcept {
        return category_dimensions_[static_cast<std::size_t>(category)];
    }
    [[nodiscard]] bool chunkGroupHasMainKey(std::int32_t dimension, std::int32_t x, std::int32_t z) const;

   private:
    std::vector<DataKeyEntry> entries_;
    std::string key_storage_;
    std::array<std::uint64_t, 11> category_counts_{};
    std::array<std::vector<std::int32_t>, 11> category_dimensions_{};
    std::vector<bl::chunk_pos> chunk_groups_with_main_keys_;
};

#endif  // BEDROCKMAP_DATAKEYINDEX_H
