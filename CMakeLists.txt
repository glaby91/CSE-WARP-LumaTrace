cmake_minimum_required(VERSION 3.24)
project(CSE_WARP_LumaTrace VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui)

add_library(LumaTraceSceneLoader STATIC
    src/scene/scene_loader.cpp
    src/scene/scene_loader.h
)
target_include_directories(LumaTraceSceneLoader PUBLIC src)
target_link_libraries(LumaTraceSceneLoader PUBLIC Qt6::Core Qt6::Gui)
target_compile_definitions(LumaTraceSceneLoader PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

if(MSVC)
    target_compile_options(LumaTraceSceneLoader PRIVATE /W4 /permissive-)
else()
    target_compile_options(LumaTraceSceneLoader PRIVATE -Wall -Wextra -Wpedantic)
endif()
