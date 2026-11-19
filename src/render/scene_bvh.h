#pragma once

#include "../scene/scene_loader.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lumatrace::render {

// 48-byte layout shared verbatim with triangle_lighting.hlsl.
struct GpuBvhNode {
    float minX, minY, minZ;
    std::uint32_t leftChild;
    float maxX, maxY, maxZ;
    std::uint32_t rightChild;
    std::uint32_t firstTriangle;
    std::uint32_t triangleCount;
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
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
    std::vector<GpuBvhNode> nodes;
    scene::Bounds bounds;
};

static_assert(sizeof(GpuBvhNode) == 48, "Keep the HLSL BVH node packing in sync");
static_assert(sizeof(GpuTriangle) == 128, "Keep the HLSL triangle packing in sync");

// Builds the reusable CPU acceleration structure once after scene loading.
// The same packed nodes/triangles are uploaded to the D3D11 compute shader.
SceneBvh buildSceneBvh(const scene::Scene& scene, std::uint32_t leafSize = 6);

} // namespace lumatrace::render
