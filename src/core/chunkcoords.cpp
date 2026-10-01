#include "chunkcoords.h"

#include <leveldb/db.h>
#include <leveldb/iterator.h>
#include <leveldb/slice.h>

#include <chrono>
#include <cstring>
#include <string>
#include <string_view>

#include "bedrock_level.h"
#include "chunk.h"
#include "loguru/loguru.hpp"
#include "maptile.h"

namespace bl::config {
    bool strict_chunk_existence();
}

namespace {
    /// View key bytes without allocating a string.
    [[nodiscard]] inline std::string_view slice_view(const leveldb::Slice& slice) noexcept {
        return std::string_view(slice.data(), slice.size());
    }

    /// Bound used to distinguish coordinate prefixes from other LevelDB keys.
    constexpr std::int32_t kMaxChunkCoordinate = 1 << 24;

    /// Return the smallest key that sorts after every key with this prefix.
    [[nodiscard]] std::string prefixSuccessor(std::string_view prefix) {
        std::string next(prefix);
        while (!next.empty() && static_cast<unsigned char>(next.back()) == 0xFF) next.pop_back();
        if (next.empty()) return {};
        ++next.back();
        return next;
    }

    /// Return a seek target for a run whose four-byte prefix cannot be a chunk key.
    [[nodiscard]] std::string skipRunTarget(std::string_view key) {
        if (key.size() < 4) return {};
        std::int32_t x = 0;
        std::memcpy(&x, key.data(), sizeof(x));
        if (x <= kMaxChunkCoordinate && x >= -kMaxChunkCoordinate) return {};
        return prefixSuccessor(key.substr(0, 4));
    }
}  // namespace

void CoordsRegion::generateImage() const {
    auto image = MapTile::COORDS_EMPTY_TILE().copy();
    for (int x = 0; x < SIZE; ++x) {
        for (int z = 0; z < SIZE; ++z) {
            if (chunk_mask_.test(static_cast<std::size_t>(x * SIZE + z))) image.setPixel(x, z, 0xffffffffu);
        }
    }
    image_ = std::move(image);
    image_dirty_ = false;
}

bool ChunkCoordsIndex::load(bl::bedrock_level& level, const std::atomic_bool& stop, ProgressCallback progress,
                            ChunkCoordsLoadStats* stats) {
    if (stats) *stats = ChunkCoordsLoadStats{};
    const auto load_start = std::chrono::steady_clock::now();
    const auto elapsed_ms = [](const std::chrono::steady_clock::time_point& start) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };

    auto* db = level.db();
    if (!db) return false;

    // Replay writes that arrived while rebuilding.
    {
        std::lock_guard<QMutex> lock(mutex_);
        regions_by_dimension_.clear();
        bounds_by_dimension_.clear();
        bounds_dirty_.clear();
        interactive_.store(false, std::memory_order_release);
    }
    std::uint64_t scannedKeys = 0;
    std::uint64_t chunkKeys = 0;
    std::uint64_t markerKeys = 0;
    std::uint64_t chunks = 0;
    std::uint64_t runSkips = 0;
    if (progress) progress(0, 0);
    const auto scan_start = std::chrono::steady_clock::now();
    auto* iterator = db->NewIterator(level.bulk_read_options());
    for (iterator->SeekToFirst(); iterator->Valid();) {
        if (stop.load(std::memory_order_acquire)) {
            delete iterator;
            return false;
        }

        ++scannedKeys;
        if (progress && (scannedKeys % (1u << 16) == 0)) progress(scannedKeys, chunks);

        const auto key = slice_view(iterator->key());
        const auto chunk_key = bl::chunk_key::parse(key);
        if (!chunk_key.valid()) {
            if (const auto target = skipRunTarget(key); !target.empty()) {
                iterator->Seek(target);
                ++runSkips;
            } else {
                iterator->Next();
            }
            continue;
        }

        ++chunkKeys;
        for (auto marker : bl::raw_chunk::MARKER_KEYS) {
            if (chunk_key.type == marker && (!bl::config::strict_chunk_existence() || !iterator->value().empty())) {
                ++markerKeys;
                if (insertUnlocked(chunk_key.cp)) ++chunks;
                break;
            }
        }
        iterator->Next();
    }

    const auto status = iterator->status();
    delete iterator;
    if (stats) {
        stats->scanned_keys = scannedKeys;
        stats->chunk_keys = chunkKeys;
        stats->marker_keys = markerKeys;
        stats->unique_chunks = chunks;
        stats->run_skips = runSkips;
        stats->scan_ms = elapsed_ms(scan_start);
    }
    if (!status.ok()) {
        LOG_F(WARNING, "Failed while preloading chunk coordinates: %s", status.ToString().c_str());
        return false;
    }
    if (stop.load(std::memory_order_acquire)) return false;

    if (progress) progress(scannedKeys, chunks);

    std::uint64_t generatedRegions = 0;
    const auto images_start = std::chrono::steady_clock::now();
    for (auto& [dim, regions] : regions_by_dimension_) {
        (void)dim;
        for (auto& region : regions) {
            region.generateImage();
            ++generatedRegions;
        }
    }
    const double images_ms = elapsed_ms(images_start);

    for (const auto& [dim, count] : dimensionCounts()) {
        LOG_F(INFO, "Preloaded chunk coordinates: dimension %d, %zu chunks", dim, count);
    }
    const auto finish_start = std::chrono::steady_clock::now();
    finishScan();
    if (stats) {
        stats->generated_regions = generatedRegions;
        stats->image_generation_ms = images_ms;
        stats->finish_scan_ms = elapsed_ms(finish_start);
        stats->total_ms = elapsed_ms(load_start);
    }
    return true;
}

bool ChunkCoordsIndex::remove(const bl::chunk_pos& pos) {
    auto lock = lockForInteractive();
    if (!lock.owns_lock()) return false;
    return removeUnlocked(pos);
}

bool ChunkCoordsIndex::removeUnlocked(const bl::chunk_pos& pos) {
    if (!validDimension(pos.dim)) return false;
    const auto dim_it = regions_by_dimension_.find(pos.dim);
    if (dim_it == regions_by_dimension_.end()) return false;

    auto region_it = dim_it->second.find(CoordsRegion::fromChunk(pos));
    if (region_it == dim_it->second.end()) return false;
    const bool removed = region_it->removeChunk(pos);
    if (!removed) return false;
    if (region_it->empty()) {
        dim_it->second.erase(region_it);
    }
    bounds_dirty_.insert(pos.dim);
    return true;
}

void ChunkCoordsIndex::updateChunk(const bl::chunk_pos& pos, bool present) {
    // Check the scan phase and queue updates under the same lock as finishScan().
    std::lock_guard<QMutex> lock(mutex_);
    if (!interactive_.load(std::memory_order_acquire)) {
        queued_writes_.emplace_back(pos, present);
        return;
    }
    if (present) {
        insertUnlocked(pos);
    } else {
        removeUnlocked(pos);
    }
}

void ChunkCoordsIndex::finishScan() {
    std::lock_guard<QMutex> lock(mutex_);
    for (const auto& [pos, present] : queued_writes_) {
        if (present) {
            insertUnlocked(pos);
        } else {
            removeUnlocked(pos);
        }
    }
    queued_writes_.clear();
    interactive_.store(true, std::memory_order_release);
}

bool ChunkCoordsIndex::containsAnyChunk(int32_t x, int32_t z, int32_t dim, int32_t size) const {
    auto lock = lockForInteractive();
    if (!validDimension(dim) || size <= 0) return false;
    const auto dim_it = regions_by_dimension_.find(dim);
    if (dim_it == regions_by_dimension_.end()) return false;

    // Region-aligned windows use one lookup; straddling windows fall back to chunks.
    const bl::chunk_pos min{x, z, dim};
    const auto region = CoordsRegion::fromChunk(min);
    const auto region_it = dim_it->second.find(region);
    if (region_it == dim_it->second.end()) return false;
    if (CoordsRegion::fromChunk(bl::chunk_pos{x + size - 1, z + size - 1, dim}) == region) {
        return region_it->containsAnyChunk(min, size);
    }
    for (int32_t dx = 0; dx < size; ++dx) {
        for (int32_t dz = 0; dz < size; ++dz) {
            if (containsUnlocked(bl::chunk_pos{x + dx, z + dz, dim})) return true;
        }
    }
    return false;
}

void ChunkCoordsIndex::rebuildBoundingBox(int32_t dim) const {
    ChunkCoordsBoundingBox bounds;
    const auto dim_it = regions_by_dimension_.find(dim);
    if (dim_it != regions_by_dimension_.end()) {
        for (const auto& region : dim_it->second) {
            region.forEachPresentChunk([&bounds](int32_t x, int32_t z) { bounds.include(bl::chunk_pos{x, z, 0}); });
        }
    }
    if (bounds.valid) {
        bounds_by_dimension_[dim] = bounds;
    } else {
        bounds_by_dimension_.erase(dim);
    }
}
