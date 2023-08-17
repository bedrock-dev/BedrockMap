cmake_minimum_required(VERSION 3.5)
set(CMAKE_PREFIX_PATH "C:\\Qt\\Qt5.12.12\\5.12.12\\mingw73_64")

project(BedrockMap VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS -utf8)
# include bedrock-lib from absolute path
set(BEDROCK_LEVEL_ROOT "C:/Users/xhy/dev/bedrock-level")
set(BEDROCK_LIBS
        ${BEDROCK_LEVEL_ROOT}/.build/libbedrock-level.a
        ${BEDROCK_LEVEL_ROOT}/libs/libleveldb-release.a
        ${BEDROCK_LEVEL_ROOT}/libs/libz.a
        )
include_directories(
        ${BEDROCK_LEVEL_ROOT}/src/include
        ${BEDROCK_LEVEL_ROOT}/third

        ${PROJECT_SOURCE_DIR}/src/include
        "C:\\Users\\xhy\\dev\\Qt\\BedrockMap\\build\\BedrockMap_autogen\\include"

)


find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets Concurrent REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets Concurrent REQUIRED)

set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/icon.rc")

set(PROJECT_SOURCES

# SRC
        ${PROJECT_SOURCE_DIR}/src/main.cpp
        ${PROJECT_SOURCE_DIR}/src/mainwindow.cpp
        ${PROJECT_SOURCE_DIR}/src/mapwidget.cpp
        ${PROJECT_SOURCE_DIR}/src/asynclevelloader.cpp
        ${PROJECT_SOURCE_DIR}/src/chunkeditorwidget.cpp
        ${PROJECT_SOURCE_DIR}/src/chunksectionwidget.cpp
        ${PROJECT_SOURCE_DIR}/src/nbtwidget.cpp
        ${PROJECT_SOURCE_DIR}/src/config.cpp
        ${PROJECT_SOURCE_DIR}/src/iconmanager.cpp
        ${PROJECT_SOURCE_DIR}/src/aboutdialog.cpp
        ${PROJECT_SOURCE_DIR}/src/msg.cpp
        ${PROJECT_SOURCE_DIR}/src/mapitemeditor.cpp
        ${PROJECT_SOURCE_DIR}/src/renderfilterdialog.cpp
        # UI
        
        ${PROJECT_SOURCE_DIR}/src/chunkeditorwidget.ui
        ${PROJECT_SOURCE_DIR}/src/nbtwidget.ui
        ${PROJECT_SOURCE_DIR}/src/mainwindow.ui
        ${PROJECT_SOURCE_DIR}/src/renderfilterdialog.ui
        ${PROJECT_SOURCE_DIR}/src/mapitemeditor.ui
        ${PROJECT_SOURCE_DIR}/src/aboutdialog.ui
        icon.qrc
        )

add_executable(BedrockMap ${PROJECT_SOURCES} ${APP_ICON_RESOURCE_WINDOWS})


target_link_libraries(BedrockMap PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent ${BEDROCK_LIBS})

# set_property(TARGET BedrockMap PROPERTY WIN32_EXECUTABLE true)

if (QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(BedrockMap)
endif ()
