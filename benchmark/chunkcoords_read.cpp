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
#include "leveldb/iterator.h"
#include "loader/chunkcoords.h"
#include "loader/leveldbxor.h"

namespace benchmark {
    namespace {

        struct Options {
            std::string world_path;
            BenchmarkTimerOptions timer;
            bool build_overview{false};
            bool compare{false};
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
                if (option == "--overview") {
                    options.build_overview = true;
                    continue;
                }
                if (option == "--compare") {
                    options.compare = true;
                    continue;
                }
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
            if (options.compare && options.build_overview) {
                std::printf("chunkcoords-read: use either --compare or --overview.\n");
                return false;
            }
            return true;
        }

    }  // namespace

    int runChunkCoordsRead(const std::vector<std::string_view>& arguments) {
        Options options;
        if (!parseOptions(arguments, options)) {
            std::printf(
                "Usage: BedrockMapBenchmark chunkcoords-read <world-path> [--compare | --overview] [--warmup N] [--repeat N]\n");
            return 1;
        }

        // Match ChunkStorage's production configuration: bulk scans use the
        // libdeflate-backed decompression allocator rather than the legacy path.
        bl::bedrock_level level{true};
        if (!leveldb_xor::isEncrypted(options.world_path)) level.set_xor_key({});
        if (!level.open(options.world_path)) {
            std::printf("chunkcoords-read: cannot open '%s'.\n", options.world_path.c_str());
            return 1;
        }
        if (!level.db()) {
            std::printf("chunkcoords-read: the opened level has no database.\n");
            return 1;
        }

        // Initialize production color tables outside the measured scan passes.
        if (options.compare || options.build_overview) constant::initColorTable();
        const BenchmarkTimer timer(options.timer);
        const std::atomic_bool never_stop{false};
        std::printf("\n== chunkcoords read ==\n");
        std::printf("world: %s\n", options.world_path.c_str());
        std::printf("runs: %d warmup, %d measured\n", options.timer.warmup_runs, options.timer.measured_runs);
        std::printf("Sampling trusts height-map minus one at (8,8); v8/v9 base storage only, no chunk objects or rendering.\n");
        std::printf("Overview mode includes block decoding, color lookup, and overview color storage.\n");
        std::printf("Each mode warms its own passes; these are warm-cache timings, not a cold-disk comparison.\n");
        std::fflush(stdout);
        BenchmarkTiming baseline;
        double baseline_scan = 0.0;
        double baseline_ready = 0.0;
        std::uint64_t baseline_chunks = 0;
        const int mode_count = options.compare ? 2 : 1;
        for (int mode = 0; mode < mode_count; ++mode) {
            const bool build_overview = options.compare ? mode == 1 : options.build_overview;
            ChunkCoordsLoadOptions load_options;
            load_options.build_overview = build_overview;
            bool valid = true;
            int iteration = 0;
            std::vector<ChunkCoordsLoadStats> measured;
            const auto timing = timer.measure(build_overview ? "overview-on" : "overview-off", [&] {
                ChunkCoordsIndex index;
                ChunkCoordsLoadStats stats;
                valid = index.load(level, never_stop, {}, &stats, load_options) && valid;
                if (iteration++ >= options.timer.warmup_runs) measured.push_back(stats);
            });
            if (!valid || measured.empty()) {
                std::printf("chunkcoords-read: ChunkCoordsIndex::load failed.\n");
                return 1;
            }
            const auto& stats = measured.back();
            double scan = 0.0, ready = 0.0, drain = 0.0, busy = 0.0, queue_wait = 0.0;
            for (const auto& run : measured) {
                scan += run.scan_ms;
                ready += run.index_ready_ms;
                drain += run.sampling_drain_ms;
                busy += run.sampling_busy_ms;
                queue_wait += run.sampling_queue_wait_ms;
                if (build_overview && (run.sampling_chunks != run.unique_chunks || run.sampling_checksum != stats.sampling_checksum ||
                                       run.sampled_blocks != stats.sampled_blocks))
                    valid = false;
            }
            const double count = static_cast<double>(measured.size());
            scan /= count;
            ready /= count;
            drain /= count;
            busy /= count;
            queue_wait /= count;
            std::printf("\n%s: %s\n", timing.label.c_str(), build_overview ? "1 scanner + 1 sampler" : "1 scanner");
            std::printf("keys: %llu visited, %llu chunks\n", static_cast<unsigned long long>(stats.scanned_keys),
                        static_cast<unsigned long long>(stats.unique_chunks));
            std::printf("wall: min %.2f, median %.2f, mean %.2f, max %.2f ms (includes index destruction)\n", timing.minimum_ms,
                        timing.median_ms, timing.mean_ms, timing.maximum_ms);
            std::printf("mean phases: scan %.2f, index-ready %.2f, sampler-drain %.2f ms\n", scan, ready, drain);
            if (build_overview) {
                std::printf(
                    "samples: %llu blocks, %llu empty/air, %llu missing, %llu unsupported, %llu malformed; %llu subchunks parsed\n",
                    static_cast<unsigned long long>(stats.sampled_blocks), static_cast<unsigned long long>(stats.sampling_empty),
                    static_cast<unsigned long long>(stats.sampling_missing), static_cast<unsigned long long>(stats.sampling_unsupported),
                    static_cast<unsigned long long>(stats.sampling_malformed), static_cast<unsigned long long>(stats.sampling_subchunks));
                std::printf("missing: %llu height maps, %llu target subchunks\n",
                            static_cast<unsigned long long>(stats.sampling_missing_height),
                            static_cast<unsigned long long>(stats.sampling_missing - stats.sampling_missing_height));
                std::printf("pipeline: copied %.2f MiB, queue peak %.2f MiB, mean scanner wait %.2f ms, sampler busy %.2f ms\n",
                            stats.sampling_copied_bytes / 1048576.0, stats.sampling_peak_queue_bytes / 1048576.0, queue_wait, busy);
                std::printf("checksum: %016llx; coverage/checksum checks: %s\n", static_cast<unsigned long long>(stats.sampling_checksum),
                            valid ? "PASS" : "FAIL");
            }
            if (!valid) return 1;
            if (options.compare && mode == 0) {
                baseline = timing;
                baseline_scan = scan;
                baseline_ready = ready;
                baseline_chunks = stats.unique_chunks;
            } else if (options.compare) {
                if (baseline_chunks != stats.unique_chunks) {
                    std::printf("chunkcoords-read: index chunk counts differ between modes.\n");
                    return 1;
                }
                const auto delta = [](double after, double before) { return before > 0.0 ? (after / before - 1.0) * 100.0 : 0.0; };
                std::printf(
                    "\nOverview overhead: scan %+.2f ms (%+.1f%%), index-ready %+.2f ms (%+.1f%%), median wall %+.2f ms (%+.1f%%)\n",
                    scan - baseline_scan, delta(scan, baseline_scan), ready - baseline_ready, delta(ready, baseline_ready),
                    timing.median_ms - baseline.median_ms, delta(timing.median_ms, baseline.median_ms));
            }
            std::fflush(stdout);
        }
        return 0;
    }

}  // namespace benchmark
