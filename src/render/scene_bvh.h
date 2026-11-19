#pragma once

#include "../scene/scene_loader.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lumatrace::render {

// 32-byte child descriptor shared verbatim with triangle_lighting.hlsl.
// triangleCount == 0 means reference points to another BVH4 node; otherwise
// reference is the first triangle in the leaf range.
struct GpuBvh4Child {
    float minX, minY, minZ;
    std::uint32_t reference;
    float maxX, maxY, maxZ;
    std::uint32_t triangleCount;
};

// Four child descriptors are stored together so the compute shader can test
// their bounds from one structured-buffer node load.
struct GpuBvh4Node {
    GpuBvh4Child children[4]{};
    std::uint32_t childCount = 0;
    std::uint32_t padding[3]{};
};

struct GpuTriangle {
    scene::Vec3 p0; std::uint32_t material;
    scene::Vec3 p1; std::uint32_t triangleId;
    scene::Vec3 p2; std::uint32_t padding0 = 0;
    scene::Vec3 n0; std::uint32_t padding1 = 0;
    scene::Vec3 n1; std::uint32_t padding2 = 0;
    scene::Vec3 n2; std::uint32_t padding3 = 0;
    scene::Vec2 uv0; scene::Vec2 uv1;
    scene::Vec2 uv2; scene::Vec2 padding4;
};

struct SceneBvh {
    std::vector<GpuTriangle> triangles; // stable global triangle IDs
    std::vector<std::uint32_t> triangleOrder; // leaf ranges into triangles
    std::vector<GpuBvh4Node> nodes;
    scene::Bounds bounds;
};

static_assert(sizeof(GpuBvh4Child) == 32, "Keep the HLSL BVH4 child packing in sync");
static_assert(sizeof(GpuBvh4Node) == 144, "Keep the HLSL BVH4 node packing in sync");
static_assert(sizeof(GpuTriangle) == 128, "Keep the HLSL triangle packing in sync");

// Builds a binned-SAH binary hierarchy, then collapses it to four-way nodes.
// The packed triangles and BVH are uploaded once after scene loading.
SceneBvh buildSceneBvh(const scene::Scene& scene, std::uint32_t leafSize = 6);

} // namespace lumatrace::render
