#include <QImage>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "bedrock_level.h"
#include "bench_timer.h"
#include "benchmark.h"
#include "chunkcoords.h"
#include "leveldb/iterator.h"
#include "maptile.h"

// ChunkCoordsIndex uses MapTile only to create its 128x128 image backing tile.
// The benchmark deliberately does not link the whole map renderer, but supplies
// the same-sized immutable tile so the production indexing code remains intact.
QImage& MapTile::COORDS_EMPTY_TILE() {
    static QImage image = [] {
        QImage tile(constant::COORDS_REGION_SIZE, constant::COORDS_REGION_SIZE, QImage::Format_RGB32);
        constexpr QRgb dark = 0xff141414u;
        constexpr QRgb light = 0xff282828u;
        constexpr int half = constant::COORDS_REGION_SIZE / 2;
        for (int y = 0; y < tile.height(); ++y) {
            auto* row = reinterpret_cast<QRgb*>(tile.scanLine(y));
            for (int x = 0; x < tile.width(); ++x) row[x] = ((x / half) ^ (y / half)) ? light : dark;
        }
        return tile;
    }();
    return image;
}

namespace benchmark {
    namespace {

        struct Options {
            std::string world_path;
            BenchmarkTimerOptions timer;
        };

        bool parsePositiveInt(std::string_view text, int& value) {
            int parsed = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
            if (error != std::errc{} || end != text.data() + text.size() || parsed <= 0) return false;
            value = parsed;
            return true;
        }

        bool parseOptions(const std::vector<std::string_view>& arguments, Options& options) {
            if (arguments.empty() || arguments.front() == "--help" || arguments.front() == "-h") return false;
            options.world_path = std::string(arguments.front());

            for (std::size_t index = 1; index < arguments.size(); ++index) {
                const auto option = arguments[index];
                if (option != "--warmup" && option != "--repeat") {
                    std::printf("chunkcoords-read: unknown option '%.*s'.\n", static_cast<int>(option.size()), option.data());
                    return false;
                }
                if (++index == arguments.size()) {
                    std::printf("chunkcoords-read: '%.*s' requires a positive integer.\n", static_cast<int>(option.size()), option.data());
                    return false;
                }
                int value = 0;
                if (!parsePositiveInt(arguments[index], value)) {
                    std::printf("chunkcoords-read: '%.*s' is not a positive integer.\n", static_cast<int>(arguments[index].size()),
                                arguments[index].data());
                    return false;
                }
                if (option == "--warmup") {
                    options.timer.warmup_runs = value;
                } else {
                    options.timer.measured_runs = value;
                }
            }
            return true;
        }

    }  // namespace

    int runChunkCoordsRead(const std::vector<std::string_view>& arguments) {
        Options options;
        if (!parseOptions(arguments, options)) {
            std::printf("Usage: BedrockMapBenchmark chunkcoords-read <world-path> [--warmup N] [--repeat N]\n");
            return 1;
        }

        // Match ChunkStorage's production configuration: bulk scans use the
        // libdeflate-backed decompression allocator rather than the legacy path.
        bl::bedrock_level level{true};
        if (!level.open(options.world_path)) {
            std::printf("chunkcoords-read: cannot open '%s'.\n", options.world_path.c_str());
            return 1;
        }
        if (!level.db()) {
            std::printf("chunkcoords-read: the opened level has no database.\n");
            return 1;
        }

        const BenchmarkTimer timer(options.timer);
        bool index_loaded = true;
        ChunkCoordsLoadStats load_stats;
        const std::atomic_bool never_stop{false};
        const auto index_timing = timer.measure("chunkcoords-load", [&] {
            ChunkCoordsIndex index;
            ChunkCoordsLoadStats current_stats;
            index_loaded = index.load(level, never_stop, {}, &current_stats);
            load_stats = current_stats;
        });

        if (!index_loaded) {
            std::printf("chunkcoords-read: ChunkCoordsIndex::load failed.\n");
            return 1;
        }

        std::printf("\n== chunkcoords read ==\n");
        std::printf("world: %s\n", options.world_path.c_str());
        std::printf("runs: %d warmup, %d measured\n", options.timer.warmup_runs, options.timer.measured_runs);
        std::printf("keys: %llu visited (%llu run-skips), %llu chunk-format, %llu marker, %llu unique chunks, %llu regions\n",
                    static_cast<unsigned long long>(load_stats.scanned_keys), static_cast<unsigned long long>(load_stats.run_skips),
                    static_cast<unsigned long long>(load_stats.chunk_keys), static_cast<unsigned long long>(load_stats.marker_keys),
                    static_cast<unsigned long long>(load_stats.unique_chunks),
                    static_cast<unsigned long long>(load_stats.generated_regions));
        std::printf("\n%-16s min %9s  median %9s  mean %9s  max %9s\n", "phase", "ms", "ms", "ms", "ms");
        std::printf("%-16s min %9.2f  median %9.2f  mean %9.2f  max %9.2f ms\n", index_timing.label.c_str(), index_timing.minimum_ms,
                    index_timing.median_ms, index_timing.mean_ms, index_timing.maximum_ms);
        std::printf("internal last run: scan %0.2f ms, images %0.2f ms, finish %0.2f ms, total %0.2f ms\n", load_stats.scan_ms,
                    load_stats.image_generation_ms, load_stats.finish_scan_ms, load_stats.total_ms);
        return 0;
    }

}  // namespace benchmark
