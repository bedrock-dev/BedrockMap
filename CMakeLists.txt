cmake_minimum_required(VERSION 3.16)

set(CMAKE_PREFIX_PATH $ENV{QT_ROOT})
message(STATUS "QT_ROOT environment variable: $ENV{QT_ROOT}")
message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

project(BedrockMap VERSION 0.1 LANGUAGES CXX)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -finput-charset=utf-8 -fexec-charset=utf-8")
endif()


# bedrock-level is built from source as a subdirectory so app headers and the
# library can never drift apart. Its sample apps/unit tests are opt-in (OFF).
set(BEDROCK_LEVEL_ROOT "${PROJECT_SOURCE_DIR}/bedrock-level")
add_subdirectory(${BEDROCK_LEVEL_ROOT} bedrock-level)

include_directories(
        # App include subfolders come first so app headers (e.g. core/config.h)
        # win over same-named bedrock-level headers.
        ${PROJECT_SOURCE_DIR}/src/include/core
        ${PROJECT_SOURCE_DIR}/src/include/ui
        ${PROJECT_SOURCE_DIR}/src/include
        ${BEDROCK_LEVEL_ROOT}/src/include
        ${BEDROCK_LEVEL_ROOT}/third
        ${PROJECT_SOURCE_DIR}/third
        # for clangd
        ${CMAKE_CURRENT_BINARY_DIR}/BedrockMap_autogen/include
)

find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Widgets 
    Concurrent 
    OpenGL 
    OpenGLWidgets
    Svg
    Network
    LinguistTools
)

set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/icon.rc")

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
        src/*.ui
        src/*.cpp
        src/include/*.h
        third/*.cpp
)

file(GLOB_RECURSE RESOURCE_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.jpg
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.svg
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.dat
)

set(QRC_FILE ${PROJECT_SOURCE_DIR}/res.qrc)
file(WRITE ${QRC_FILE} "<RCC>\n    <qresource prefix=\"/\">\n")
foreach(FILE ${RESOURCE_FILES})
    file(RELATIVE_PATH REL_PATH ${CMAKE_CURRENT_SOURCE_DIR} ${FILE})
    string(REPLACE "\\" "/" REL_PATH ${REL_PATH})
    file(APPEND ${QRC_FILE} "        <file>${REL_PATH}</file>\n")
endforeach()
file(APPEND ${QRC_FILE} "    </qresource>\n</RCC>\n")

set(TS_FILES
    translations/zh_CN.ts
    translations/en.ts
)

qt6_add_translations(BedrockMap
    TS_FILES ${TS_FILES}
)

add_executable(BedrockMap
    ${SRC_FILES}
    ${APP_ICON_RESOURCE_WINDOWS}
    ${QRC_FILE}
)

target_link_libraries(BedrockMap PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Svg
    Qt6::Network
    bedrock-level
)

target_precompile_headers(BedrockMap PRIVATE
    src/include/pch.h
)

target_compile_definitions(BedrockMap PRIVATE NOMINMAX)

execute_process(
    COMMAND git rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()
message(STATUS "Git commit hash: ${GIT_COMMIT_HASH}")
target_compile_definitions(BedrockMap PRIVATE GIT_COMMIT_HASH="${GIT_COMMIT_HASH}")

if(WIN32)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(BedrockMap PRIVATE
            -static-libgcc
            -static-libstdc++
            -mconsole
        )
    else()
        target_link_options(BedrockMap PRIVATE
            -static-libgcc
            -static-libstdc++
            -mwindows
        )
    endif()
endif()


if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    target_compile_definitions(BedrockMap PRIVATE QT_NO_DEBUG_OUTPUT)
endif()
