cmake_minimum_required(VERSION 3.16)

# Toolchain
# Resolve Qt from QT_ROOT so local builds do not depend on a system install.
set(CMAKE_PREFIX_PATH $ENV{QT_ROOT})
message(STATUS "QT_ROOT environment variable: $ENV{QT_ROOT}")
message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

# Project
# Qt Quick application and its data layer.
project(BedrockMap VERSION 1.2.0 LANGUAGES CXX)
set(BEDROCKMAP_VERSION_SUFFIX "" CACHE STRING "Optional BedrockMap version suffix, e.g. -beta1")
set(BEDROCKMAP_VERSION "${PROJECT_VERSION}${BEDROCKMAP_VERSION_SUFFIX}")

# Qt AUTOGEN handles resources and moc for the data layer and Qt Quick application.
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -finput-charset=utf-8 -fexec-charset=utf-8")

# External dependencies
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Concurrent
    Qml
    Quick
    ShaderTools
    Svg
    Network
    LinguistTools
)

# Pure C++ data layer. Its standalone scripts own the unit-test builds, so keep
# GoogleTest out of this tree while leaving its nbt benchmark available.
set(BEDROCK_LEVEL_ROOT "${PROJECT_SOURCE_DIR}/bedrock-level")
set(BEDROCK_LEVEL_BUILD_TESTS OFF CACHE BOOL "Disable bedrock-level tests in the GUI build" FORCE)
set(BEDROCK_LEVEL_BUILD_BENCHMARKS ON CACHE BOOL "Build bedrock-level benchmarks" FORCE)
add_subdirectory(${BEDROCK_LEVEL_ROOT} bedrock-level)

# Shared resources and version metadata
set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/icon.rc")

# Application resources: data icons, QML sources and QML shell icons.
file(GLOB_RECURSE RESOURCE_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.jpg
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.svg
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.dat
)
list(FILTER RESOURCE_FILES EXCLUDE REGEX "/res/qml/")

file(GLOB_RECURSE QML_SOURCE_FILES CONFIGURE_DEPENDS
    src/qml/*.qml
)
file(GLOB_RECURSE QML_ICON_FILES CONFIGURE_DEPENDS
    res/qml/*.svg
)

# Generate the combined resource manifest in the build tree so configuring
# CMake never modifies a tracked source file. A single root qresource block
# keeps every runtime path unchanged through explicit alias prefixes.
set(QRC_FILE ${CMAKE_CURRENT_BINARY_DIR}/res.qrc)
file(WRITE "${QRC_FILE}" "<RCC>\n    <qresource prefix=\"/\">\n")

set(COLOR_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/bedrock-level/data/colors/block_color.json
    ${CMAKE_CURRENT_SOURCE_DIR}/bedrock-level/data/colors/biome_color.json
)
set(BEDROCKMAP_RESOURCE_FILES
    ${RESOURCE_FILES}
    ${QML_SOURCE_FILES}
    ${QML_ICON_FILES}
    ${COLOR_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/BedrockMap.svg
)

foreach(resource_file IN LISTS BEDROCKMAP_RESOURCE_FILES)
    string(REPLACE "\\" "/" resource_path "${resource_file}")
    if(resource_path MATCHES "/src/qml/(.+)$")
        set(alias_path "qml/${CMAKE_MATCH_1}")
    elseif(resource_path MATCHES "/res/qml/(.+)$")
        set(alias_path "icons/${CMAKE_MATCH_1}")
    elseif(resource_path MATCHES "/bedrock-level/data/colors/(.+)$")
        set(alias_path "res/colors/${CMAKE_MATCH_1}")
    else()
        file(RELATIVE_PATH alias_path "${CMAKE_CURRENT_SOURCE_DIR}" "${resource_path}")
        string(REPLACE "\\" "/" alias_path "${alias_path}")
    endif()
    file(APPEND "${QRC_FILE}" "        <file alias=\"${alias_path}\">${resource_path}</file>\n")
endforeach()
file(APPEND "${QRC_FILE}" "    </qresource>\n</RCC>\n")

set(TS_FILES
    translations/zh_CN.ts
    translations/en.ts
)

# Short commit hash shown in version/about text; resolved at configure time.
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

set(BEDROCKMAP_COMMON_DEFINITIONS
    NOMINMAX
    BEDROCKMAP_VERSION="${BEDROCKMAP_VERSION}"
    GIT_COMMIT_HASH="${GIT_COMMIT_HASH}"
)

# Core library
# Data, session and CPU rendering code. No Qt Widgets or Qt Quick dependency is
# allowed here.
file(GLOB_RECURSE BEDROCKMAP_CORE_SOURCES CONFIGURE_DEPENDS
    src/base/*.cpp
    src/controller/*.cpp
    src/loader/*.cpp
    src/model/*.cpp
    src/render/*.cpp
    src/include/base/*.h
    src/include/controller/*.h
    src/include/loader/*.h
    src/include/model/*.h
    src/include/render/*.h
)

add_library(BedrockMapCore STATIC
    ${BEDROCKMAP_CORE_SOURCES}
)

target_include_directories(BedrockMapCore PUBLIC
    ${PROJECT_SOURCE_DIR}/src/include
    ${PROJECT_SOURCE_DIR}/third
)

target_link_libraries(BedrockMapCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
    Qt6::Network
    bedrock-level
)

target_compile_definitions(BedrockMapCore PUBLIC
    ${BEDROCKMAP_COMMON_DEFINITIONS}
    $<$<CONFIG:Debug>:BEDROCKMAP_ENABLE_RENDER_TIMINGS=1>
)

# Target: BedrockMapQml (Qt Quick GUI)
file(GLOB_RECURSE BEDROCKMAP_QML_SOURCES CONFIGURE_DEPENDS
    src/qml/*.cpp
    src/qml/*.h
)

qt_add_executable(BedrockMapQml
    ${BEDROCKMAP_QML_SOURCES}
    ${QRC_FILE}
)

if(WIN32)
    target_sources(BedrockMapQml PRIVATE ${APP_ICON_RESOURCE_WINDOWS})
endif()

target_link_libraries(BedrockMapQml PRIVATE
    BedrockMapCore
    Qt6::Qml
    Qt6::Quick
    Qt6::Svg
)

# Target: BedrockMapBenchmark (console, excluded from normal builds)
add_subdirectory(${PROJECT_SOURCE_DIR}/benchmark)

# Shared translations
# The application embeds the catalogs; the core is scanned for its tr() strings.
qt6_add_translations(TARGETS BedrockMapQml
    SOURCE_TARGETS BedrockMapQml BedrockMapCore
    TS_FILES ${TS_FILES}
    RESOURCE_PREFIX "/i18n"
)

# QML runtime setup
# QRhi's public headers are versioned below the QtGui include directory.
target_include_directories(BedrockMapQml PRIVATE
    "${Qt6Gui_DIR}/../../../include/QtGui/${Qt6Gui_VERSION}/QtGui"
)

# qsb invokes fxc for PRECOMPILE on Windows. Keep the SDK tool visible even
# when the project is built from a plain PowerShell/IDE session.
if(WIN32)
    file(GLOB _fxc_candidates "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe")
    list(SORT _fxc_candidates COMPARE STRING ORDER DESCENDING)
    if(NOT _fxc_candidates)
        find_program(_fxc_executable NAMES fxc)
    endif()
    if(_fxc_candidates)
        list(GET _fxc_candidates 0 _fxc_executable)
    endif()
    if(_fxc_executable)
        # Ninja custom commands do not inherit variables assigned to ENV during
        # configure, but they run from the build root. Placing fxc there lets
        # qsb resolve the bare "fxc" command on every build.
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_fxc_executable}" "${CMAKE_CURRENT_BINARY_DIR}/fxc.exe"
        )
    else()
        message(FATAL_ERROR "Windows SDK fxc.exe is required to precompile the QML terrain shaders.")
    endif()
endif()

qt_add_shaders(BedrockMapQml terrain_shaders
    PREFIX "/bedrockmap/shaders"
    # BATCHABLE lets Qt Quick's scene graph accept the vertex stage as a
    # batchable material; the keyword form is required here, not -b.
    BATCHABLE
    # Ship DXBC in the QSB on Windows. The terrain fragment shader has AO and
    # shadow marches; compiling it through D3DCompiler at first paint blocked
    # the render thread for several seconds.
    PRECOMPILE
    # GLES 3 provides explicit texture LOD and integer operations natively.
    GLSL "300 es,120,150"
    OPTIONS --qt6
    FILES
        src/qml/shaders/terrainarray.vert
        src/qml/shaders/terrainarray.frag
)

qt_add_shaders(BedrockMapQml voxel_rhi_shaders
    PREFIX "/bedrockmap/shaders"
    PRECOMPILE
    GLSL "300 es,120,150"
    OPTIONS --qt6
    FILES
        src/qml/shaders/voxelrhi.vert
        src/qml/shaders/voxelrhi.frag
)

# Keep the standalone QML executable runnable directly from the build tree.
# Some Qt installations do not expose their QML import path to applications
# launched outside Qt Creator.
get_filename_component(QT_BUILD_ROOT "${Qt6_DIR}/../../.." ABSOLUTE)
set(QT_QML_IMPORT_ROOT "${QT_BUILD_ROOT}/qml")
add_custom_command(TARGET BedrockMapQml POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_QML_IMPORT_ROOT}/QtQml"
            "$<TARGET_FILE_DIR:BedrockMapQml>/qml/QtQml"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_QML_IMPORT_ROOT}/QtQuick"
            "$<TARGET_FILE_DIR:BedrockMapQml>/qml/QtQuick"
    VERBATIM
)

# Platform: Android
if(ANDROID)
    set(BEDROCKMAP_ANDROID_COMPILE_SDK "37.0" CACHE STRING "Installed Android SDK platform version")
    option(BEDROCKMAP_ANDROID_COMPRESS_LIBS "Compress native libraries inside the APK" OFF)
    set_property(TARGET BedrockMapQml PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${PROJECT_SOURCE_DIR}/android")
    set_property(TARGET BedrockMapQml PROPERTY QT_ANDROID_PACKAGE_NAME "io.github.hhhxiao.bedrockmap")
    set_property(TARGET BedrockMapQml PROPERTY QT_ANDROID_APP_NAME "BedrockMap")
    set_property(TARGET BedrockMapQml PROPERTY QT_ANDROID_VERSION_CODE "2")
    set_property(TARGET BedrockMapQml PROPERTY QT_ANDROID_VERSION_NAME "1.2.0")
    if(BEDROCKMAP_ANDROID_COMPRESS_LIBS)
        set_property(TARGET BedrockMapQml PROPERTY QT_ANDROID_LEGACY_PACKAGING TRUE)
    endif()
    set_property(TARGET BedrockMapQml PROPERTY QT_QML_ROOT_PATH "${PROJECT_SOURCE_DIR}/src/qml")
    set_property(TARGET BedrockMapQml PROPERTY QT_ANDROID_COMPILE_SDK_VERSION "${BEDROCKMAP_ANDROID_COMPILE_SDK}")
    target_link_libraries(BedrockMapCore PUBLIC log)
endif()

# Platform: Windows / MinGW
# OFF by default: -g1 keeps crash-handler backtraces symbolized without the
# ~90MB of full debug info. ON gives full -g for gdb local-variable debugging.
option(BEDROCKMAP_FULL_DEBUG "Build with full -g debug info (for gdb)" OFF)

if(WIN32)
    if(MINGW)
        if(BEDROCKMAP_FULL_DEBUG)
            set(_dbg_flag -g)
        else()
            set(_dbg_flag -g1)
        endif()
        foreach(_target BedrockMapCore BedrockMapQml)
            target_compile_options(${_target} PRIVATE ${_dbg_flag} -fno-omit-frame-pointer)
        endforeach()
        # The crash handler lives in the core. ntdll provides
        # RtlLookupFunctionEntry/RtlVirtualUnwind.
        target_compile_definitions(BedrockMapCore PUBLIC BEDROCKMAP_ENABLE_CRASH_HANDLER=1)
        target_link_libraries(BedrockMapCore PUBLIC backtrace ntdll)
    endif()
endif()

# Release trimming
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    target_compile_definitions(BedrockMapCore PRIVATE QT_NO_DEBUG_OUTPUT)
    target_compile_definitions(BedrockMapQml PRIVATE QT_NO_DEBUG_OUTPUT)
endif()
