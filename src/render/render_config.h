#pragma once

#include <cstdint>

namespace lumatrace::render {

struct RenderConfig {
    // Conservative first-run profile for Intel HD 620 shared-memory graphics.
    std::uint32_t width = 960;
    std::uint32_t height = 540;
    std::uint32_t triangleLightSamples = 1; // user-selectable 1..4
    bool zPrepass = true;
    bool triangleIdTarget = true;
    bool msaa = false;
    bool vsync = true;
    bool rasterTextures = true;
    bool normalMapping = true;
    bool rayReflections = true;
};

} // namespace lumatrace::render
