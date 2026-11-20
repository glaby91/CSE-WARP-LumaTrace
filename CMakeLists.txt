cmake_minimum_required(VERSION 3.24)
project(CSE_WARP_LumaTrace VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

add_library(LumaTraceSceneLoader STATIC
    src/scene/scene_loader.cpp
    src/scene/scene_loader.h
    src/diagnostics/run_logger.cpp
    src/diagnostics/run_logger.h
    src/render/scene_bvh.cpp
    src/render/scene_bvh.h
    src/render/render_config.h
)
target_include_directories(LumaTraceSceneLoader PUBLIC src)
target_link_libraries(LumaTraceSceneLoader PUBLIC Qt6::Core Qt6::Gui)
target_compile_definitions(LumaTraceSceneLoader PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

add_executable(CSE_WARP_LumaTrace
    src/main.cpp
    src/app/scene_render_window.cpp
    src/app/scene_render_window.h
    src/app/progress_window.cpp
    src/app/progress_window.h
)
target_link_libraries(CSE_WARP_LumaTrace PRIVATE LumaTraceSceneLoader Qt6::Widgets)
target_compile_definitions(CSE_WARP_LumaTrace PRIVATE LUMATRACE_VERSION=\"${PROJECT_VERSION}\")
if(WIN32)
    target_sources(CSE_WARP_LumaTrace PRIVATE src/render/d3d11_scene_renderer.cpp src/render/d3d11_scene_renderer.h)
    target_link_libraries(CSE_WARP_LumaTrace PRIVATE d3d11 dxgi d3dcompiler)
    add_custom_command(TARGET CSE_WARP_LumaTrace POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders"
            "$<TARGET_FILE_DIR:CSE_WARP_LumaTrace>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/vehicles/porsche_930/scene"
            "$<TARGET_FILE_DIR:CSE_WARP_LumaTrace>/assets/vehicles/porsche_930/scene"
        COMMENT "Copying LumaTrace shaders and scene assets beside the executable")
endif()

if(MSVC)
    target_compile_options(LumaTraceSceneLoader PRIVATE /W4 /permissive-)
else()
    target_compile_options(LumaTraceSceneLoader PRIVATE -Wall -Wextra -Wpedantic)
endif()
