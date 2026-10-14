// Linux implementation of reportError(): the Windows version falls back to a native
// MessageBoxW when no Qt window exists.

#include "log.h"
#include "report.h"

#include <QApplication>
#include <QMainWindow>
#include <QMessageBox>

#include <cstdio>

namespace MOBase
{

// defined in report.cpp
QWidget* topLevelWindow();

void reportError(const QString& message)
{
  log::error("{}", message);

  if (!qobject_cast<QApplication*>(QCoreApplication::instance())) {
    // no widgets without a QApplication (command line tools, tests): stderr only
    std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
    return;
  }

  if (auto* mw = topLevelWindow()) {
    QMessageBox::warning(mw, QObject::tr("Error"), message, QMessageBox::Ok);
  } else {
    criticalOnTop(message);
  }
}

}  // namespace MOBase
