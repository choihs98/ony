cmake_minimum_required(VERSION 3.21)
project(ONY LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
endif()
set(CMAKE_AUTOMOC ON)

if(WIN32)
    enable_language(RC)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)
find_package(Qt6 OPTIONAL_COMPONENTS Test PrintSupport)

add_library(tvpcore STATIC
    src/Document.h src/Document.cpp
    src/Effects.h src/Effects.cpp
    src/Warp.h src/Warp.cpp
    src/Extras.h src/Extras.cpp
    src/Brush.h src/Brush.cpp
    src/Style.h src/Style.cpp
    src/Blend.h src/Blend.cpp
    src/VideoExport.h src/VideoExport.cpp
    src/Print.h src/Print.cpp
)
target_include_directories(tvpcore PUBLIC src)
target_link_libraries(tvpcore PUBLIC Qt6::Widgets)
if(TARGET Qt6::PrintSupport)
    target_link_libraries(tvpcore PUBLIC Qt6::PrintSupport)
    target_compile_definitions(tvpcore PUBLIC ONY_HAS_PRINT)
endif()

set(UI_SOURCES
    src/MainWindow.h src/MainWindow.cpp
    src/CanvasWidget.h src/CanvasWidget.cpp
    src/Panels.h src/Panels.cpp
    src/MorePanels.h src/MorePanels.cpp
    src/TimelineWidget.h src/TimelineWidget.cpp
    src/LayerMenu.h src/LayerMenu.cpp
    src/Shortcuts.h src/Shortcuts.cpp
    src/Privacy.h src/Privacy.cpp
)

# 로고 (resources/ony.png, resources/ony.ico) — 있으면 자동으로 포함
set(ONY_EXTRA_SOURCES "")
if(EXISTS ${CMAKE_SOURCE_DIR}/resources/ony.png)
    file(WRITE ${CMAKE_BINARY_DIR}/ony_logo.qrc
        "<RCC><qresource prefix=\"/\"><file alias=\"ony.png\">${CMAKE_SOURCE_DIR}/resources/ony.png</file><file alias=\"ony_logo.png\">${CMAKE_SOURCE_DIR}/resources/ony_logo.png</file></qresource></RCC>\n")
    set(CMAKE_AUTORCC ON)
    list(APPEND ONY_EXTRA_SOURCES ${CMAKE_BINARY_DIR}/ony_logo.qrc)
    add_compile_definitions(ONY_HAS_LOGO)
endif()
message(STATUS "ONY 로고: png=${CMAKE_SOURCE_DIR}/resources/ony.png ico=${CMAKE_SOURCE_DIR}/resources/ony.ico")
if(WIN32 AND EXISTS ${CMAKE_SOURCE_DIR}/resources/ony.ico)
    file(WRITE ${CMAKE_BINARY_DIR}/ony_icon.rc "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/resources/ony.ico\"\n")
    list(APPEND ONY_EXTRA_SOURCES ${CMAKE_BINARY_DIR}/ony_icon.rc)
endif()

add_executable(ony src/main.cpp ${UI_SOURCES} ${ONY_EXTRA_SOURCES})
target_link_libraries(ony PRIVATE tvpcore Qt6::Multimedia)

# 자체 테스트 (오프스크린) — Qt Test 가 있을 때만
if(TARGET Qt6::Test)
    add_executable(selftest tests/selftest.cpp ${UI_SOURCES})
    target_link_libraries(selftest PRIVATE tvpcore Qt6::Multimedia Qt6::Test)
endif()

if(WIN32)
    set_target_properties(ony PROPERTIES WIN32_EXECUTABLE ON)
endif()

if(TARGET Qt6::Test)
    add_executable(demo tests/demo.cpp ${UI_SOURCES})
    target_link_libraries(demo PRIVATE tvpcore Qt6::Multimedia Qt6::Test)
endif()

if(TARGET Qt6::Test AND EXISTS ${CMAKE_SOURCE_DIR}/tests/bench.cpp)
    add_executable(bench tests/bench.cpp ${UI_SOURCES})
    target_link_libraries(bench PRIVATE tvpcore Qt6::Multimedia Qt6::Test)
endif()
