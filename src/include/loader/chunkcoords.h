#ifndef BEDROCKMAP_CHUNKCOORDS_H
#define BEDROCKMAP_CHUNKCOORDS_H

#include <qimage.h>
#include <qmutex.h>

#include <QRect>
#include <QSize>
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

#include "base/config.h"
#include "bedrock_key.h"

namespace bl {
    class bedrock_level;
    class raw_chunk;
}  // namespace bl

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
    /// Number of keys visited by the iterator, excluding skipped runs.
    std::uint64_t scanned_keys{0};
    /// Visited keys that classified as a chunk key, whatever their type.
    std::uint64_t chunk_keys{0};
    /// The existence markers among those that passed the emptiness rule, i.e.
    /// the keys that could contribute a chunk: unique_chunks <= marker_keys.
    std::uint64_t marker_keys{0};
    /// Chunks the index did not already hold.
    std::uint64_t unique_chunks{0};
    /// Seeks that stepped over a run of keys no chunk key could start with.
    std::uint64_t run_skips{0};
    double scan_ms{0.0};
    double finish_scan_ms{0.0};
    double index_ready_ms{0.0};
    double sampling_drain_ms{0.0};
    double sampling_busy_ms{0.0};
    double sampling_queue_wait_ms{0.0};
    std::uint64_t sampling_chunks{0};
    std::uint64_t sampled_blocks{0};
    std::uint64_t sampling_empty{0};
    std::uint64_t sampling_missing{0};
    std::uint64_t sampling_missing_height{0};
    std::uint64_t sampling_unsupported{0};
    std::uint64_t sampling_malformed{0};
    std::uint64_t sampling_subchunks{0};
    std::uint64_t sampling_copied_bytes{0};
    std::uint64_t sampling_peak_queue_bytes{0};
    std::uint64_t sampling_checksum{0};
    double total_ms{0.0};
};

struct ChunkCoordsLoadOptions {
    /// Sample one block per chunk on a second worker and store overview colors.
    bool build_overview{false};
    std::size_t sampling_queue_bytes{64 * 1024 * 1024};
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
        return !wasPresent;
    }

    bool removeChunk(const bl::chunk_pos& pos) const noexcept {
        const auto region = fromChunk(pos);
        if (*this != region) return false;
        const auto bit = bitIndex(pos);
        if (!chunk_mask_.test(bit)) return false;
        chunk_mask_.reset(bit);
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

    /// True when any chunk in an in-region square is set.
    [[nodiscard]] bool containsAnyChunk(const bl::chunk_pos& min, int32_t size) const noexcept {
        const int32_t x0 = min.x - region_x_;
        const int32_t z0 = min.z - region_z_;
        for (int32_t dx = 0; dx < size; ++dx) {
            for (int32_t dz = 0; dz < size; ++dz) {
                if (chunk_mask_.test(static_cast<std::size_t>((x0 + dx) * SIZE + z0 + dz))) return true;
            }
        }
        return false;
    }

    int32_t x() const noexcept { return region_x_; }
    int32_t z() const noexcept { return region_z_; }

    std::size_t chunkCount() const noexcept { return chunk_mask_.count(); }

    bool empty() const noexcept { return chunk_mask_.none(); }

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
};

/// Stores existing chunk coordinates grouped by dimension and compact region.
class ChunkCoordsIndex {
   public:
    static constexpr int32_t MIN_DIMENSION = -1024;
    static constexpr int32_t MAX_DIMENSION = 1024;
    using RegionSet = std::unordered_set<CoordsRegion, CoordsRegion::Hash>;

    static bool validDimension(int32_t dim) noexcept { return dim >= MIN_DIMENSION && dim <= MAX_DIMENSION; }

    using ProgressCallback = std::function<void(std::uint64_t scannedKeys, std::uint64_t chunks)>;

    /// Scan LevelDB keys, build the index, and generate region images.
    /// Writes arriving during the scan are replayed before interactive mode.
    bool load(bl::bedrock_level& level, const std::atomic_bool& stop, ProgressCallback progress = {}, ChunkCoordsLoadStats* stats = nullptr,
              const ChunkCoordsLoadOptions& options = {});
    /// Rasterize the sampled pages on a worker; coordinates are in chunks.
    [[nodiscard]] QImage overviewImage(int dimension, const QRect& chunks, const QSize& pixels) const;
    [[nodiscard]] std::vector<bl::chunk_pos> terrainRegions(int dimension, const QRect& regions) const;
    void updateOverview(const bl::chunk_pos& pos, QRgb color, bool edited = true);
    void updateOverview(const bl::raw_chunk& raw);
    [[nodiscard]] quint64 overviewRevision() const { return overview_revision_.load(std::memory_order_acquire); }

    /// Replay queued writes and enter interactive mode atomically.
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

    /// True when any chunk in the indexed square is present.
    [[nodiscard]] bool containsAnyChunk(int32_t x, int32_t z, int32_t dim, int32_t size) const;
    std::optional<ChunkCoordsBoundingBox> boundingBox(int32_t dim) const {
        auto lock = lockForInteractive();
        // A removal can shrink the box, which cannot be derived incrementally, so
        // the recompute is deferred to the first read instead of every edit.
        if (bounds_dirty_.erase(dim) > 0) rebuildBoundingBox(dim);
        const auto it = bounds_by_dimension_.find(dim);
        return it == bounds_by_dimension_.end() ? std::nullopt : std::optional<ChunkCoordsBoundingBox>(it->second);
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
        {
            std::lock_guard<QMutex> lock(overview_mutex_);
            overview_pages_.clear();
            overview_writes_.clear();
            overview_scanning_ = false;
            overview_revision_.fetch_add(1, std::memory_order_release);
        }
        interactive_.store(false, std::memory_order_release);
        regions_by_dimension_.clear();
        bounds_by_dimension_.clear();
        bounds_dirty_.clear();
        queued_writes_.clear();
    }

   private:
    void setOverviewPixel(const bl::chunk_pos& pos, QRgb color);
    mutable QMutex overview_mutex_;
    using OverviewPages = std::unordered_map<CoordsRegion, QImage, CoordsRegion::Hash>;
    std::unordered_map<int, OverviewPages> overview_pages_;
    std::vector<std::pair<bl::chunk_pos, QRgb>> overview_writes_;
    bool overview_scanning_{false};
    std::atomic<quint64> overview_revision_{0};
    std::unique_lock<QMutex> lockForInteractive() const {
        std::unique_lock<QMutex> lock(mutex_, std::defer_lock);
        if (interactive_.load(std::memory_order_acquire)) lock.lock();
        return lock;
    }

    void rebuildBoundingBox(int32_t dim) const;

    std::unordered_map<int32_t, RegionSet> regions_by_dimension_;
    mutable std::unordered_map<int32_t, ChunkCoordsBoundingBox> bounds_by_dimension_;
    mutable std::unordered_set<int32_t> bounds_dirty_;
    /// Writes queued while the index is rebuilding.
    std::vector<std::pair<bl::chunk_pos, bool>> queued_writes_;
    mutable QMutex mutex_;
    std::atomic_bool interactive_{false};
};

#endif  // BEDROCKMAP_CHUNKCOORDS_H
