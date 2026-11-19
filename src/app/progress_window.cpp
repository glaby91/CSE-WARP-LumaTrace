#include "progress_window.h"

#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QTextCursor>

namespace lumatrace::app {

ProgressWindow::ProgressWindow(QWidget* parent):QMainWindow(parent) {
    setWindowTitle(QStringLiteral("LumaTrace — initialisation et chronométrage"));
    setMinimumSize(560,300);resize(680,360);
    setAttribute(Qt::WA_QuitOnClose,false);
    output_=new QPlainTextEdit(this);output_->setReadOnly(true);output_->setMaximumBlockCount(4000);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    output_->setStyleSheet(QStringLiteral("QPlainTextEdit { background:#07110a; color:#9de7a8; border:1px solid #275a32; padding:6px; selection-background-color:#285d35; }"));
    setCentralWidget(output_);
}

void ProgressWindow::appendLogLine(const QString& line) {
    output_->appendPlainText(line);
    output_->moveCursor(QTextCursor::End);
}
} // namespace lumatrace::app
