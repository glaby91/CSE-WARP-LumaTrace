#include "app/scene_render_window.h"
#include "app/progress_window.h"
#include "diagnostics/run_logger.h"
#include "scene/scene_loader.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QMetaObject>
#include <QMessageBox>
#include <QPointer>

#include <memory>
#include <thread>
#include <utility>

int main(int argc, char* argv[]) {
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Symbiose Environnement"));
    QCoreApplication::setApplicationName(QStringLiteral("CSE-WARP-LumaTrace"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LUMATRACE_VERSION));

    lumatrace::app::ProgressWindow progress;
    progress.show();
    const auto paths=lumatrace::diagnostics::makeRunPaths();
    QPointer<lumatrace::app::ProgressWindow> progressGuard(&progress);
    lumatrace::diagnostics::RunLogger logger(paths.logFile,[progressGuard](const QString& line){
        if(!progressGuard)return;
        QMetaObject::invokeMethod(progressGuard.data(),[progressGuard,line]{if(progressGuard)progressGuard->appendLogLine(line);},Qt::QueuedConnection);
    });
    auto appTiming=logger.scope(QStringLiteral("application · démarrage jusqu'à fermeture"));
    logger.note(QStringLiteral("Journal : ")+paths.logFile);
    logger.note(QStringLiteral("Export image prévu : ")+paths.imageFile);
    logger.note(QStringLiteral("Chronométrage : temps mur côté CPU pour les scopes ; les dispatchs GPU restent asynchrones."));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Charge une scène LumaTrace et ouvre le viewport Direct3D 11."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("scene"),
        QStringLiteral("Manifeste lumatrace-scene-v1 (par défaut : scène Porsche 930)."),
        QStringLiteral("scene.json"));
    parser.process(app);

    QString manifest = parser.positionalArguments().isEmpty()
        ? QCoreApplication::applicationDirPath() + QStringLiteral("/assets/vehicles/porsche_930/scene/scene.json")
        : parser.positionalArguments().front();
    if (parser.positionalArguments().isEmpty() && !QFileInfo::exists(manifest))
        manifest = QStringLiteral("assets/vehicles/porsche_930/scene/scene.json");
    manifest = QFileInfo(manifest).absoluteFilePath();
    std::unique_ptr<lumatrace::app::SceneRenderWindow> renderWindow;
    auto loaded=std::make_shared<lumatrace::scene::LoadResult>();
    std::thread loader([&logger,&progress,&renderWindow,loaded,manifest,imagePath=paths.imageFile]{
        {auto timing=logger.scope(QStringLiteral("chargement scène · manifest → géométrie et textures"));*loaded=lumatrace::scene::loadScene(manifest,&logger);}
        if(!*loaded)logger.note(QStringLiteral("ÉCHEC chargement scène : ")+loaded->error);
        QMetaObject::invokeMethod(&progress,[&logger,&progress,&renderWindow,loaded,imagePath]{
            if(!*loaded){
                progress.setAttribute(Qt::WA_QuitOnClose,true);
                QMessageBox::critical(&progress,QStringLiteral("Chargement de la scène impossible"),loaded->error);
                return;
            }
            renderWindow=std::make_unique<lumatrace::app::SceneRenderWindow>(std::move(loaded->scene),logger,imagePath);
            renderWindow->show();
            logger.note(QStringLiteral("Fenêtre de rendu ouverte ; le journal reste affiché dans la fenêtre console."));
        },Qt::QueuedConnection);
    });
    const int exitCode=app.exec();
    if(loader.joinable())loader.join();
    logger.note(QStringLiteral("Application fermée."));
    renderWindow.reset();
    return exitCode;
}
