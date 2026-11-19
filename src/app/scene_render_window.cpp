#include "scene_render_window.h"

#include <QComboBox>
#include <QLabel>
#include <QPaintEngine>
#include <QPaintEvent>
#include <QShowEvent>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

#include <functional>
#include <utility>

namespace lumatrace::app {
namespace { constexpr int kViewWidth=960, kViewHeight=540; }

class SceneRenderWindow::Viewport final : public QWidget {
public:
    Viewport(const scene::Scene& scene,const render::SceneBvh& bvh,diagnostics::RunLogger& logger,
             QString imageOutputPath,QString& status,
             std::function<void(const QString&)> statusChanged,QWidget* parent=nullptr)
        : QWidget(parent),scene_(scene),bvh_(bvh),logger_(logger),imageOutputPath_(std::move(imageOutputPath)),status_(status),statusChanged_(std::move(statusChanged)) {
        setAttribute(Qt::WA_NativeWindow);setAttribute(Qt::WA_PaintOnScreen);setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);setFixedSize(kViewWidth,kViewHeight);
    }
    QPaintEngine* paintEngine() const override { return nullptr; }
    void setLightSamples(unsigned samples) {
#ifdef Q_OS_WIN
        if(renderer_)renderer_->setSamplesPerTriangle(samples);
#else
        Q_UNUSED(samples);
#endif
        status_=QStringLiteral("Rendu réglé sur %1 échantillon(s) lumineux par triangle.").arg(samples);
        if(statusChanged_)statusChanged_(status_);
        update();
    }
protected:
    void showEvent(QShowEvent* e) override {
        QWidget::showEvent(e);
#ifdef Q_OS_WIN
        if(!renderer_) {
            renderer_=std::make_unique<render::D3D11SceneRenderer>();
            if(!renderer_->initialize(reinterpret_cast<HWND>(winId()),scene_,bvh_,logger_,imageOutputPath_,status_)) {logger_.note(QStringLiteral("ERREUR D3D11 : ")+status_);renderer_.reset();}
        }
#else
        status_=QStringLiteral("Le rendu Direct3D 11 nécessite Windows.");
#endif
        if(statusChanged_)statusChanged_(status_);update();
    }
    void paintEvent(QPaintEvent*) override {
#ifdef Q_OS_WIN
        if(renderer_)renderer_->render(scene_,bvh_);
#endif
    }
private:
    const scene::Scene& scene_;const render::SceneBvh& bvh_;diagnostics::RunLogger& logger_;QString imageOutputPath_;QString& status_;
    std::function<void(const QString&)> statusChanged_;
#ifdef Q_OS_WIN
    std::unique_ptr<render::D3D11SceneRenderer> renderer_;
#endif
};

SceneRenderWindow::SceneRenderWindow(scene::Scene loadedScene,diagnostics::RunLogger& logger,QString imageOutputPath,QWidget* parent)
    :QMainWindow(parent),scene_(std::move(loadedScene)) {
    {auto timing=logger.scope(QStringLiteral("buildSceneBvh · SAH 12 bins → BVH4 (CPU)"));bvh_=render::buildSceneBvh(scene_);}
    logger.note(QStringLiteral("BVH CPU prêt · %1 triangles · %2 nœuds.").arg(bvh_.triangles.size()).arg(bvh_.nodes.size()));
    setWindowTitle(QStringLiteral("LumaTrace — %1").arg(scene_.name));
    viewport_=new Viewport(scene_,bvh_,logger,std::move(imageOutputPath),viewportStatus_,[this](const QString& text){statusBar()->showMessage(text);},this);
    auto* controls=addToolBar(QStringLiteral("Rendu"));controls->setMovable(false);
    controls->addWidget(new QLabel(QStringLiteral("Rayons par triangle : "),controls));
    auto* samples=new QComboBox(controls);samples->addItem(QStringLiteral("1 — HD 620"),1);samples->addItem(QStringLiteral("2"),2);samples->addItem(QStringLiteral("4 — qualité"),4);controls->addWidget(samples);
    connect(samples,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,samples](int){viewport_->setLightSamples(samples->currentData().toUInt());});
    setCentralWidget(viewport_);setFixedSize(kViewWidth,kViewHeight+controls->sizeHint().height()+statusBar()->sizeHint().height());
    statusBar()->showMessage(QStringLiteral("Scène chargée · %1 triangles · BVH CPU : %2 nœuds · profil 960×540")
        .arg(qulonglong(scene_.triangleCount)).arg(qulonglong(bvh_.nodes.size())));
}

SceneRenderWindow::~SceneRenderWindow()=default;
} // namespace lumatrace::app
