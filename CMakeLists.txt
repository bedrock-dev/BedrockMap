cmake_minimum_required(VERSION 3.5)
set(CMAKE_PREFIX_PATH "C:\\Qt\\Qt5.12.12\\5.12.12\\mingw73_64")

project(BedrockMap VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# include bedrock-lib from absolute path
set(BEDROCK_LEVEL_ROOT "C:/Users/xhy/dev/bedrock-level")
set(BEDROCK_LIBS
        ${BEDROCK_LEVEL_ROOT}/.build-debug/libbedrock-level.a
        ${BEDROCK_LEVEL_ROOT}/libs/libleveldb.a
        ${BEDROCK_LEVEL_ROOT}/.build-debug/libnbt-cpp.a
        ${BEDROCK_LEVEL_ROOT}/libs/libz.a
        )
include_directories(
        ${BEDROCK_LEVEL_ROOT}/src/include
        ${BEDROCK_LEVEL_ROOT}/third
)


find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets Concurrent REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets Concurrent REQUIRED)


set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        mapwidget.h
        mapwidget.cpp
        world.h
        world.cpp
        asynclevelloader.h
        asynclevelloader.cpp
        icon.qrc
        chunkeditorwidget.h
        chunkeditorwidget.cpp
        chunkeditorwidget.ui
        chunksectionwidget.h
        chunksectionwidget.cpp
        nbtwidget.h
        nbtwidget.cpp
        nbtwidget.ui
        config.h
        #        treeitem.cpp
        #        treeitem.h
        #        treemodel.cpp
        #        treemodel.h
        iconmanager.cpp
        iconmanager.h

        qdarkstyle/dark/style.qrc
        qdarkstyle/light/style.qrc

        )

add_executable(BedrockMap ${PROJECT_SOURCES})


target_link_libraries(BedrockMap PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent ${BEDROCK_LIBS})

set_target_properties(BedrockMap PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        )

if (QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(BedrockMap)
endif ()
