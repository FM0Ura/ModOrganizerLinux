// Linux implementation of spawn.h (spawn.cpp is Windows-only).
//
// Starting programs is not available on Linux before Phase 3 of the port (they will
// be Windows programs run with Wine/Proton): startBinary() shows why and returns
// INVALID_HANDLE_VALUE, which every caller already handles as a failure to start, and
// ProcessRunner::run() (processrunner_linux.cpp) shows the same message without
// preparing anything. There is no Steam check, no executable blacklist (it is a usvfs
// list) and no helper program: Mod Organizer doesn't need administrative rights on
// Linux.
//
// The type of the files is the same as on Windows, so the "Run" entries of the menus
// are the same.

#include "../spawn.h"

#include <QMessageBox>
#include <QObject>
#include <QStandardPaths>

#include <uibase/log.h>

using namespace MOBase;

namespace spawn
{

QString launchUnavailableMessage()
{
  return QObject::tr("Executar programas ainda não está disponível no Linux. "
                     "Esta funcionalidade chega na Fase 3 do port.");
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
  log::warn("not starting '{}' {}: starting programs is not available on Linux yet",
            sp.binary.absoluteFilePath(), sp.arguments);

  QMessageBox::information(parent, QObject::tr("Mod Organizer"),
                           launchUnavailableMessage());

  return INVALID_HANDLE_VALUE;
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
  if (isExeFile(target) || isBatchFile(target) || isJavaFile(target)) {
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
