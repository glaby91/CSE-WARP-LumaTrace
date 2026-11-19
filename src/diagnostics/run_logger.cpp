#include "run_logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

namespace lumatrace::diagnostics {
namespace { thread_local int g_scopeDepth=0; }

RunLogger::Scope::Scope(RunLogger& logger,QString name)
    :logger_(&logger),name_(std::move(name)),depth_(g_scopeDepth) {
    timer_.start();logger_->writeLine(depth_,QStringLiteral("START  ")+name_);++g_scopeDepth;
}
RunLogger::Scope::Scope(Scope&& other) noexcept
    :logger_(other.logger_),name_(std::move(other.name_)),timer_(other.timer_),depth_(other.depth_) { other.logger_=nullptr; }
RunLogger::Scope::~Scope(){
    if(!logger_)return;
    g_scopeDepth=depth_;
    logger_->writeLine(depth_,QStringLiteral("END    %1 — %2 ms").arg(name_).arg(timer_.elapsed()));
}

RunLogger::RunLogger(QString filePath,Sink sink):filePath_(std::move(filePath)),file_(filePath_),sink_(std::move(sink)) {
    QDir().mkpath(QFileInfo(filePath_).absolutePath());
    if(file_.open(QIODevice::WriteOnly|QIODevice::Append|QIODevice::Text))
        note(QStringLiteral("--- Nouvelle session LumaTrace · %1 ---").arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));
    else if(sink_)sink_(QStringLiteral("ERREUR : journal impossible à ouvrir : ")+filePath_);
}
RunLogger::~RunLogger(){if(file_.isOpen())file_.close();}
void RunLogger::note(const QString& message){writeLine(g_scopeDepth,message);}
void RunLogger::writeLine(int depth,const QString& message){
    const QString line=QStringLiteral("[%1] %2%3")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")))
        .arg(QString(std::max(0,depth)*2,QLatin1Char(' '))).arg(message);
    {
        QMutexLocker lock(&mutex_);
        if(file_.isOpen()){file_.write(line.toUtf8());file_.write("\n");file_.flush();}
    }
    if(sink_)sink_(line);
}

RunPaths makeRunPaths(const QString& outputRoot){
    QString root=outputRoot.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation):outputRoot;
    if(root.isEmpty())root=QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("LumaTraceData"));
    const QString stamp=QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString logs=QDir(root).filePath(QStringLiteral("logs"));
    const QString images=QDir(root).filePath(QStringLiteral("renders"));
    QDir().mkpath(logs);QDir().mkpath(images);
    return {QDir(logs).filePath(QStringLiteral("LumaTrace_%1.log").arg(stamp)),
            QDir(images).filePath(QStringLiteral("LumaTrace_%1.png").arg(stamp))};
}
} // namespace lumatrace::diagnostics
