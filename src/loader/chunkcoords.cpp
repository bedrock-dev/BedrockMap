#include "loader/chunkcoords.h"

#include <leveldb/db.h>
#include <leveldb/iterator.h>
#include <leveldb/slice.h>

#include <QPainter>
#include <QRunnable>
#include <QThreadPool>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <memory>
#include <string>
#include <string_view>

#include "bedrock_level.h"
#include "binary_io.h"
#include "chunk.h"
#include "color.h"
#include "loader/chunkcoordsblocks.h"
#include "loguru/loguru.hpp"

namespace bl::config {
    bool strict_chunk_existence();
}

namespace {
    /// View key bytes without allocating a string.
    [[nodiscard]] inline std::string_view slice_view(const leveldb::Slice& slice) noexcept {
        return std::string_view(slice.data(), slice.size());
    }

    /// Bound used to distinguish coordinate prefixes from other LevelDB keys.
    constexpr std::int32_t MAX_CHUNK_COORDINATE = 1 << 24;

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
        if (x <= MAX_CHUNK_COORDINATE && x >= -MAX_CHUNK_COORDINATE) return {};
        return prefixSuccessor(key.substr(0, 4));
    }

    using SamplingClock = std::chrono::steady_clock;

    QRgb overviewColor(const chunkcoords_blocks::Sample& sample) {
        if (sample.status == chunkcoords_blocks::Status::Empty) return 0;
        if (sample.status != chunkcoords_blocks::Status::Found) return qRgb(96, 96, 96);
        const auto base = bl::get_block_color(sample.name);
        const auto tinted = bl::blend_color_with_biome(sample.name, base, bl::biome::plains);
        return qRgb(tinted.r, tinted.g, tinted.b);
    }

    class BlockSamplingPipeline {
       public:
        using SampleCallback = std::function<void(const bl::chunk_pos&, const chunkcoords_blocks::Sample&)>;
        BlockSamplingPipeline(const std::atomic_bool& stop, std::size_t queue_bytes, SampleCallback callback = {})
            : stop_(stop), queue_limit_(std::max<std::size_t>(queue_bytes, 1)), callback_(std::move(callback)) {
            pool_.setMaxThreadCount(1);
            pool_.start(QRunnable::create([this] { consume(); }));
        }

        ~BlockSamplingPipeline() {
            {
                std::lock_guard lock(mutex_);
                aborted_ = true;
            }
            ready_.notify_all();
            space_.notify_all();
            pool_.waitForDone();
        }

        bool visit(const bl::chunk_key& key, leveldb::Iterator& iterator, bool marker) {
            // Dimensions can interleave in key order. Flush only after the
            // shared (x,z) prefix changes, keeping one pending job per dimension.
            if (position_ && (position_->x != key.cp.x || position_->z != key.cp.z)) {
                if (!flush()) return false;
            }
            position_ = key.cp;
            if (!ChunkCoordsIndex::validDimension(key.cp.dim)) return true;
            if (!marker && key.type != bl::chunk_key::Data3D && key.type != bl::chunk_key::Data2D &&
                key.type != bl::chunk_key::Data2DLegacy && key.type != bl::chunk_key::SubChunkTerrain)
                return true;
            auto& job = pending_[key.cp.dim];
            job.pos = key.cp;
            job.marker = job.marker || marker;

            if (key.type == bl::chunk_key::Data3D || key.type == bl::chunk_key::Data2D || key.type == bl::chunk_key::Data2DLegacy) {
                if (job.height && key.type != bl::chunk_key::Data3D) return true;
                const auto value = iterator.value();
                if (value.size() < 512) return true;
                const int height = bl::binary::read_i16_le(value.data() + (8 + 8 * 16) * 2);
                // On-disk height maps point one block above the surface.
                job.height = height - 1 + (key.type == bl::chunk_key::Data3D ? bl::dimension_min_y(key.cp.dim) : 0);
                // Zero means no blocks above the dimension floor; negative
                // sentinels used by older/editor-generated chunks are empty too.
                job.empty = height <= 0;
            } else if (key.type == bl::chunk_key::SubChunkTerrain && job.height && !job.empty) {
                const int local_y = ((*job.height % 16) + 16) % 16;
                if (key.y_index == (*job.height - local_y) / 16) {
                    const auto value = iterator.value();
                    job.value.assign(value.data(), value.size());
                    copied_bytes_ += value.size();
                }
            }
            return true;
        }

        bool endInput() {
            if (!flush()) return false;
            {
                std::lock_guard lock(mutex_);
                done_ = true;
            }
            ready_.notify_one();
            return true;
        }

        bool finish(ChunkCoordsLoadStats* stats) {
            pool_.waitForDone();
            if (error_) std::rethrow_exception(error_);
            if (stats) {
                stats->sampling_chunks = counters_.sampling_chunks;
                stats->sampled_blocks = counters_.sampled_blocks;
                stats->sampling_empty = counters_.sampling_empty;
                stats->sampling_missing = counters_.sampling_missing;
                stats->sampling_missing_height = counters_.sampling_missing_height;
                stats->sampling_unsupported = counters_.sampling_unsupported;
                stats->sampling_malformed = counters_.sampling_malformed;
                stats->sampling_subchunks = counters_.sampling_subchunks;
                stats->sampling_checksum = counters_.sampling_checksum;
                stats->sampling_busy_ms = counters_.sampling_busy_ms;
                stats->sampling_copied_bytes = copied_bytes_;
                stats->sampling_peak_queue_bytes = peak_bytes_;
                stats->sampling_queue_wait_ms = queue_wait_ms_;
            }
            return !stop_.load(std::memory_order_acquire);
        }

       private:
        struct Job {
            bl::chunk_pos pos;
            std::optional<int> height;
            bool marker{false};
            bool empty{false};
            std::string value;
        };

        bool flush() {
            for (auto& [dimension, job] : pending_) {
                (void)dimension;
                if (!job.marker) continue;
                std::unique_lock lock(mutex_);
                // Bound both raw bytes and metadata. One oversize value may
                // enter an empty queue so a small configured limit cannot deadlock.
                const auto full = [&] {
                    return !queue_.empty() &&
                           (queue_.size() >= 1024 || job.value.size() > queue_limit_ - std::min(queue_bytes_, queue_limit_));
                };
                if (full()) {
                    const auto start = SamplingClock::now();
                    while (full() && !aborted_ && !stop_.load(std::memory_order_acquire))
                        space_.wait_for(lock, std::chrono::milliseconds(20));
                    queue_wait_ms_ += std::chrono::duration<double, std::milli>(SamplingClock::now() - start).count();
                }
                if (aborted_ || stop_.load(std::memory_order_acquire)) return false;
                queue_bytes_ += job.value.size();
                peak_bytes_ = std::max(peak_bytes_, queue_bytes_);
                queue_.push_back(std::move(job));
                lock.unlock();
                ready_.notify_one();
            }
            pending_.clear();
            return true;
        }

        void sample(const Job& job) {
            ++counters_.sampling_chunks;
            if (job.empty) {
                ++counters_.sampling_empty;
                if (callback_) callback_(job.pos, {chunkcoords_blocks::Status::Empty});
                return;
            }
            if (!job.height || job.value.empty()) {
                ++counters_.sampling_missing;
                if (!job.height) ++counters_.sampling_missing_height;
                if (callback_) callback_(job.pos, {});
                return;
            }
            ++counters_.sampling_subchunks;
            const auto result = chunkcoords_blocks::sampleBlock(job.value, *job.height);
            if (callback_) callback_(job.pos, result);
            switch (result.status) {
                case chunkcoords_blocks::Status::Found:
                    ++counters_.sampled_blocks;
                    break;
                case chunkcoords_blocks::Status::Empty:
                    ++counters_.sampling_empty;
                    break;
                case chunkcoords_blocks::Status::Unsupported:
                    ++counters_.sampling_unsupported;
                    break;
                case chunkcoords_blocks::Status::Malformed:
                    ++counters_.sampling_malformed;
                    break;
            }
            // Hash the selected name, position and height independently of task ordering.
            std::uint64_t hash = 14695981039346656037ull;
            for (const unsigned char c : result.name) hash = (hash ^ c) * 1099511628211ull;
            for (const int v : {job.pos.x, job.pos.z, job.pos.dim, result.y})
                hash = (hash ^ static_cast<std::uint32_t>(v)) * 1099511628211ull;
            counters_.sampling_checksum ^= hash;
        }

        void consume() noexcept {
            try {
                for (;;) {
                    Job job;
                    {
                        std::unique_lock lock(mutex_);
                        while (queue_.empty() && !done_ && !aborted_ && !stop_.load(std::memory_order_acquire))
                            ready_.wait_for(lock, std::chrono::milliseconds(20));
                        if (aborted_ || stop_.load(std::memory_order_acquire) || (done_ && queue_.empty())) return;
                        job = std::move(queue_.front());
                        queue_.pop_front();
                        queue_bytes_ -= job.value.size();
                    }
                    space_.notify_one();
                    const auto start = SamplingClock::now();
                    sample(job);
                    counters_.sampling_busy_ms += std::chrono::duration<double, std::milli>(SamplingClock::now() - start).count();
                }
            } catch (...) {
                {
                    std::lock_guard lock(mutex_);
                    error_ = std::current_exception();
                    aborted_ = true;
                }
                space_.notify_all();
            }
        }

        const std::atomic_bool& stop_;
        const std::size_t queue_limit_;
        SampleCallback callback_;
        QThreadPool pool_;
        std::mutex mutex_;
        std::condition_variable ready_;
        std::condition_variable space_;
        std::deque<Job> queue_;
        std::unordered_map<int, Job> pending_;
        std::optional<bl::chunk_pos> position_;
        std::size_t queue_bytes_{0};
        std::size_t peak_bytes_{0};
        std::uint64_t copied_bytes_{0};
        double queue_wait_ms_{0.0};
        bool done_{false};
        bool aborted_{false};
        std::exception_ptr error_;
        ChunkCoordsLoadStats counters_;
    };
}  // namespace

bool ChunkCoordsIndex::load(bl::bedrock_level& level, const std::atomic_bool& stop, ProgressCallback progress, ChunkCoordsLoadStats* stats,
                            const ChunkCoordsLoadOptions& options) {
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
    std::unique_ptr<BlockSamplingPipeline> sampler;
    if (options.build_overview) {
        {
            std::lock_guard<QMutex> lock(overview_mutex_);
            overview_pages_.clear();
            overview_writes_.clear();
            overview_scanning_ = true;
        }
        auto sampleCallback = [this](const bl::chunk_pos& pos, const chunkcoords_blocks::Sample& sample) {
            updateOverview(pos, overviewColor(sample), false);
        };
        sampler = std::make_unique<BlockSamplingPipeline>(stop, options.sampling_queue_bytes, std::move(sampleCallback));
    }
    if (progress) progress(0, 0);
    const auto scan_start = std::chrono::steady_clock::now();
    std::unique_ptr<leveldb::Iterator> iterator(db->NewIterator(level.bulk_read_options()));
    for (iterator->SeekToFirst(); iterator->Valid();) {
        if (stop.load(std::memory_order_acquire)) {
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
        bool markerPresent = false;
        for (auto marker : bl::raw_chunk::MARKER_KEYS) {
            if (chunk_key.type == marker && (!bl::config::strict_chunk_existence() || !iterator->value().empty())) {
                ++markerKeys;
                if (insertUnlocked(chunk_key.cp)) ++chunks;
                markerPresent = true;
                break;
            }
        }
        if (sampler && !sampler->visit(chunk_key, *iterator, markerPresent)) return false;
        iterator->Next();
    }

    const auto status = iterator->status();
    iterator.reset();
    if (sampler && !sampler->endInput()) return false;
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

    for (const auto& [dim, count] : dimensionCounts()) {
        LOG_F(INFO, "Preloaded chunk coordinates: dimension %d, %llu chunks", dim, static_cast<unsigned long long>(count));
    }
    const auto finish_start = std::chrono::steady_clock::now();
    finishScan();
    if (stats) {
        stats->finish_scan_ms = elapsed_ms(finish_start);
        stats->index_ready_ms = elapsed_ms(load_start);
    }
    const auto drain_start = std::chrono::steady_clock::now();
    if (sampler && !sampler->finish(stats)) return false;
    if (options.build_overview) {
        std::lock_guard<QMutex> lock(overview_mutex_);
        for (const auto& [pos, color] : overview_writes_) setOverviewPixel(pos, color);
        overview_writes_.clear();
        overview_scanning_ = false;
        overview_revision_.fetch_add(1, std::memory_order_release);
    }
    if (stats) {
        stats->sampling_drain_ms = sampler ? elapsed_ms(drain_start) : 0.0;
        stats->total_ms = elapsed_ms(load_start);
    }
    return true;
}

void ChunkCoordsIndex::setOverviewPixel(const bl::chunk_pos& pos, QRgb color) {
    const auto region = CoordsRegion::fromChunk(pos);
    auto& image = overview_pages_[pos.dim][region];
    if (image.isNull()) {
        image = QImage(CoordsRegion::SIZE, CoordsRegion::SIZE, QImage::Format_ARGB32);
        image.fill(0);
    }
    image.setPixel(pos.x - region.x(), pos.z - region.z(), color);
}

void ChunkCoordsIndex::updateOverview(const bl::chunk_pos& pos, QRgb color, bool edited) {
    std::lock_guard<QMutex> lock(overview_mutex_);
    if (edited && overview_scanning_) overview_writes_.emplace_back(pos, color);
    setOverviewPixel(pos, color);
    if (edited) overview_revision_.fetch_add(1, std::memory_order_release);
}

void ChunkCoordsIndex::updateOverview(const bl::raw_chunk& raw) {
    auto heights = raw.get_normal_key(bl::chunk_key::Data3D);
    int offset = bl::dimension_min_y(raw.pos().dim);
    if (heights.size() < 512) {
        heights = raw.get_normal_key(bl::chunk_key::Data2D);
        if (heights.size() < 512) heights = raw.get_normal_key(bl::chunk_key::Data2DLegacy);
        offset = 0;
    }
    if (heights.size() < 512 || raw.get_sub_chunks().empty()) {
        updateOverview(raw.pos(), 0);
        return;
    }
    const int h = bl::binary::read_i16_le(heights.data() + (8 + 8 * 16) * 2);
    if (h <= 0) {
        updateOverview(raw.pos(), 0);
        return;
    }
    const int y = h - 1 + offset;
    const int section = (y - ((y % 16 + 16) % 16)) / 16;
    updateOverview(raw.pos(), overviewColor(chunkcoords_blocks::sampleBlock(raw.get_sub_chunk(static_cast<int8_t>(section)), y)));
}

QImage ChunkCoordsIndex::overviewImage(int dimension, const QRect& chunks, const QSize& pixels) const {
    if (chunks.isEmpty() || pixels.isEmpty()) return {};
    std::vector<std::pair<QPoint, QImage>> pages;
    {
        std::lock_guard<QMutex> lock(overview_mutex_);
        const auto dim = overview_pages_.find(dimension);
        if (dim != overview_pages_.end()) {
            for (const auto& [region, image] : dim->second) {
                if (chunks.intersects(QRect(region.x(), region.z(), CoordsRegion::SIZE, CoordsRegion::SIZE)))
                    pages.emplace_back(QPoint(region.x(), region.z()), image);
            }
        }
    }
    QImage result(pixels, QImage::Format_RGBA8888);
    result.fill(0);
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    const double sx = double(pixels.width()) / chunks.width();
    const double sz = double(pixels.height()) / chunks.height();
    for (const auto& [pos, image] : pages) {
        const QRectF target((pos.x() - chunks.x()) * sx, (pos.y() - chunks.y()) * sz, CoordsRegion::SIZE * sx, CoordsRegion::SIZE * sz);
        painter.drawImage(target, image);
    }
    return result;
}

std::vector<bl::chunk_pos> ChunkCoordsIndex::terrainRegions(int dimension, const QRect& regions) const {
    auto lock = lockForInteractive();
    std::unordered_set<bl::chunk_pos> present;
    const auto dim = regions_by_dimension_.find(dimension);
    if (dim == regions_by_dimension_.end()) return {};
    for (const auto& region : dim->second) {
        const QRect page(region.x() / constant::RW, region.z() / constant::RW, CoordsRegion::SIZE / constant::RW,
                         CoordsRegion::SIZE / constant::RW);
        if (!regions.intersects(page)) continue;
        region.forEachPresentChunk([&](int x, int z) {
            const auto pos = constant::c2r({x, z, dimension});
            if (regions.contains(QPoint(pos.x / constant::RW, pos.z / constant::RW))) present.insert(pos);
        });
    }
    return {present.begin(), present.end()};
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
