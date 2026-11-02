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
project(BedrockMap VERSION 1.5.0 LANGUAGES CXX)
set(BEDROCKMAP_VERSION_SUFFIX "-beta1" CACHE STRING "Optional BedrockMap version suffix, e.g. -beta1")
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

# Private Qt modules are an intentional part of the scene-graph backend.
set(QT_NO_PRIVATE_MODULE_WARNING ON)

# External dependencies
find_package(Qt6 REQUIRED COMPONENTS
    Core
    CorePrivate
    Concurrent
    Qml
    Quick
    QuickPrivate
    GuiPrivate
    QuickDialogs2Private
    QuickDialogs2UtilsPrivate
    QuickTemplates2Private
    ShaderTools
    Svg
    Network
    LinguistTools
)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# Pure C++ data layer. Its standalone scripts own the unit-test builds, so keep
# GoogleTest out of this tree while leaving its nbt benchmark available.
set(BEDROCK_LEVEL_ROOT "${PROJECT_SOURCE_DIR}/bedrock-level")
set(BEDROCK_LEVEL_BUILD_TESTS OFF CACHE BOOL "Disable bedrock-level tests in the GUI build" FORCE)
set(BEDROCK_LEVEL_BUILD_BENCHMARKS ON CACHE BOOL "Build bedrock-level benchmarks" FORCE)
add_subdirectory(${BEDROCK_LEVEL_ROOT} bedrock-level)

# Shared resources and version metadata
set(APP_ICON_RESOURCE_WINDOWS "${PROJECT_SOURCE_DIR}/icon.rc")
set(BEDROCKMAP_RESOURCE_PREFIX "/bedrockmap")

# Assets keep their paths relative to res/ under one application prefix.
file(GLOB_RECURSE BEDROCKMAP_RESOURCE_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/res/*.png"
    "${PROJECT_SOURCE_DIR}/res/*.jpg"
    "${PROJECT_SOURCE_DIR}/res/*.svg"
)
list(APPEND BEDROCKMAP_RESOURCE_FILES
    "${PROJECT_SOURCE_DIR}/res/fonts/JetBrainsMono-Regular.ttf"
)
if(ANDROID OR EMSCRIPTEN)
    list(APPEND BEDROCKMAP_RESOURCE_FILES
        "${PROJECT_SOURCE_DIR}/res/fonts/NotoSansSC-Regular.otf"
        "${PROJECT_SOURCE_DIR}/res/fonts/NotoSansSC-OFL.txt"
    )
endif()

set(COLOR_FILES
    "${BEDROCK_LEVEL_ROOT}/data/block_color.json"
    "${BEDROCK_LEVEL_ROOT}/data/biome_color.json"
)

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
    src/io/*.cpp
    src/loader/*.cpp
    src/model/*.cpp
    src/render/*.cpp
    src/include/base/*.h
    src/include/controller/*.h
    src/include/io/*.h
    src/include/loader/*.h
    src/include/model/*.h
    src/include/render/*.h
)

add_library(BedrockMapCore STATIC
    ${BEDROCKMAP_CORE_SOURCES}
)
qt_extract_metatypes(BedrockMapCore)

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

# QZipReader and QZipWriter are Qt Core private APIs in this Qt version.
target_link_libraries(BedrockMapCore PRIVATE Qt6::CorePrivate)

target_compile_definitions(BedrockMapCore PUBLIC
    ${BEDROCKMAP_COMMON_DEFINITIONS}
    $<$<CONFIG:Debug>:BEDROCKMAP_ENABLE_RENDER_TIMINGS=1>
)

# Target: BedrockMap (Qt Quick GUI)
file(GLOB_RECURSE QML_SOURCE_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/qml/*.qml"
)
file(GLOB_RECURSE BEDROCKMAP_QML_SOURCES CONFIGURE_DEPENDS
    src/qml/*.cpp
    src/qml/*.h
)

qt_add_executable(BedrockMap)

if(EMSCRIPTEN)
    # The browser entry page is maintained in web/, rather than Qt's default shell.
    set_target_properties(BedrockMap PROPERTIES NO_WASM_DEFAULT_FILES TRUE)
    configure_file(web/index.html "${CMAKE_CURRENT_BINARY_DIR}/index.html" COPYONLY)
    configure_file(web/coi-serviceworker.js "${CMAKE_CURRENT_BINARY_DIR}/coi-serviceworker.js" COPYONLY)
    configure_file("${Qt6_DIR}/../../../plugins/platforms/qtloader.js"
        "${CMAKE_CURRENT_BINARY_DIR}/qtloader.js" COPYONLY)
    # Qt's native browser file engine suspends while awaiting File System Access.
    target_link_options(BedrockMap PRIVATE -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=1048576)
endif()

set(BEDROCKMAP_ASSET_COMPRESSION_OPTIONS)
if(ANDROID OR EMSCRIPTEN)
    # Preserve the complete font and compress resource data even when the
    # default rcc savings threshold would leave a large font uncompressed.
    set(BEDROCKMAP_ASSET_COMPRESSION_OPTIONS --compress-algo zlib --compress 9 --threshold 0)
endif()
qt_add_resources(BedrockMap app_assets
    PREFIX "${BEDROCKMAP_RESOURCE_PREFIX}"
    BASE "${PROJECT_SOURCE_DIR}/res"
    FILES ${BEDROCKMAP_RESOURCE_FILES}
    OPTIONS ${BEDROCKMAP_ASSET_COMPRESSION_OPTIONS}
)

# Keep the repository logo in place for packaging and icon generation.
set_source_files_properties("${PROJECT_SOURCE_DIR}/BedrockMap.svg"
    PROPERTIES QT_RESOURCE_ALIAS "icons/app.svg"
)
qt_add_resources(BedrockMap app_icon
    PREFIX "${BEDROCKMAP_RESOURCE_PREFIX}"
    FILES "${PROJECT_SOURCE_DIR}/BedrockMap.svg"
)

qt_add_resources(BedrockMap color_tables
    PREFIX "${BEDROCKMAP_RESOURCE_PREFIX}/data/colors"
    BASE "${BEDROCK_LEVEL_ROOT}/data"
    FILES ${COLOR_FILES}
)

foreach(qml_file IN LISTS QML_SOURCE_FILES)
    file(RELATIVE_PATH qml_alias "${PROJECT_SOURCE_DIR}/src/qml" "${qml_file}")
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()
set_source_files_properties(src/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(BedrockMap
    URI BedrockMap
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/BedrockMap"
    QML_FILES ${QML_SOURCE_FILES}
    SOURCES ${BEDROCKMAP_QML_SOURCES}
)

target_include_directories(BedrockMap PRIVATE "${PROJECT_SOURCE_DIR}/src/qml")

if(WIN32)
    set_target_properties(BedrockMap PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MINGW)
        # MinGW can start the GUI subsystem through main without Qt's CRT-specific wrapper.
        set_target_properties(BedrockMap PROPERTIES qt_no_entrypoint TRUE)
    endif()
    target_sources(BedrockMap PRIVATE ${APP_ICON_RESOURCE_WINDOWS})
endif()

target_link_libraries(BedrockMap PRIVATE
    BedrockMapCore
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickPrivate
    Qt6::GuiPrivate
    Qt6::QuickDialogs2Private
    Qt6::QuickDialogs2UtilsPrivate
    Qt6::QuickTemplates2Private
    Qt6::Svg
)

# Target: BedrockMapBenchmark (console, excluded from normal builds)
add_subdirectory(${PROJECT_SOURCE_DIR}/benchmark)

# Shared translations
# The application embeds the catalogs; the core is scanned for its tr() strings.
qt6_add_translations(TARGETS BedrockMap
    SOURCE_TARGETS BedrockMap BedrockMapCore
    TS_FILES ${TS_FILES}
    RESOURCE_PREFIX "${BEDROCKMAP_RESOURCE_PREFIX}/i18n"
)

# QML runtime setup
# QRhi's public headers are versioned below the QtGui include directory.
target_include_directories(BedrockMap PRIVATE
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

qt_add_shaders(BedrockMap terrain_shaders
    PREFIX "${BEDROCKMAP_RESOURCE_PREFIX}/shaders"
    BASE "${PROJECT_SOURCE_DIR}/src/qml/shaders"
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

qt_add_shaders(BedrockMap voxel_rhi_shaders
    PREFIX "${BEDROCKMAP_RESOURCE_PREFIX}/shaders"
    BASE "${PROJECT_SOURCE_DIR}/src/qml/shaders"
    PRECOMPILE
    GLSL "300 es,120,150"
    OPTIONS --qt6
    FILES
        src/qml/shaders/voxelrhi.vert
        src/qml/shaders/voxelrhi.frag
)

if(WIN32)
    # qsb falls back to HLSL source without failing when fxc does; catch that
    # here instead of at the first map frame.
    set(_precompiled_qsb
        "${CMAKE_CURRENT_BINARY_DIR}/.qsb/terrainarray.frag.qsb"
        "${CMAKE_CURRENT_BINARY_DIR}/.qsb/voxelrhi.frag.qsb")
    set(_qsb_check_stamp "${CMAKE_CURRENT_BINARY_DIR}/.qsb/dxbc_check.stamp")
    add_custom_command(
        OUTPUT "${_qsb_check_stamp}"
        COMMAND "${CMAKE_COMMAND}" "-DQSB=$<TARGET_FILE:Qt6::qsb>" "-DSHADERS=${_precompiled_qsb}"
                "-DSTAMP=${_qsb_check_stamp}" -P "${PROJECT_SOURCE_DIR}/scripts/check_qsb_dxbc.cmake"
        DEPENDS ${_precompiled_qsb} "${PROJECT_SOURCE_DIR}/scripts/check_qsb_dxbc.cmake"
        COMMENT "Checking precompiled shaders contain DXBC"
        VERBATIM
    )
    # The .qsb files are generated by BedrockMap itself, so the check runs
    # after it instead of gating it. Until it passes, the stamp is missing and
    # every build fails again.
    add_custom_target(BedrockMap_shader_check ALL DEPENDS "${_qsb_check_stamp}")
    add_dependencies(BedrockMap_shader_check BedrockMap)
endif()

# Keep the standalone QML executable runnable directly from the build tree.
# Some Qt installations do not expose their QML import path to applications
# launched outside Qt Creator.
get_filename_component(QT_BUILD_ROOT "${Qt6_DIR}/../../.." ABSOLUTE)
set(QT_QML_IMPORT_ROOT "${QT_BUILD_ROOT}/qml")
if(NOT ANDROID AND NOT EMSCRIPTEN)
    add_custom_command(TARGET BedrockMap POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${QT_QML_IMPORT_ROOT}/QtQml"
                "$<TARGET_FILE_DIR:BedrockMap>/qml/QtQml"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${QT_QML_IMPORT_ROOT}/QtQuick"
                "$<TARGET_FILE_DIR:BedrockMap>/qml/QtQuick"
        VERBATIM
    )
endif()

# Platform: Android
if(ANDROID)
    set(BEDROCKMAP_ANDROID_COMPILE_SDK "37.0" CACHE STRING "Installed Android SDK platform version")
    option(BEDROCKMAP_ANDROID_COMPRESS_LIBS "Compress native libraries inside the APK" ON)
    set_property(TARGET BedrockMap PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${PROJECT_SOURCE_DIR}/android")
    set_property(TARGET BedrockMap PROPERTY QT_ANDROID_PACKAGE_NAME "io.github.hhhxiao.bedrockmap")
    set_property(TARGET BedrockMap PROPERTY QT_ANDROID_APP_NAME "BedrockMap")
    set_property(TARGET BedrockMap PROPERTY QT_ANDROID_VERSION_CODE "2")
    set_property(TARGET BedrockMap PROPERTY QT_ANDROID_VERSION_NAME "${BEDROCKMAP_VERSION}")
    if(BEDROCKMAP_ANDROID_COMPRESS_LIBS)
        set_property(TARGET BedrockMap PROPERTY QT_ANDROID_LEGACY_PACKAGING TRUE)
    endif()
    set_property(TARGET BedrockMap PROPERTY QT_QML_ROOT_PATH "${PROJECT_SOURCE_DIR}/src/qml")
    set_property(TARGET BedrockMap PROPERTY QT_ANDROID_COMPILE_SDK_VERSION "${BEDROCKMAP_ANDROID_COMPILE_SDK}")
    target_link_libraries(BedrockMapCore PUBLIC log)
    qt_import_plugins(BedrockMap EXCLUDE_BY_TYPE qmltooling styles)
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
        foreach(_target BedrockMapCore BedrockMap)
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
    target_compile_definitions(BedrockMap PRIVATE QT_NO_DEBUG_OUTPUT)
endif()
