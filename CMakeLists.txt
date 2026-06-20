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

# 统一 UTF-8 编码：MSVC 用 /utf-8，GCC/Clang 用 -finput-charset=utf-8 -fexec-charset=utf-8
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -finput-charset=utf-8 -fexec-charset=utf-8")
endif()



#bedrock-level
set(BEDROCK_LEVEL_ROOT "${PROJECT_SOURCE_DIR}/bedrock-level")

set(BEDROCK_LIBS
        ${BEDROCK_LEVEL_ROOT}/build/libbedrock-level.a
        ${BEDROCK_LEVEL_ROOT}/libs/libleveldb.a
        ${BEDROCK_LEVEL_ROOT}/libs/libzs.a
)

#dir
include_directories(
        ${BEDROCK_LEVEL_ROOT}/src/include
        ${BEDROCK_LEVEL_ROOT}/third
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/third
        # for clangd
        ${CMAKE_CURRENT_BINARY_DIR}/BedrockMap_autogen/include
)

#qt6
find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Widgets 
    Concurrent 
    OpenGL 
    OpenGLWidgets
    LinguistTools
)

# 定义资源文件
set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/icon.rc")

# 收集源文件
file(GLOB_RECURSE SRC_FILES
        src/*.ui
        src/*.cpp
        src/include/*.h
        third/*.cpp
)

# 收集资源文件
file(GLOB_RECURSE RESOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.jpg
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.dat
)

# 生成 qrc 文件
set(QRC_FILE ${PROJECT_SOURCE_DIR}/res.qrc)
file(WRITE ${QRC_FILE} "<RCC>\n    <qresource prefix=\"/\">\n")
foreach(FILE ${RESOURCE_FILES})
    file(RELATIVE_PATH REL_PATH ${CMAKE_CURRENT_SOURCE_DIR} ${FILE})
    string(REPLACE "\\" "/" REL_PATH ${REL_PATH})
    file(APPEND ${QRC_FILE} "        <file>${REL_PATH}</file>\n")
endforeach()
file(APPEND ${QRC_FILE} "    </qresource>\n</RCC>\n")

# i18n 翻译
set(TS_FILES
    translations/zh_CN.ts
    translations/en.ts
)

# Qt6 翻译
qt6_add_translations(BedrockMap
    TS_FILES ${TS_FILES}
)

# 创建可执行文件 - 不要使用 WIN32 参数
add_executable(BedrockMap
    ${SRC_FILES}
    ${APP_ICON_RESOURCE_WINDOWS}
    ${QRC_FILE}
)

# 链接库
target_link_libraries(BedrockMap PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    ${BEDROCK_LIBS}
)

# 预编译头 — 大幅减少重复编译 Qt 头文件的时间
target_precompile_headers(BedrockMap PRIVATE
    src/include/pch.h
)

target_compile_definitions(BedrockMap PRIVATE NOMINMAX)

# Windows 特定设置
if(WIN32)
    # 添加 Windows 系统库和链接选项
    # Debug 模式使用控制台子系统以显示 qDebug 输出，Release 使用 GUI 子系统
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

# 根据构建类型添加选项
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    target_compile_definitions(BedrockMap PRIVATE QT_NO_DEBUG_OUTPUT)
endif()