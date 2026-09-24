#ifndef BEDROCKMAP_BENCH_TIMER_H
#define BEDROCKMAP_BENCH_TIMER_H

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace benchmark {

/// Shared settings for every benchmark phase. Warmup passes establish the
/// cache state, while measured passes produce a distribution rather than one
/// noisy wall-clock value.
struct BenchmarkTimerOptions {
    int warmup_runs{1};
    int measured_runs{3};
};

struct BenchmarkTiming {
    std::string label;
    double minimum_ms{0.0};
    double median_ms{0.0};
    double mean_ms{0.0};
    double maximum_ms{0.0};
};

/// Reusable wall-clock timer with no dependency on a specific world, renderer,
/// or counter type. New benchmark files can use it for any named phase.
class BenchmarkTimer {
   public:
    explicit BenchmarkTimer(BenchmarkTimerOptions options) : options_(options) {}

    template <typename Function>
    BenchmarkTiming measure(std::string_view label, Function&& function) const {
        for (int iteration = 0; iteration < options_.warmup_runs; ++iteration) function();

        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(options_.measured_runs));
        for (int iteration = 0; iteration < options_.measured_runs; ++iteration) {
            const auto start = Clock::now();
            function();
            samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        }

        std::sort(samples.begin(), samples.end());
        const auto middle = samples.size() / 2;
        const double median = samples.size() % 2 == 0 ? (samples[middle - 1] + samples[middle]) / 2.0 : samples[middle];
        const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
        return {std::string(label), samples.front(), median, sum / samples.size(), samples.back()};
    }

   private:
    using Clock = std::chrono::steady_clock;

    BenchmarkTimerOptions options_;
};

}  // namespace benchmark

#endif  // BEDROCKMAP_BENCH_TIMER_H
