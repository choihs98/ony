cmake_minimum_required(VERSION 3.21)
project(Ony LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)
find_package(Qt6 OPTIONAL_COMPONENTS Test)

add_library(tvpcore STATIC
    src/Document.h src/Document.cpp
    src/Effects.h src/Effects.cpp
    src/Warp.h src/Warp.cpp
    src/Extras.h src/Extras.cpp
    src/Brush.h src/Brush.cpp
)
target_include_directories(tvpcore PUBLIC src)
target_link_libraries(tvpcore PUBLIC Qt6::Widgets)

set(UI_SOURCES
    src/MainWindow.h src/MainWindow.cpp
    src/CanvasWidget.h src/CanvasWidget.cpp
    src/Panels.h src/Panels.cpp
    src/MorePanels.h src/MorePanels.cpp
    src/TimelineWidget.h src/TimelineWidget.cpp
)

add_executable(ony src/main.cpp ${UI_SOURCES})
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
