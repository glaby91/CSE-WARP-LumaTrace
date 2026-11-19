#pragma once

#include "scene_bvh.h"
#include "../scene/scene_loader.h"
#include "../diagnostics/run_logger.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <memory>
#include <cstdint>
#include <QString>
#include <QImage>

namespace lumatrace::render {

// Owns the complete D3D11 pipeline for one native Qt viewport.
class D3D11SceneRenderer final {
public:
    D3D11SceneRenderer();
    ~D3D11SceneRenderer();
    D3D11SceneRenderer(const D3D11SceneRenderer&) = delete;
    D3D11SceneRenderer& operator=(const D3D11SceneRenderer&) = delete;

    bool initialize(HWND hwnd, const scene::Scene& scene, const SceneBvh& bvh,
                    diagnostics::RunLogger& logger, QString imageOutputPath, QString& status);
    void render(const scene::Scene& scene, const SceneBvh& bvh, QImage* capturedFrame = nullptr);
    void setOrbitAngleDegrees(float degrees);
    void setSamplesPerTriangle(std::uint32_t samples);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lumatrace::render
#endif
