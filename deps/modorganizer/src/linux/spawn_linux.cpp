// Linux implementation of spawn.h (spawn.cpp is Windows-only).
//
// Native programs (ELF executables and #! scripts the user can run, isNativeProgram())
// are started by startBinary() with posix_spawn() (posixspawn_linux.cpp) in a process
// group of their own, which mo2-vfs watches to keep the virtual file system mounted
// while the program runs (ProcessRunner::run(), processrunner_linux.cpp), in the
// working directory of the parameters and with the environment of Mod Organizer; the
// HANDLE returned is the pid (envmodule.h). The standard output and error handles of
// the parameters are not used (they are for lootcli on Windows, see loot_linux.cpp).
//
// Windows programs (.exe, .bat, ...) are not started before Phase 3 of the port (they
// will be run with Wine/Proton): startBinary() shows why and returns
// INVALID_HANDLE_VALUE, which every caller already handles as a failure to start, and
// ProcessRunner::run() shows the same message without preparing anything. There is no
// Steam check, no executable blacklist (it is a usvfs list) and no helper program: Mod
// Organizer doesn't need administrative rights on Linux.
//
// The type of the files is the same as on Windows, so the "Run" entries of the menus
// are the same, plus the native programs, which are executables too.

#include "../spawn.h"
#include "posixspawn_linux.h"

#include <QFile>
#include <QMessageBox>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>

#include <uibase/log.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

using namespace MOBase;

namespace spawn
{

QString launchUnavailableMessage()
{
  return QObject::tr("Executar programas ainda não está disponível no Linux. "
                     "Esta funcionalidade chega na Fase 3 do port.");
}

bool isNativeProgram(const QFileInfo& target)
{
  // a regular file (or a link to one) that the user may execute
  const auto path = target.absoluteFilePath();
  if (!QFileInfo(path).isFile() ||
      ::access(QFile::encodeName(path).constData(), X_OK) != 0) {
    return false;
  }

  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return false;
  }

  const auto header = f.read(4);
  return header.startsWith("\177ELF") || header.startsWith("#!");
}

bool checkSteam(QWidget*, const SpawnParameters&, const QDir&, const QString&,
                const Settings&)
{
  return true;
}

bool checkBlacklist(QWidget*, const SpawnParameters&, Settings&)
{
  return true;
}

HANDLE startBinary(QWidget* parent, const SpawnParameters& sp)
{
  const auto program = sp.binary.absoluteFilePath();

  if (!isNativeProgram(sp.binary)) {
    log::warn("not starting '{}' {}: starting Windows programs is not available on "
              "Linux yet",
              program, sp.arguments);

    QMessageBox::information(parent, QObject::tr("Mod Organizer"),
                             launchUnavailableMessage());

    return INVALID_HANDLE_VALUE;
  }

  const auto cwd = sp.currentDirectory.absolutePath();

  std::vector<std::string> arguments;
  for (const auto& a : QProcess::splitCommand(sp.arguments)) {
    arguments.push_back(QFile::encodeName(a).toStdString());
  }

  log::debug("starting '{}' {} in '{}'", program, sp.arguments, cwd);

  pid_t pid   = 0;
  const int e = posixspawn::startInNewProcessGroup(
      QFile::encodeName(program).toStdString(), arguments,
      QFile::encodeName(cwd).toStdString(), pid);

  if (e != 0) {
    const auto reason = QString::fromLocal8Bit(std::strerror(e));
    log::error("failed to start '{}' in '{}': {}", program, cwd, reason);

    QMessageBox::critical(
        parent, QObject::tr("Mod Organizer"),
        QObject::tr("Cannot start \"%1\" in \"%2\": %3").arg(program, cwd, reason));

    return INVALID_HANDLE_VALUE;
  }

  log::debug("started '{}', pid {} (its own process group)", program, pid);

  return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(pid));
}

QString findJavaInstallation(const QString&)
{
  // there are no file associations to look at, java is in the PATH or nowhere
  return QStandardPaths::findExecutable("java");
}

namespace
{

  bool hasSuffix(const QFileInfo& target, std::initializer_list<const char*> suffixes)
  {
    for (auto&& s : suffixes) {
      if (target.suffix().compare(s, Qt::CaseInsensitive) == 0) {
        return true;
      }
    }

    return false;
  }

  bool isExeFile(const QFileInfo& target)
  {
    return hasSuffix(target, {"exe"});
  }

  bool isBatchFile(const QFileInfo& target)
  {
    return hasSuffix(target, {"cmd", "bat"});
  }

  bool isJavaFile(const QFileInfo& target)
  {
    return hasSuffix(target, {"jar"});
  }

}  // namespace

FileExecutionTypes getFileExecutionType(const QFileInfo& target)
{
  if (isExeFile(target) || isBatchFile(target) || isJavaFile(target) ||
      isNativeProgram(target)) {
    return FileExecutionTypes::Executable;
  }

  return FileExecutionTypes::Other;
}

FileExecutionContext getFileExecutionContext(QWidget*, const QFileInfo& target)
{
  if (isExeFile(target) || isBatchFile(target)) {
    // how they are started (Wine) is decided by Phase 3
    return {target, "", FileExecutionTypes::Executable};
  }

  if (isNativeProgram(target)) {
    return {target, "", FileExecutionTypes::Executable};
  }

  if (isJavaFile(target)) {
    const auto java = findJavaInstallation(target.absoluteFilePath());

    if (!java.isEmpty()) {
      return {QFileInfo(java), QString("-jar \"%1\"").arg(target.absoluteFilePath()),
              FileExecutionTypes::Executable};
    }
  }

  return {{}, {}, FileExecutionTypes::Other};
}

}  // namespace spawn

namespace helper
{

bool backdateBSAs(QWidget*, const std::wstring&, const std::wstring& dataPath)
{
  // the helper is a Windows program; the file times of the archives only matter to
  // the games, which run with Wine/Proton in Phase 3
  log::warn("back-dating the archives in '{}' is not available on Linux yet",
            QString::fromStdWString(dataPath));

  return false;
}

bool adminLaunch(QWidget*, const std::wstring&, const std::wstring&,
                 const std::wstring&)
{
  // Mod Organizer never needs administrative rights on Linux
  log::error("restarting as administrator is not supported on Linux");
  return false;
}

}  // namespace helper
