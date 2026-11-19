#pragma once

#include <QFile>
#include <QElapsedTimer>
#include <QMutex>
#include <QString>

#include <functional>
#include <utility>

namespace lumatrace::diagnostics {

class RunLogger final {
public:
    using Sink = std::function<void(const QString&)>;

    class Scope final {
    public:
        Scope(RunLogger& logger, QString name);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&& other) noexcept;
        Scope& operator=(Scope&&) = delete;
    private:
        RunLogger* logger_ = nullptr;
        QString name_;
        QElapsedTimer timer_;
        int depth_ = 0;
    };

    explicit RunLogger(QString filePath, Sink sink = {});
    ~RunLogger();
    Scope scope(QString name) { return Scope(*this, std::move(name)); }
    void note(const QString& message);
    QString filePath() const { return filePath_; }

private:
    friend class Scope;
    void writeLine(int depth, const QString& message);
    QString filePath_;
    QFile file_;
    Sink sink_;
    QMutex mutex_;
};

struct RunPaths { QString logFile; QString imageFile; };
RunPaths makeRunPaths();

} // namespace lumatrace::diagnostics
