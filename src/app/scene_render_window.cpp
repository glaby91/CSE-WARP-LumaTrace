#include "scene_render_window.h"

#include <QBuffer>
#include <QByteArray>
#include <QComboBox>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QPaintEngine>
#include <QPaintEvent>
#include <QPixmap>
#include <QProgressBar>
#include <QResizeEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QWidget>

#include <functional>
#include <utility>
#include <vector>

namespace lumatrace::app {
namespace { constexpr int kViewWidth=960, kViewHeight=540; constexpr int kOrbitFrames=720; }

class OrbitPlaybackWindow final : public QMainWindow {
public:
    explicit OrbitPlaybackWindow(std::vector<QByteArray> frames,QWidget* parent=nullptr)
        :QMainWindow(parent),frames_(std::move(frames)) {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QStringLiteral("LumaTrace — lecture en boucle · 0,5°"));
        image_=new QLabel(this);image_->setAlignment(Qt::AlignCenter);setCentralWidget(image_);
        resize(kViewWidth,kViewHeight);
        timer_=new QTimer(this);timer_->setInterval(33);
        connect(timer_,&QTimer::timeout,this,[this]{showNext();});
        showNext();timer_->start();
    }
protected:
    void resizeEvent(QResizeEvent* e) override {QMainWindow::resizeEvent(e);showNext(false);}
private:
    void showNext(bool advance=true) {
        if(frames_.empty())return;
        if(advance)index_=(index_+1)%frames_.size();
        const QImage frame=QImage::fromData(frames_[index_],"JPG");
        if(!frame.isNull())image_->setPixmap(QPixmap::fromImage(frame).scaled(image_->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
        statusBar()->showMessage(QStringLiteral("Image %1 / %2 · angle %3°").arg(qulonglong(index_+1)).arg(qulonglong(frames_.size())).arg(index_*.5,0,'f',1));
    }
    std::vector<QByteArray> frames_;QLabel* image_=nullptr;QTimer* timer_=nullptr;size_t index_=0;
};

class SceneRenderWindow::Viewport final : public QWidget {
public:
    using Progress=std::function<void(int,double,double)>;
    using Complete=std::function<void(std::vector<QByteArray>)>;
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
    void startOrbitCapture(Progress progress,Complete complete) {
#ifdef Q_OS_WIN
        if(!renderer_||capturing_)return;
        progress_=std::move(progress);complete_=std::move(complete);frames_.clear();frames_.reserve(kOrbitFrames);
        frameIndex_=0;elapsed_.start();capturing_=true;captureScheduled_=true;
        status_=QStringLiteral("Calcul de l’orbite lumineuse · préparation de 720 images.");
        if(statusChanged_)statusChanged_(status_);
        update();
#else
        Q_UNUSED(progress);Q_UNUSED(complete);
        status_=QStringLiteral("Le calcul des images nécessite Windows / Direct3D 11.");
        if(statusChanged_)statusChanged_(status_);
#endif
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
        if(renderer_) {
            const bool captureThisFrame=capturing_&&captureScheduled_;
            captureScheduled_=false;
            QImage frame;
            renderer_->render(scene_,bvh_,captureThisFrame?&frame:nullptr);
            if(captureThisFrame) {
                QByteArray encoded;QBuffer buffer(&encoded);buffer.open(QIODevice::WriteOnly);frame.save(&buffer,"JPG",88);
                frames_.push_back(std::move(encoded));++frameIndex_;
                const double elapsed=elapsed_.elapsed()/1000.0;
                const double eta=frameIndex_>0?elapsed*(kOrbitFrames-frameIndex_)/frameIndex_:0.0;
                double mb=0.0;for(const auto& f:frames_)mb+=f.size();mb/=1024.0*1024.0;
                if(progress_)progress_(frameIndex_,mb,eta);
                if(frameIndex_>=kOrbitFrames) {
                    capturing_=false;status_=QStringLiteral("Calcul terminé · 720 images en buffer · %1 Mo.").arg(mb,0,'f',1);
                    if(statusChanged_)statusChanged_(status_);
                    auto done=std::move(complete_);auto frames=std::move(frames_);
                    if(done)done(std::move(frames));
                } else {
                    renderer_->setOrbitAngleDegrees(frameIndex_*.5f);
                    captureScheduled_=true;
                    QTimer::singleShot(0,this,[this]{update();});
                }
            }
        }
#endif
    }
private:
    const scene::Scene& scene_;const render::SceneBvh& bvh_;diagnostics::RunLogger& logger_;QString imageOutputPath_;QString& status_;
    std::function<void(const QString&)> statusChanged_;
    Progress progress_;Complete complete_;QElapsedTimer elapsed_;std::vector<QByteArray> frames_;int frameIndex_=0;bool capturing_=false,captureScheduled_=false;
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
    auto* orbitButton=new QPushButton(QStringLiteral("Calculer l’orbite (720 images)"),controls);controls->addWidget(orbitButton);
    auto* orbitProgress=new QProgressBar(controls);orbitProgress->setRange(0,kOrbitFrames);orbitProgress->setValue(0);orbitProgress->setFixedWidth(165);orbitProgress->setFormat(QStringLiteral("%v / %m"));controls->addWidget(orbitProgress);
    auto* orbitLabel=new QLabel(QStringLiteral("prêt"),controls);controls->addWidget(orbitLabel);
    connect(orbitButton,&QPushButton::clicked,this,[this,orbitButton,orbitProgress,orbitLabel]{
        orbitButton->setEnabled(false);orbitProgress->setValue(0);orbitLabel->setText(QStringLiteral("démarrage"));
        viewport_->startOrbitCapture(
            [orbitProgress,orbitLabel](int done,double mb,double eta){
                orbitProgress->setValue(done);
                orbitLabel->setText(QStringLiteral("%1% · buffer %2 Mo · reste ~%3 s · angle %4°")
                    .arg(int(done*100.0/kOrbitFrames)).arg(mb,0,'f',1).arg(int(eta)).arg((done-1)*.5,0,'f',1));
            },
            [this,orbitButton,orbitLabel](std::vector<QByteArray> frames){
                orbitButton->setEnabled(true);orbitLabel->setText(QStringLiteral("lecture en boucle"));
                auto* playback=new OrbitPlaybackWindow(std::move(frames),this);playback->show();
            });
    });
    setCentralWidget(viewport_);setFixedSize(kViewWidth,kViewHeight+controls->sizeHint().height()+statusBar()->sizeHint().height());
    statusBar()->showMessage(QStringLiteral("Scène chargée · %1 triangles · BVH CPU : %2 nœuds · profil 960×540")
        .arg(qulonglong(scene_.triangleCount)).arg(qulonglong(bvh_.nodes.size())));
}

SceneRenderWindow::~SceneRenderWindow()=default;
} // namespace lumatrace::app
