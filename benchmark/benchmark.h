#ifndef BEDROCKMAP_BENCHMARK_H
#define BEDROCKMAP_BENCHMARK_H

#include <string_view>
#include <vector>

namespace benchmark {

    /// Runs the chunk-coordinate read benchmark. Arguments exclude the command
    /// name itself and start with the path to a Bedrock world.
    int runChunkCoordsRead(const std::vector<std::string_view>& arguments);

    void printUsage(const char* executable);

}  // namespace benchmark

#endif  // BEDROCKMAP_BENCHMARK_H
