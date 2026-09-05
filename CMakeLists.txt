cmake_minimum_required(VERSION 3.16)

set(CMAKE_PREFIX_PATH $ENV{QT_ROOT})
message(STATUS "QT_ROOT environment variable: $ENV{QT_ROOT}")
message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

project(BedrockMap VERSION 0.1 LANGUAGES CXX)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -finput-charset=utf-8 -fexec-charset=utf-8")
endif()


# bedrock-level is built from source as a subdirectory so app headers and the
# library can never drift apart. Its sample apps/unit tests are opt-in (OFF).
set(BEDROCK_LEVEL_ROOT "${PROJECT_SOURCE_DIR}/bedrock-level")
# The library's standalone scripts own its unit-test builds; keep the GUI build
# focused on the application and never pull GoogleTest into this build tree.
set(BEDROCK_LEVEL_BUILD_TESTS OFF CACHE BOOL "Disable bedrock-level tests in the GUI build" FORCE)
add_subdirectory(${BEDROCK_LEVEL_ROOT} bedrock-level)

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

# Generate the resource manifest in the build tree so configuring CMake never
# modifies a tracked source file.
set(QRC_FILE ${CMAKE_CURRENT_BINARY_DIR}/res.qrc)
file(WRITE ${QRC_FILE} "<RCC>\n    <qresource prefix=\"/\">\n")
foreach(FILE ${RESOURCE_FILES})
    file(RELATIVE_PATH REL_PATH ${CMAKE_CURRENT_SOURCE_DIR} ${FILE})
    string(REPLACE "\\" "/" REL_PATH ${REL_PATH})
    string(REPLACE "\\" "/" RESOURCE_PATH ${FILE})
    file(APPEND ${QRC_FILE} "        <file alias=\"${REL_PATH}\">${RESOURCE_PATH}</file>\n")
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

target_include_directories(BedrockMap PRIVATE
    ${PROJECT_SOURCE_DIR}/src/include/core
    ${PROJECT_SOURCE_DIR}/src/include/ui
    ${PROJECT_SOURCE_DIR}/src/include
    ${PROJECT_SOURCE_DIR}/third
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

# OFF by default: -g1 keeps crash-handler backtraces symbolized without the
# ~90MB of full debug info. ON gives full -g for gdb local-variable debugging.
option(BEDROCKMAP_FULL_DEBUG "Build with full -g debug info (for gdb)" OFF)

if(WIN32)
    if(MINGW)
        # crashhandler.cpp unwinds via the RBP frame chain and symbolizes with
        # libbacktrace (reads the DWARF from the exe). -g1 is enough for that:
        # it keeps function names + file/line but drops local vars & full type
        # info, which slashes the ~90MB of debug bloat. Turn BEDROCKMAP_FULL_DEBUG
        # on (cmake -DBEDROCKMAP_FULL_DEBUG=ON) if you need full -g for gdb.
        if(BEDROCKMAP_FULL_DEBUG)
            set(_dbg_flag -g)
        else()
            set(_dbg_flag -g1)
        endif()
        target_compile_options(BedrockMap PRIVATE ${_dbg_flag} -fno-omit-frame-pointer)
        # ntdll exports RtlLookupFunctionEntry/RtlVirtualUnwind, which the crash
        # handler uses to unwind via the per-function .pdata/.xdata metadata.
        target_link_libraries(BedrockMap PRIVATE backtrace ntdll)
    endif()
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
