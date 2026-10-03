#ifndef BEDROCKMAP_DATAKEYINDEX_H
#define BEDROCKMAP_DATAKEYINDEX_H

#include <atomic>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

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
    std::string key;
    std::string label;
    std::string group;
    DataKeyCategory category{DataKeyCategory::Unknown};
    std::int32_t dimension{0};
    bool has_dimension{false};
    bool value_empty{false};
};

/// Full-key index used by the archive data manager. This is separate from the
/// chunk-coordinate index because it retains every key, including global keys.
class DataKeyIndex {
   public:
    using ProgressCallback = std::function<void(std::uint64_t scannedKeys, std::uint64_t indexedKeys)>;

    bool load(bl::bedrock_level& level, const std::atomic_bool& stop, ProgressCallback progress = {});

    void clear() noexcept {
        entries_.clear();
        category_counts_.fill(0);
        chunk_groups_with_main_keys_.clear();
        for (auto& dimensions : category_dimensions_) dimensions.clear();
    }

    [[nodiscard]] const std::vector<DataKeyEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::uint64_t categoryCount(DataKeyCategory category) const noexcept {
        return category_counts_[static_cast<std::size_t>(category)];
    }
    [[nodiscard]] const std::vector<std::int32_t>& categoryDimensions(DataKeyCategory category) const noexcept {
        return category_dimensions_[static_cast<std::size_t>(category)];
    }
    [[nodiscard]] bool chunkGroupHasMainKey(std::int32_t dimension, const std::string& group) const;

   private:
    std::vector<DataKeyEntry> entries_;
    std::array<std::uint64_t, 11> category_counts_{};
    std::array<std::vector<std::int32_t>, 11> category_dimensions_{};
    std::unordered_set<std::string> chunk_groups_with_main_keys_;
};

#endif  // BEDROCKMAP_DATAKEYINDEX_H
