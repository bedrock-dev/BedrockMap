cmake_minimum_required(VERSION 3.5)


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

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS -utf8)

#bedorck-level
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
        # for clangd
        ${CMAKE_CURRENT_BINARY_DIR}/BedrockMap_autogen/include
)

#qt
find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets Concurrent OpenGL REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets Concurrent OpenGL REQUIRED)

set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/icon.rc")

file(GLOB_RECURSE SRC_FILES
        src/*.ui
        src/*.cpp
        src/include/*.h
)


#qrc generation
file(GLOB_RECURSE RESOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.jpg
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.ttf
)

set(QRC_FILE ${PROJECT_SOURCE_DIR}/res.qrc)
file(WRITE ${QRC_FILE} "<RCC>\n    <qresource prefix=\"/\">\n")
foreach(FILE ${RESOURCE_FILES})
    file(RELATIVE_PATH REL_PATH ${CMAKE_CURRENT_SOURCE_DIR} ${FILE})
    string(REPLACE "\\" "/" REL_PATH ${REL_PATH})
    file(APPEND ${QRC_FILE} "        <file>${REL_PATH}</file>\n")
endforeach()
file(APPEND ${QRC_FILE} "    </qresource>\n</RCC>\n")
#qt5_add_resources(RESOURCES_RCC ${QRC_FILE})

add_executable(BedrockMap
                ${SRC_FILES}
                ${APP_ICON_RESOURCE_WINDOWS} 
                ${QRC_FILE})

message(STATUS "Build type: " ${CMAKE_BUILD_TYPE})
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
 message(STATUS "Use no console mode")
 target_link_libraries(BedrockMap PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Concurrent
        -static-libgcc -static-libstdc++
        -mwindows
        ${BEDROCK_LIBS}) 
else()
  target_link_libraries(BedrockMap PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Concurrent
        Qt${QT_VERSION_MAJOR}::OpenGL
        -static-libgcc -static-libstdc++
        ${BEDROCK_LIBS})
endif()

if (QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(BedrockMap)
endif ()
