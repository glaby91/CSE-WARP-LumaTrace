#pragma once

#include <QMainWindow>
#include <QString>

class QPlainTextEdit;

namespace lumatrace::app {

class ProgressWindow final : public QMainWindow {
public:
    explicit ProgressWindow(QWidget* parent = nullptr);
    void appendLogLine(const QString& line);
private:
    QPlainTextEdit* output_ = nullptr;
};

} // namespace lumatrace::app
