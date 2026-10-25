// The check of the programs of the host that Mod Organizer needs (hostdeps_linux.h).

#include "hostdeps_linux.h"

#include "vfsbridge_linux.h"

#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <stdexcept>

#include <uibase/log.h>

using namespace MOBase;

namespace hostdeps
{
const char HostDependenciesUrl[] = "https://github.com/FM0Ura/ModOrganizerLinux/blob/"
                                   "main/docs/user/en/install.md#host-dependencies";

std::vector<MissingHostTool>
missingHostTools(const std::function<QString(const QString&)>& findExecutable)
{
  const std::vector<MissingHostTool> needed = {
      {"umu-run",
       QCoreApplication::translate("hostdeps", "runs the Windows programs"),
       "host-dependencies"},
      {"fusermount3",
       QCoreApplication::translate("hostdeps", "mounts the virtual folder of the mods"),
       "host-dependencies"}};

  std::vector<MissingHostTool> missing;
  for (const auto& tool : needed) {
    if (findExecutable(tool.name).isEmpty()) {
      missing.push_back(tool);
    }
  }

  return missing;
}

QString findOnHost(const QString& name)
{
  if (vfsbridge::mode() == vfsbridge::Mode::Direct) {
    return QStandardPaths::findExecutable(name);
  }

  try {
    // the name is a positional parameter of the script, never part of it
    const auto cmd = vfsbridge::helperCommand(
        "sh", {"-c", "command -v \"$1\"", "sh", name});

    QProcess p;
    p.start(cmd.program, cmd.arguments);
    if (!p.waitForFinished(10000) || p.exitStatus() != QProcess::NormalExit ||
        p.exitCode() != 0) {
      if (p.state() != QProcess::NotRunning) {
        p.kill();
        p.waitForFinished(1000);
      }
      return {};
    }

    return QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
  } catch (const std::exception& e) {
    // distrobox-host-exec is missing: the check cannot tell, and the launch reports it
    log::warn("cannot look for '{}' on the host: {}", name, e.what());
    return name;
  }
}

QString missingHostToolsMessage(const std::vector<MissingHostTool>& missing)
{
  if (missing.empty()) {
    return {};
  }

  QString text = QCoreApplication::translate(
      "hostdeps", "These programs are missing on the host and Mod Organizer needs "
                  "them:");

  for (const auto& tool : missing) {
    text += QString("\n  - %1 (%2)").arg(tool.name, tool.purpose);
  }

  text += "\n\n" + QCoreApplication::translate("hostdeps", "How to install them: %1")
                       .arg(HostDependenciesUrl);
  return text;
}
}  // namespace hostdeps
