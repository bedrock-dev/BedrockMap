#include "benchmark.h"

#include <cstdio>
#include <string_view>
#include <vector>

namespace benchmark {

void printUsage(const char* executable) {
    std::printf("Usage:\n");
    std::printf("  %s chunkcoords-read <world-path> [--warmup N] [--repeat N]\n", executable);
    std::printf("\n");
    std::printf("Tests:\n");
    std::printf("  chunkcoords-read  Measure the production chunk-coordinate scan.\n");
    std::printf("\n");
    std::printf("The benchmark calls ChunkCoordsIndex::load() and reports its internal\n");
    std::printf("scan, image-generation, and finish phases.  --warmup defaults to 1\n");
    std::printf("and --repeat defaults to 3.\n");
}

}  // namespace benchmark

int main(int argc, char* argv[]) {
    if (argc < 2) {
        benchmark::printUsage(argv[0]);
        return 1;
    }

    const std::string_view command = argv[1];
    if (command == "--help" || command == "-h" || command == "help") {
        benchmark::printUsage(argv[0]);
        return 0;
    }

    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc - 2));
    for (int index = 2; index < argc; ++index) arguments.emplace_back(argv[index]);

    if (command == "chunkcoords-read") return benchmark::runChunkCoordsRead(arguments);

    std::printf("Unknown benchmark '%.*s'.\n\n", static_cast<int>(command.size()), command.data());
    benchmark::printUsage(argv[0]);
    return 1;
}
