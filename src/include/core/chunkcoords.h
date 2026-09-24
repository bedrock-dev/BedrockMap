#ifndef BEDROCKMAP_CHUNKCOORDS_H
#define BEDROCKMAP_CHUNKCOORDS_H

#include <qimage.h>
#include <qmutex.h>

#include <algorithm>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bedrock_key.h"
#include "config.h"

namespace bl {
    class bedrock_level;
}

/// Bounding box of all indexed chunks in one dimension, inclusive on both axes.
struct ChunkCoordsBoundingBox {
    bool valid{false};
    int32_t min_x{0};
    int32_t min_z{0};
    int32_t max_x{0};
    int32_t max_z{0};

    void include(const bl::chunk_pos& pos) noexcept {
        if (!valid) {
            min_x = max_x = pos.x;
            min_z = max_z = pos.z;
            valid = true;
            return;
        }
        min_x = std::min(min_x, pos.x);
        min_z = std::min(min_z, pos.z);
        max_x = std::max(max_x, pos.x);
        max_z = std::max(max_z, pos.z);
    }
};

/// Counters and phase timings from one ChunkCoordsIndex::load(). The benchmark
/// reads them to attribute the scan's cost; the map renderer ignores them.
struct ChunkCoordsLoadStats {
    /// Keys the iterator visited.
    std::uint64_t scanned_keys{0};
    /// Visited keys that classified as a chunk key, whatever their type.
    std::uint64_t chunk_keys{0};
    /// The existence markers among those that passed the emptiness rule, i.e.
    /// the keys that could contribute a chunk: unique_chunks <= marker_keys.
    std::uint64_t marker_keys{0};
    /// Chunks the index did not already hold.
    std::uint64_t unique_chunks{0};
    /// Region tiles whose image the scan built.
    std::uint64_t generated_regions{0};
    double scan_ms{0.0};
    double image_generation_ms{0.0};
    double finish_scan_ms{0.0};
    double total_ms{0.0};
};

/// A compact configurable chunk grid. The region coordinates are chunk-space
/// region origins, and each instance stores one presence bit per chunk.
class CoordsRegion {
   public:
    static constexpr int32_t SIZE = constant::COORDS_REGION_SIZE;
    static constexpr std::size_t CHUNK_COUNT = static_cast<std::size_t>(SIZE) * SIZE;

    CoordsRegion() = default;
    CoordsRegion(int32_t region_x, int32_t region_z) : region_x_(region_x), region_z_(region_z) {}

    static CoordsRegion fromChunk(const bl::chunk_pos& pos) noexcept { return {floorDiv(pos.x) * SIZE, floorDiv(pos.z) * SIZE}; }

    bool operator==(const CoordsRegion& other) const noexcept { return region_x_ == other.region_x_ && region_z_ == other.region_z_; }

    bool operator!=(const CoordsRegion& other) const noexcept { return !(*this == other); }

    struct Hash {
        size_t operator()(const CoordsRegion& region) const noexcept {
            const auto x = std::hash<int32_t>{}(region.region_x_);
            const auto z = std::hash<int32_t>{}(region.region_z_);
            return x ^ (z << 7);
        }
    };

    bool insertChunk(const bl::chunk_pos& pos) const noexcept {
        const auto region = fromChunk(pos);
        if (*this != region) return false;
        const auto bit = bitIndex(pos);
        const bool wasPresent = chunk_mask_.test(bit);
        chunk_mask_.set(bit);
        image_dirty_ = true;
        return !wasPresent;
    }

    bool removeChunk(const bl::chunk_pos& pos) const noexcept {
        const auto region = fromChunk(pos);
        if (*this != region) return false;
        const auto bit = bitIndex(pos);
        if (!chunk_mask_.test(bit)) return false;
        chunk_mask_.reset(bit);
        image_dirty_ = true;
        return true;
    }

    void forEachPresentChunk(const std::function<void(int32_t, int32_t)>& visitor) const {
        for (int x = 0; x < SIZE; ++x) {
            for (int z = 0; z < SIZE; ++z) {
                if (chunk_mask_.test(static_cast<std::size_t>(x * SIZE + z))) visitor(region_x_ + x, region_z_ + z);
            }
        }
    }

    bool containsChunk(const bl::chunk_pos& pos) const noexcept {
        const auto region = fromChunk(pos);
        if (*this != region) return false;
        return chunk_mask_.test(bitIndex(pos));
    }

    int32_t x() const noexcept { return region_x_; }
    int32_t z() const noexcept { return region_z_; }

    std::size_t chunkCount() const noexcept { return chunk_mask_.count(); }

    bool empty() const noexcept { return chunk_mask_.none(); }

    void generateImage() const;

    // Return an implicit-shared snapshot so callers never retain a pointer into
    // the mutable index.
    QImage image() const {
        // An edit only sets a bit, so the (128x128) image is rebuilt when
        // something actually draws this region, not once per edited chunk.
        if (image_dirty_) generateImage();
        return image_;
    }

   private:
    static int32_t floorDiv(int32_t value) noexcept {
        const auto quotient = value / SIZE;
        return value % SIZE < 0 ? quotient - 1 : quotient;
    }

    static unsigned bitIndex(const bl::chunk_pos& pos) noexcept {
        const auto region = fromChunk(pos);
        const auto local_x = pos.x - region.region_x_;
        const auto local_z = pos.z - region.region_z_;
        return static_cast<unsigned>(local_x * SIZE + local_z);
    }

    int32_t region_x_{0};
    int32_t region_z_{0};
    mutable std::bitset<CHUNK_COUNT> chunk_mask_;
    mutable QImage image_;
    mutable bool image_dirty_{true};
};

/// Stores existing chunk coordinates grouped by dimension and compact region.
/// The index is a view of the archive: every chunk write goes through
/// updateChunk() on the writing thread, so it never lags behind the edits.
class ChunkCoordsIndex {
   public:
    static constexpr int32_t MIN_DIMENSION = -1024;
    static constexpr int32_t MAX_DIMENSION = 1024;
    using RegionSet = std::unordered_set<CoordsRegion, CoordsRegion::Hash>;

    static bool validDimension(int32_t dim) noexcept { return dim >= MIN_DIMENSION && dim <= MAX_DIMENSION; }

    using ProgressCallback = std::function<void(std::uint64_t scannedKeys, std::uint64_t chunks)>;

    /// Scan all LevelDB keys, build the index, and generate region images.
    /// Returns false when the scan is cancelled or fails. Progress is reported
    /// periodically from the scanning worker and is therefore only advisory.
    /// Chunk writes made while this runs are held and replayed at the end, so the
    /// index is correct for a level that is edited during its initial scan too.
    /// `stats` receives the counters and phase timings; it is optional.
    bool load(bl::bedrock_level& level, const std::atomic_bool& stop, ProgressCallback progress = {},
              ChunkCoordsLoadStats* stats = nullptr);

    /// Leave the scan phase: replay the writes that arrived during it and let
    /// later ones apply directly. Both parts run in one critical section, so no
    /// write can slip between the replay and the switch.
    void finishScan();

    bool insert(const bl::chunk_pos& pos) {
        auto lock = lockForInteractive();
        return insertUnlocked(pos);
    }

   private:
    bool insertUnlocked(const bl::chunk_pos& pos) {
        if (!validDimension(pos.dim)) return false;
        auto& regions = regions_by_dimension_[pos.dim];
        auto [it, inserted] = regions.emplace(CoordsRegion::fromChunk(pos));
        (void)inserted;
        bounds_by_dimension_[pos.dim].include(pos);
        return it->insertChunk(pos);
    }

    bool removeUnlocked(const bl::chunk_pos& pos);

   public:
    bool insert(int32_t x, int32_t z, int32_t dim) { return insert(bl::chunk_pos{x, z, dim}); }

    bool remove(const bl::chunk_pos& pos);

    /// Mirror one chunk write into the index, called on the editing thread so a
    /// caller that has performed an edit sees it reflected here immediately.
    void updateChunk(const bl::chunk_pos& pos, bool present);

    bool contains(const bl::chunk_pos& pos) const {
        auto lock = lockForInteractive();
        return containsUnlocked(pos);
    }

   private:
    bool containsUnlocked(const bl::chunk_pos& pos) const {
        if (!validDimension(pos.dim)) return false;
        const auto dim_it = regions_by_dimension_.find(pos.dim);
        if (dim_it == regions_by_dimension_.end()) return false;
        const auto region_it = dim_it->second.find(CoordsRegion::fromChunk(pos));
        return region_it != dim_it->second.end() && region_it->containsChunk(pos);
    }

   public:
    bool contains(int32_t x, int32_t z, int32_t dim) const { return contains(bl::chunk_pos{x, z, dim}); }

    bool containsRegion(int32_t dim, int32_t region_x, int32_t region_z) const {
        auto lock = lockForInteractive();
        if (!validDimension(dim)) return false;
        const auto dim_it = regions_by_dimension_.find(dim);
        return dim_it != regions_by_dimension_.end() && dim_it->second.find(CoordsRegion(region_x, region_z)) != dim_it->second.end();
    }

    QImage image(int32_t dim, int32_t region_x, int32_t region_z) const {
        auto lock = lockForInteractive();
        if (!validDimension(dim)) return {};
        const auto dim_it = regions_by_dimension_.find(dim);
        if (dim_it == regions_by_dimension_.end()) return {};
        const auto region_it = dim_it->second.find(CoordsRegion(region_x, region_z));
        return region_it == dim_it->second.end() ? QImage{} : region_it->image();
    }

    QImage image(const bl::chunk_pos& region_pos) const { return image(region_pos.dim, region_pos.x, region_pos.z); }

    std::optional<ChunkCoordsBoundingBox> boundingBox(int32_t dim) const {
        auto lock = lockForInteractive();
        // A removal can shrink the box, which cannot be derived incrementally, so
        // the recompute is deferred to the first read instead of every edit.
        if (bounds_dirty_.erase(dim) > 0) rebuildBoundingBox(dim);
        const auto it = bounds_by_dimension_.find(dim);
        return it == bounds_by_dimension_.end() ? std::nullopt : std::optional<ChunkCoordsBoundingBox>(it->second);
    }

    void generateImages() {
        auto lock = lockForInteractive();
        for (auto& [dim, regions] : regions_by_dimension_) {
            (void)dim;
            for (auto& region : regions) region.generateImage();
        }
    }

    std::size_t chunkCount(int32_t dim) const {
        auto lock = lockForInteractive();
        const auto dim_it = regions_by_dimension_.find(dim);
        if (dim_it == regions_by_dimension_.end()) return 0;
        std::size_t count = 0;
        for (const auto& region : dim_it->second) count += region.chunkCount();
        return count;
    }

    std::size_t regionCount(int32_t dim) const {
        auto lock = lockForInteractive();
        const auto dim_it = regions_by_dimension_.find(dim);
        return dim_it == regions_by_dimension_.end() ? 0 : dim_it->second.size();
    }

    std::vector<std::pair<int32_t, std::size_t>> dimensionCounts() const {
        auto lock = lockForInteractive();
        std::vector<std::pair<int32_t, std::size_t>> counts;
        counts.reserve(regions_by_dimension_.size());
        for (const auto& [dim, regions] : regions_by_dimension_) {
            std::size_t count = 0;
            for (const auto& region : regions) count += region.chunkCount();
            counts.emplace_back(dim, count);
        }
        std::sort(counts.begin(), counts.end());
        return counts;
    }

    bool empty() const {
        auto lock = lockForInteractive();
        return regions_by_dimension_.empty();
    }

    void clear() noexcept {
        interactive_.store(false, std::memory_order_release);
        regions_by_dimension_.clear();
        bounds_by_dimension_.clear();
        bounds_dirty_.clear();
        queued_writes_.clear();
    }

   private:
    std::unique_lock<QMutex> lockForInteractive() const {
        std::unique_lock<QMutex> lock(mutex_, std::defer_lock);
        if (interactive_.load(std::memory_order_acquire)) lock.lock();
        return lock;
    }

    void rebuildBoundingBox(int32_t dim) const;

    std::unordered_map<int32_t, RegionSet> regions_by_dimension_;
    mutable std::unordered_map<int32_t, ChunkCoordsBoundingBox> bounds_by_dimension_;
    mutable std::unordered_set<int32_t> bounds_dirty_;
    /// Writes that arrived while the scan was rebuilding the index; the scan
    /// cannot contain them and would overwrite them, so they are replayed at the
    /// end instead of being applied immediately.
    std::vector<std::pair<bl::chunk_pos, bool>> queued_writes_;
    mutable QMutex mutex_;
    std::atomic_bool interactive_{false};
};

#endif  // BEDROCKMAP_CHUNKCOORDS_H
