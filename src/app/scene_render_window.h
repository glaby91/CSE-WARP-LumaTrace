#pragma once

#include "../render/scene_bvh.h"
#include "../scene/scene_loader.h"
#include "../diagnostics/run_logger.h"
#include <QMainWindow>
#ifdef Q_OS_WIN
#include "../render/d3d11_scene_renderer.h"
#endif

#include <QString>

#include <memory>

namespace lumatrace::app {

class SceneRenderWindow final : public QMainWindow {
public:
    explicit SceneRenderWindow(scene::Scene loadedScene, diagnostics::RunLogger& logger,
                               QString imageOutputPath, QWidget* parent = nullptr);
    ~SceneRenderWindow() override;

private:
    class Viewport;
    scene::Scene scene_;
    render::SceneBvh bvh_;
    Viewport* viewport_ = nullptr;
    QString viewportStatus_;
};

} // namespace lumatrace::app
