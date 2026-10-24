# Console benchmark; reuse the shared core instead of recompiling loader code.
add_executable(BedrockMapBenchmark EXCLUDE_FROM_ALL
    main.cpp
    chunkcoords_read.cpp
)

target_link_libraries(BedrockMapBenchmark PRIVATE
    BedrockMapCore
)

qt_add_resources(BedrockMapBenchmark benchmark_colors
    PREFIX "/res/colors"
    BASE "${PROJECT_SOURCE_DIR}/bedrock-level/data/colors"
    FILES ${COLOR_FILES}
)

add_executable(BedrockMapChunkCoordsBlocksTest EXCLUDE_FROM_ALL chunkcoords_blocks_test.cpp)
target_link_libraries(BedrockMapChunkCoordsBlocksTest PRIVATE BedrockMapCore)

add_executable(BedrockMapRegionImportTest EXCLUDE_FROM_ALL region_import_test.cpp)
target_link_libraries(BedrockMapRegionImportTest PRIVATE BedrockMapCore)

