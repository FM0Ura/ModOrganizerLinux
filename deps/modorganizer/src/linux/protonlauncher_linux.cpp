// Launch of the programs started by Run on Linux (protonlauncher_linux.h): Windows
// programs through umu-run in the Proton prefix of their Steam game (spec of Phase 3,
// §6.2), native programs directly; both are then started by mo2-vfs
// (VfsConnector::spawn()).

#include "protonlauncher_linux.h"

#include "vfsbridge_linux.h"

#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>

#include <uibase/linux/steam.h>
#include <uibase/log.h>

using namespace MOBase;

namespace proton
{
namespace
{
  QString names(const std::vector<steam::Proton>& protons)
  {
    QStringList list;
    for (const auto& p : protons) {
      list.append(p.displayName);
    }
    return list.isEmpty() ? QObject::tr("none") : list.join(", ");
  }

  // STEAM_COMPAT_LIBRARY_PATHS: the steamapps directory of every library of the Steam
  // installation, the one with the game first
  QString libraryPaths(const steam::App& app)
  {
    QStringList paths;

    const auto add = [&](const QString& library) {
      const auto steamapps = vfsbridge::hostCanonicalPath(library + "/steamapps");
      if (!paths.contains(steamapps)) {
        paths.append(steamapps);
      }
    };

    add(app.library);
    for (const auto& library : steam::libraries(app.root)) {
      add(library);
    }

    return paths.join(':');
  }
}  // namespace

bool isWindowsProgram(const QFileInfo& file)
{
  static const QStringList suffixes = {"exe", "bat", "cmd", "com"};
  return suffixes.contains(file.suffix(), Qt::CaseInsensitive);
}

QStringList validEnvironment(const QStringList& environment)
{
  static const QRegularExpression name("^[A-Za-z_][A-Za-z0-9_]*$");
  QStringList valid;

  for (const auto& e : environment) {
    const auto eq = e.indexOf('=');
    if (eq > 0 && name.match(e.left(eq)).hasMatch()) {
      valid.append(e);
    } else if (!e.trimmed().isEmpty()) {
      log::warn("ignoring the environment variable '{}' of the executable: it is not "
                "NAME=value",
                e);
    }
  }

  return valid;
}

VfsSpawnRequest makeLaunch(const LaunchParameters& p)
{
  VfsSpawnRequest r;
  const auto program = p.binary.absoluteFilePath();
  const auto args    = QProcess::splitCommand(p.arguments);

  r.workingDirectory = (p.workingDirectory.isEmpty() || p.workingDirectory == ".")
                           ? p.binary.absolutePath()
                           : p.workingDirectory;
  r.output           = p.output;
  const auto extra   = validEnvironment(p.environment);

  if (!isWindowsProgram(p.binary)) {
    r.argv        = QStringList{program} + args;
    r.environment = extra;
    return r;
  }

  const auto appId = p.steamAppId.isEmpty() ? p.gameSteamAppId : p.steamAppId;
  if (appId.isEmpty()) {
    throw LaunchError(QObject::tr("Cannot start \"%1\" with Proton: the game has no "
                                  "Steam app ID; set one in the executable settings.")
                          .arg(program)
                          .toStdString());
  }

  const auto app = steam::findApp(appId);
  if (!app) {
    throw LaunchError(QObject::tr("Cannot start \"%1\" with Proton: the Steam game %2 "
                                  "is not installed in a Steam library.")
                          .arg(program, appId)
                          .toStdString());
  }
  if (app->compatData.isEmpty()) {
    throw LaunchError(
        QObject::tr("Cannot start \"%1\": the Steam game %2 has no Proton "
                    "prefix yet. Run the game once from Steam to create "
                    "it.")
            .arg(program, appId)
            .toStdString());
  }

  const auto protons = steam::installedProtons();
  std::optional<steam::Proton> chosen;

  if (!p.proton.isEmpty()) {
    chosen = steam::findProton(protons, p.proton);
    if (!chosen) {
      throw LaunchError(QObject::tr("Proton \"%1\", set for this executable, is not "
                                    "installed. Installed: %2.")
                            .arg(p.proton, names(protons))
                            .toStdString());
    }
  } else {
    chosen = steam::protonForApp(*app);
    if (!chosen) {
      const auto mapped = steam::compatToolName(*app);
      throw LaunchError(
          (mapped.isEmpty()
               ? QObject::tr("Cannot tell which Proton Steam uses for the game %1; "
                             "choose one in the executable settings. Installed: %2.")
                     .arg(appId, names(protons))
               : QObject::tr("Steam uses Proton \"%1\" for the game %2, but it is not "
                             "installed. Installed: %3.")
                     .arg(mapped, appId, names(protons)))
              .toStdString());
    }
  }

  const bool batch = p.binary.suffix().compare("bat", Qt::CaseInsensitive) == 0 ||
                     p.binary.suffix().compare("cmd", Qt::CaseInsensitive) == 0;
  r.argv           = batch ? QStringList{"umu-run", "cmd", "/c", program} + args
                           : QStringList{"umu-run", program} + args;

  r.environment = QStringList{"WINEPREFIX=" + app->compatData, "GAMEID=umu-" + appId,
                              "STORE=steam", "SteamAppId=" + appId,
                              "SteamGameId=" + appId, "PROTONPATH=" + chosen->path,
                              "STEAM_COMPAT_CLIENT_INSTALL_PATH=" + app->root,
                              // the game drive (S:) of Proton, which the registry paths
                              // Steam writes for the game use (SSEEdit); canonical
                              // because umu keeps the library list only when it finds
                              // a mount point above the game path, and /home may be a
                              // symlink to /var/home (in a distrobox, they are
                              // resolved as the host sees them)
                              "STEAM_COMPAT_INSTALL_PATH=" +
                                  vfsbridge::hostCanonicalPath(app->gamePath),
                              "STEAM_COMPAT_LIBRARY_PATHS=" + libraryPaths(*app)} +
                  extra;
  r.winePrefix  = app->compatData;

  log::debug("{} runs with {} in {}", program, chosen->displayName, app->compatData);
  return r;
}
}  // namespace proton
