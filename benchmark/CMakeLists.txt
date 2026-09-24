add_executable(BedrockMapBenchmark EXCLUDE_FROM_ALL
    main.cpp
    chunkcoords_read.cpp
    ../src/core/chunkcoords.cpp
)

target_include_directories(BedrockMapBenchmark PRIVATE
    ${PROJECT_SOURCE_DIR}/src/include/core
    ${PROJECT_SOURCE_DIR}/src/include/ui
    ${PROJECT_SOURCE_DIR}/src/include
    ${PROJECT_SOURCE_DIR}/third
)

target_link_libraries(BedrockMapBenchmark PRIVATE
    Qt6::Core
    Qt6::Gui
    bedrock-level
)

target_compile_definitions(BedrockMapBenchmark PRIVATE NOMINMAX)

