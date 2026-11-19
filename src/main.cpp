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
#include <QTextStream>
#include <QTimer>

#include <memory>
#include <cstdint>
#include <thread>
#include <utility>

int main(int argc, char* argv[]) {
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Symbiose Environnement"));
    QCoreApplication::setApplicationName(QStringLiteral("CSE-WARP-LumaTrace"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Charge une scène LumaTrace et ouvre le viewport Direct3D 11."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("scene"),
        QStringLiteral("Manifeste lumatrace-scene-v1 (par défaut : scène Porsche 930)."),
        QStringLiteral("scene.json"));
    const QCommandLineOption samplesOption(QStringLiteral("samples"),QStringLiteral("Échantillons lumineux par triangle (1, 2 ou 4)."),QStringLiteral("count"),QStringLiteral("1"));
    const QCommandLineOption outputDirOption(QStringLiteral("output-dir"),QStringLiteral("Répertoire racine des journaux et images exportés."),QStringLiteral("directory"));
    const QCommandLineOption exitAfterRenderOption(QStringLiteral("exit-after-render"),QStringLiteral("Ferme automatiquement après le premier rendu et son export."));
    parser.addOption(samplesOption);parser.addOption(outputDirOption);parser.addOption(exitAfterRenderOption);
    parser.process(app);
    bool samplesOk=false;const int samplesValue=parser.value(samplesOption).toInt(&samplesOk);
    if(!samplesOk||(samplesValue!=1&&samplesValue!=2&&samplesValue!=4)){
        QTextStream(stderr)<<"--samples doit valoir 1, 2 ou 4.\n";return 2;
    }
    const auto paths=lumatrace::diagnostics::makeRunPaths(parser.value(outputDirOption));
    const bool exitAfterRender=parser.isSet(exitAfterRenderOption);
    lumatrace::app::ProgressWindow progress;
    progress.show();
    QPointer<lumatrace::app::ProgressWindow> progressGuard(&progress);
    lumatrace::diagnostics::RunLogger logger(paths.logFile,[progressGuard](const QString& line){
        if(!progressGuard)return;
        QMetaObject::invokeMethod(progressGuard.data(),[progressGuard,line]{if(progressGuard)progressGuard->appendLogLine(line);},Qt::QueuedConnection);
    });
    auto appTiming=logger.scope(QStringLiteral("application · démarrage jusqu'à fermeture"));
    logger.note(QStringLiteral("Journal : ")+paths.logFile);
    logger.note(QStringLiteral("Export image prévu : ")+paths.imageFile);
    logger.note(QStringLiteral("Chronométrage : temps mur côté CPU pour les scopes ; les dispatchs GPU restent asynchrones."));

    QString manifest = parser.positionalArguments().isEmpty()
        ? QCoreApplication::applicationDirPath() + QStringLiteral("/assets/vehicles/porsche_930/scene/scene.json")
        : parser.positionalArguments().front();
    if (parser.positionalArguments().isEmpty() && !QFileInfo::exists(manifest))
        manifest = QStringLiteral("assets/vehicles/porsche_930/scene/scene.json");
    manifest = QFileInfo(manifest).absoluteFilePath();
    std::unique_ptr<lumatrace::app::SceneRenderWindow> renderWindow;
    auto loaded=std::make_shared<lumatrace::scene::LoadResult>();
    std::thread loader([&logger,&progress,&renderWindow,&app,loaded,manifest,imagePath=paths.imageFile,samples=std::uint32_t(samplesValue),exitAfterRender]{
        {auto timing=logger.scope(QStringLiteral("chargement scène · manifest → géométrie et textures"));*loaded=lumatrace::scene::loadScene(manifest,&logger);}
        if(!*loaded)logger.note(QStringLiteral("ÉCHEC chargement scène : ")+loaded->error);
        QMetaObject::invokeMethod(&progress,[&logger,&progress,&renderWindow,&app,loaded,imagePath,samples,exitAfterRender]{
            if(!*loaded){
                progress.setAttribute(Qt::WA_QuitOnClose,true);
                if(exitAfterRender)QTimer::singleShot(0,&app,[&app]{app.exit(3);});
                else QMessageBox::critical(&progress,QStringLiteral("Chargement de la scène impossible"),loaded->error);
                return;
            }
            renderWindow=std::make_unique<lumatrace::app::SceneRenderWindow>(std::move(loaded->scene),logger,imagePath,samples,exitAfterRender);
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
