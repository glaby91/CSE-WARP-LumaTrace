#include "scene_render_window.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace lumatrace::app {
namespace { constexpr int kViewWidth=960, kViewHeight=540; constexpr float kLightStepDegrees=.5f; }

class SceneRenderWindow::Viewport final : public QWidget {
public:
    Viewport(const scene::Scene& scene,const render::SceneBvh& bvh,diagnostics::RunLogger& logger,
             QString imageOutputPath,QString& status,
             std::function<void(const QString&)> statusChanged,QWidget* parent=nullptr)
        : QWidget(parent),scene_(scene),bvh_(bvh),logger_(logger),imageOutputPath_(std::move(imageOutputPath)),status_(status),statusChanged_(std::move(statusChanged)) {
        setAttribute(Qt::WA_NativeWindow);setAttribute(Qt::WA_PaintOnScreen);setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);setFixedSize(kViewWidth,kViewHeight);
        setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);
        renderTimer_=new QTimer(this);renderTimer_->setInterval(1);
        connect(renderTimer_,&QTimer::timeout,this,qOverload<>(&Viewport::update));
    }
    QPaintEngine* paintEngine() const override { return nullptr; }
    void setLightSamples(unsigned samples) {
#ifdef Q_OS_WIN
        if(renderer_)renderer_->setSamplesPerTriangle(samples);
#else
        Q_UNUSED(samples);
#endif
        status_=QStringLiteral("Rendu continu réglé sur %1 échantillon(s) lumineux par triangle.").arg(samples);
        if(statusChanged_)statusChanged_(status_);
        update();
    }
    void toggleLightOrbit() {
        orbitEnabled_=!orbitEnabled_;
        status_=orbitEnabled_
            ?QStringLiteral("Orbite lumineuse active · pas de 0,5° par image.")
            :QStringLiteral("Orbite lumineuse en pause · le rendu continue.");
        if(statusChanged_)statusChanged_(status_);
    }
    bool lightOrbitEnabled() const { return orbitEnabled_; }
protected:
    void showEvent(QShowEvent* e) override {
        QWidget::showEvent(e);
#ifdef Q_OS_WIN
        if(!renderer_) {
            renderer_=std::make_unique<render::D3D11SceneRenderer>();
            if(!renderer_->initialize(reinterpret_cast<HWND>(winId()),scene_,bvh_,logger_,imageOutputPath_,status_)) {
                logger_.note(QStringLiteral("ERREUR D3D11 : ")+status_);renderer_.reset();
            }
        }
#else
        status_=QStringLiteral("Le rendu Direct3D 11 nécessite Windows.");
#endif
        if(statusChanged_)statusChanged_(status_);
        if(renderer_)renderTimer_->start();
        update();
    }
    void paintEvent(QPaintEvent*) override {
#ifdef Q_OS_WIN
        if(renderer_) {
            renderer_->setOrbitAngleDegrees(orbitAngleDegrees_);
            renderer_->render(scene_,bvh_);
            if(orbitEnabled_) {
                orbitAngleDegrees_+=kLightStepDegrees;
                if(orbitAngleDegrees_>=360.f)orbitAngleDegrees_-=360.f;
            }
        }
#endif
    }
    void mousePressEvent(QMouseEvent* event) override {
        if(event->button()==Qt::LeftButton) {
            setFocus(Qt::MouseFocusReason);
            if(!mouseLookEnabled_) {
                mouseLookEnabled_=true;
                setCursor(Qt::BlankCursor);grabMouse();
                QCursor::setPos(mapToGlobal(rect().center()));
                status_=QStringLiteral("Souris capturée · glisser pour tourner autour de la voiture · Échap libère le curseur.");
                if(statusChanged_)statusChanged_(status_);
            }
            event->accept();return;
        }
        QWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if(!mouseLookEnabled_) { QWidget::mouseMoveEvent(event);return; }
        const QPoint centerGlobal=mapToGlobal(rect().center());
        const QPoint current=event->globalPosition().toPoint();
        const QPoint delta=current-centerGlobal;
        if(!delta.isNull()) {
            cameraYawDegrees_+=float(delta.x())*.35f;
            cameraPitchDegrees_=std::clamp(cameraPitchDegrees_-float(delta.y())*.35f,-70.f,70.f);
#ifdef Q_OS_WIN
            if(renderer_)renderer_->setCameraOrbitDegrees(cameraYawDegrees_,cameraPitchDegrees_);
#endif
            QCursor::setPos(centerGlobal);
        }
        event->accept();
    }
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Escape&&mouseLookEnabled_) {
            releaseMouseLook();event->accept();return;
        }
        QWidget::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent* event) override {
        if(mouseLookEnabled_)releaseMouseLook();
        QWidget::focusOutEvent(event);
    }
private:
    void releaseMouseLook() {
        mouseLookEnabled_=false;releaseMouse();unsetCursor();
        status_=QStringLiteral("Curseur libéré · rendu et orbite des lumières continuent.");
        if(statusChanged_)statusChanged_(status_);
    }
    const scene::Scene& scene_;const render::SceneBvh& bvh_;diagnostics::RunLogger& logger_;QString imageOutputPath_;QString& status_;
    std::function<void(const QString&)> statusChanged_;
    QTimer* renderTimer_=nullptr;float orbitAngleDegrees_=0.f,cameraYawDegrees_=0.f,cameraPitchDegrees_=0.f;
    bool orbitEnabled_=true,mouseLookEnabled_=false;
#ifdef Q_OS_WIN
    std::unique_ptr<render::D3D11SceneRenderer> renderer_;
#endif
};

SceneRenderWindow::SceneRenderWindow(scene::Scene loadedScene,diagnostics::RunLogger& logger,QString imageOutputPath,QWidget* parent)
    :QMainWindow(parent),scene_(std::move(loadedScene)) {
    {auto timing=logger.scope(QStringLiteral("buildSceneBvh · SAH 12 bins → BVH4 (CPU)"));bvh_=render::buildSceneBvh(scene_);}
    logger.note(QStringLiteral("BVH CPU prêt · %1 triangles · %2 nœuds.").arg(bvh_.triangles.size()).arg(bvh_.nodes.size()));
    setWindowTitle(QStringLiteral("LumaTrace %1 — %2").arg(QCoreApplication::applicationVersion(),scene_.name));
    viewport_=new Viewport(scene_,bvh_,logger,std::move(imageOutputPath),viewportStatus_,[this](const QString& text){statusBar()->showMessage(text);},this);
    auto* controls=addToolBar(QStringLiteral("Rendu"));controls->setMovable(false);
    controls->addWidget(new QLabel(QStringLiteral("Rayons par triangle : "),controls));
    auto* samples=new QComboBox(controls);samples->addItem(QStringLiteral("1 — rapide"),1);samples->addItem(QStringLiteral("2"),2);samples->addItem(QStringLiteral("4 — qualité"),4);controls->addWidget(samples);
    connect(samples,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,samples](int){viewport_->setLightSamples(samples->currentData().toUInt());});
    auto* orbitButton=new QPushButton(QStringLiteral("Pause orbite lumière"),controls);controls->addWidget(orbitButton);
    connect(orbitButton,&QPushButton::clicked,this,[this,orbitButton]{
        viewport_->toggleLightOrbit();
        orbitButton->setText(viewport_->lightOrbitEnabled()?QStringLiteral("Pause orbite lumière"):QStringLiteral("Reprendre orbite lumière"));
    });
    auto* help=menuBar()->addMenu(QStringLiteral("Aide"));
    auto* about=help->addAction(QStringLiteral("À propos de LumaTrace"));
    connect(about,&QAction::triggered,this,[this]{
        const QString text=QStringLiteral(
            "LumaTrace %1\n\n"
            "Rendu continu Direct3D 11 : l’image est calculée dans cette fenêtre et présentée immédiatement par le double-buffer. "
            "Aucune séquence vidéo ni seconde caméra n’est calculée.\n\n"
            "Éclairage : deux sources colorées de même intensité tournent en sens opposés sur des cercles inclinés. "
            "Leur angle avance de 0,5° à chaque image rendue (720 images pour un tour complet). Le bouton de la barre d’outils met leur mouvement en pause ou le reprend.\n\n"
            "Souris : cliquez dans l’image puis glissez pour faire orbiter le point de vue autour de la voiture. "
            "Échap libère le curseur pour Windows ; le rendu continue dans la fenêtre. Cliquez à nouveau pour reprendre la rotation à la souris.\n\n"
            "Rayons par triangle : 1 privilégie la vitesse, 2 est intermédiaire, 4 lisse davantage l’éclairage des surfaces au prix du calcul. "
            "Ce réglage concerne l’échantillonnage des lumières, pas le MSAA.\n\n"
            "BVH4 : structure hiérarchique qui accélère les tests d’intersection des rayons d’ombre et de réflexion. "
            "Le rendu utilise aussi le raster PBR pour les matériaux et les textures. Les réflexions ne font pas de rebonds multiples.\n\n"
            "Scène chargée : %2 · %3 triangles.")
            .arg(QCoreApplication::applicationVersion(),scene_.name).arg(qulonglong(scene_.triangleCount));
        QMessageBox::about(this,QStringLiteral("À propos de LumaTrace"),text);
    });
    setCentralWidget(viewport_);
    setFixedSize(kViewWidth,kViewHeight+controls->sizeHint().height()+menuBar()->sizeHint().height()+statusBar()->sizeHint().height());
    statusBar()->showMessage(QStringLiteral("Rendu continu · orbite lumineuse active · cliquer/glisser pour tourner la vue."));
}

SceneRenderWindow::~SceneRenderWindow()=default;
} // namespace lumatrace::app
