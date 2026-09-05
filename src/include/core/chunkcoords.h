#ifndef BEDROCKMAP_CHUNKCOORDS_H
#define BEDROCKMAP_CHUNKCOORDS_H

#include <qimage.h>
#include <qmutex.h>
#include <qrunnable.h>
#include <qthreadpool.h>

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

namespace leveldb {
    class DB;
}

/// Bounding box of all indexed chunks in one dimension, inclusive on both axes.
struct ChunkCoordsBoundingBox {
    bool valid{false};
    int32_t min_x{0};
    int32_t min_z{0};
    int32_t max_x{0};
    int32_t max_z{0};

    void include(const bl::chunk_pos &pos) noexcept {
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

/// A compact configurable chunk grid. The region coordinates are chunk-space
/// region origins, and each instance stores one presence bit per chunk.
class CoordsRegion {
   public:
    static constexpr int32_t SIZE = constant::COORDS_REGION_SIZE;
    static constexpr std::size_t CHUNK_COUNT = static_cast<std::size_t>(SIZE) * SIZE;

    CoordsRegion() = default;
    CoordsRegion(int32_t region_x, int32_t region_z) : region_x_(region_x), region_z_(region_z) {}

    static CoordsRegion fromChunk(const bl::chunk_pos &pos) noexcept { return {floorDiv(pos.x) * SIZE, floorDiv(pos.z) * SIZE}; }

    bool operator==(const CoordsRegion &other) const noexcept { return region_x_ == other.region_x_ && region_z_ == other.region_z_; }

    bool operator!=(const CoordsRegion &other) const noexcept { return !(*this == other); }

    struct Hash {
        size_t operator()(const CoordsRegion &region) const noexcept {
            const auto x = std::hash<int32_t>{}(region.region_x_);
            const auto z = std::hash<int32_t>{}(region.region_z_);
            return x ^ (z << 7);
        }
    };

    bool insertChunk(const bl::chunk_pos &pos) const noexcept {
        const auto region = fromChunk(pos);
        if (*this != region) return false;
        const auto bit = bitIndex(pos);
        const bool wasPresent = chunk_mask_.test(bit);
        chunk_mask_.set(bit);
        image_ = QImage();
        return !wasPresent;
    }

    bool removeChunk(const bl::chunk_pos &pos) const noexcept {
        const auto region = fromChunk(pos);
        if (*this != region) return false;
        const auto bit = bitIndex(pos);
        if (!chunk_mask_.test(bit)) return false;
        chunk_mask_.reset(bit);
        image_ = QImage();
        return true;
    }

    void forEachPresentChunk(const std::function<void(int32_t, int32_t)> &visitor) const {
        for (int x = 0; x < SIZE; ++x) {
            for (int z = 0; z < SIZE; ++z) {
                if (chunk_mask_.test(static_cast<std::size_t>(x * SIZE + z))) visitor(region_x_ + x, region_z_ + z);
            }
        }
    }

    bool containsChunk(const bl::chunk_pos &pos) const noexcept {
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
    QImage image() const { return image_; }

   private:
    static int32_t floorDiv(int32_t value) noexcept {
        const auto quotient = value / SIZE;
        return value % SIZE < 0 ? quotient - 1 : quotient;
    }

    static unsigned bitIndex(const bl::chunk_pos &pos) noexcept {
        const auto region = fromChunk(pos);
        const auto local_x = pos.x - region.region_x_;
        const auto local_z = pos.z - region.region_z_;
        return static_cast<unsigned>(local_x * SIZE + local_z);
    }

    int32_t region_x_{0};
    int32_t region_z_{0};
    mutable std::bitset<CHUNK_COUNT> chunk_mask_;
    mutable QImage image_;
};

/// Stores existing chunk coordinates grouped by dimension and compact region.
class ChunkCoordsIndex {
   public:
    static constexpr int32_t MIN_DIMENSION = -1024;
    static constexpr int32_t MAX_DIMENSION = 1024;
    using RegionSet = std::unordered_set<CoordsRegion, CoordsRegion::Hash>;

    ChunkCoordsIndex() { update_pool_.setMaxThreadCount(1); }

    ~ChunkCoordsIndex() { update_pool_.waitForDone(); }

    static bool validDimension(int32_t dim) noexcept { return dim >= MIN_DIMENSION && dim <= MAX_DIMENSION; }

    /// Scan all LevelDB keys, build the index, and generate region images.
    /// Returns false when the scan is cancelled or fails.
    bool load(leveldb::DB *db, const std::atomic_bool &stop);

    // Switch from the background-build phase to the interactive update phase.
    void beginInteractivePhase() noexcept { interactive_.store(true, std::memory_order_release); }

    bool insert(const bl::chunk_pos &pos) {
        auto lock = lockForInteractive();
        return insertUnlocked(pos);
    }

   private:
    bool insertUnlocked(const bl::chunk_pos &pos) {
        if (!validDimension(pos.dim)) return false;
        auto &regions = regions_by_dimension_[pos.dim];
        auto [it, inserted] = regions.emplace(CoordsRegion::fromChunk(pos));
        (void)inserted;
        bounds_by_dimension_[pos.dim].include(pos);
        return it->insertChunk(pos);
    }

    bool removeUnlocked(const bl::chunk_pos &pos);

   public:
    bool insert(int32_t x, int32_t z, int32_t dim) { return insert(bl::chunk_pos{x, z, dim}); }

    bool remove(const bl::chunk_pos &pos);

    bool updateChunk(const bl::chunk_pos &pos, bool present);

    void enqueueUpdate(const bl::chunk_pos &pos, bool present, std::function<void()> finished = {});

    bool contains(const bl::chunk_pos &pos) const {
        auto lock = lockForInteractive();
        return containsUnlocked(pos);
    }

   private:
    bool containsUnlocked(const bl::chunk_pos &pos) const {
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

    QImage image(const bl::chunk_pos &region_pos) const { return image(region_pos.dim, region_pos.x, region_pos.z); }

    std::optional<ChunkCoordsBoundingBox> boundingBox(int32_t dim) const {
        auto lock = lockForInteractive();
        const auto it = bounds_by_dimension_.find(dim);
        return it == bounds_by_dimension_.end() ? std::nullopt : std::optional<ChunkCoordsBoundingBox>(it->second);
    }

    void generateImages() {
        auto lock = lockForInteractive();
        for (auto &[dim, regions] : regions_by_dimension_) {
            (void)dim;
            for (auto &region : regions) region.generateImage();
        }
    }

    std::size_t chunkCount(int32_t dim) const {
        auto lock = lockForInteractive();
        const auto dim_it = regions_by_dimension_.find(dim);
        if (dim_it == regions_by_dimension_.end()) return 0;
        std::size_t count = 0;
        for (const auto &region : dim_it->second) count += region.chunkCount();
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
        for (const auto &[dim, regions] : regions_by_dimension_) {
            std::size_t count = 0;
            for (const auto &region : regions) count += region.chunkCount();
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
        update_pool_.waitForDone();
        regions_by_dimension_.clear();
        bounds_by_dimension_.clear();
    }

    void waitForTasks() noexcept { update_pool_.waitForDone(); }

   private:
    std::unique_lock<QMutex> lockForInteractive() const {
        std::unique_lock<QMutex> lock(mutex_, std::defer_lock);
        if (interactive_.load(std::memory_order_acquire)) lock.lock();
        return lock;
    }

    void rebuildBoundingBox(int32_t dim);

    std::unordered_map<int32_t, RegionSet> regions_by_dimension_;
    std::unordered_map<int32_t, ChunkCoordsBoundingBox> bounds_by_dimension_;
    mutable QMutex mutex_;
    std::atomic_bool interactive_{false};
    QThreadPool update_pool_;
};

#endif  // BEDROCKMAP_CHUNKCOORDS_H
