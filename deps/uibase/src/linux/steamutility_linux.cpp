// Linux implementation of steamutility.h: the native Steam client (or its Flatpak) and
// its libraries. The discovery itself (installations, libraries listed in
// steamapps/libraryfolders.vdf, apps, Proton) is steam_linux.cpp.

#include "steamutility.h"

#include <uibase/linux/steam.h>

#include <QDir>

namespace MOBase
{

QString findSteam()
{
  // native package, the ~/.steam/steam link of older installs, Flatpak
  return steam::roots().value(0);
}

QString findSteamGame(const QString& appName, const QString& validFile)
{
  const QString steamPath = findSteam();
  if (steamPath.isEmpty()) {
    return "";
  }

  for (const QString& library : steam::libraries(steamPath)) {
    QDir libraryDir(library);
    if (!libraryDir.cd("steamapps/common/" + appName)) {
      continue;
    }

    if (validFile.isEmpty() || libraryDir.exists(validFile)) {
      return libraryDir.absolutePath();
    }
  }

  return "";
}

}  // namespace MOBase
