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
    /// Classification reads a key, it never keeps it: the scan passes the iterator's
    /// own bytes instead of materialising a string per key.
    [[nodiscard]] inline std::string_view slice_view(const leveldb::Slice& slice) noexcept {
        return std::string_view(slice.data(), slice.size());
    }

    /// The largest chunk coordinate a world can hold, and deliberately far beyond
    /// one: the world border is 30M blocks (1.875M chunks), so this is nine times
    /// it. The bound decides whether a four-byte prefix may be skipped, so making
    /// it too small would silently lose real chunks; being generous only costs a
    /// skipped run or two.
    constexpr std::int32_t kMaxChunkCoordinate = 1 << 24;

    /// The smallest key that sorts after every key starting with `prefix`: the
    /// prefix with its last byte that is not 0xFF incremented. Empty when there is
    /// no such key (the prefix is all 0xFF, i.e. it ends the key space).
    [[nodiscard]] std::string prefixSuccessor(std::string_view prefix) {
        std::string next(prefix);
        while (!next.empty() && static_cast<unsigned char>(next.back()) == 0xFF) next.pop_back();
        if (next.empty()) return {};
        ++next.back();
        return next;
    }

    /// Where a key that cannot be a chunk key should jump to, or empty to step on.
    ///
    /// A chunk key stores its column's x in its first four bytes, so a key whose
    /// first four bytes hold an impossible x cannot be a chunk key -- and neither
    /// can any other key sharing them, because keys with a common prefix are
    /// adjacent in the sorted key space. One Seek then steps over such a run whole,
    /// and LevelDB never reads or inflates the blocks in between.
    ///
    /// This is what skips the actor keys: a world with entities is dominated by
    /// them, they all start with the same non-coordinate four bytes, and nothing in
    /// a chunk-coordinate scan wants them.
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

    // Rebuild from scratch, but keep writes that arrived since open(): they are
    // replayed by finishScan(), which the scan below cannot do for them.
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
    // Always locked, unlike the read paths: during the scan this only appends to
    // the queue, and once the scan is over it is the write path itself. The phase
    // is re-read under the lock because finishScan() switches it while holding it,
    // which is what keeps the append from landing after the replay.
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