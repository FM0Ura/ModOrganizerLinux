// Linux implementation of steamutility.h: the native Steam client (or its Flatpak) and
// the libraries listed in steamapps/libraryfolders.vdf, read with the VDF parser of
// vdf_linux.cpp. A complete Steam module (app manifests, Proton prefixes) is
// steam_linux.cpp.

#include "steamutility.h"

#include <uibase/linux/vdf.h>

#include <QDir>
#include <QFileInfo>

namespace MOBase
{

namespace
{

  bool isNumber(const QByteArray& s)
  {
    bool ok = false;
    s.toUInt(&ok);
    return ok;
  }

  // library paths of a libraryfolders.vdf file: the "path" values of the blocks inside
  // the root block (current format) or the values with numeric keys directly in the
  // root block (format used before 2021)
  QStringList parseLibraryFolders(const vdf::Node& root)
  {
    QStringList paths;

    for (const auto& top : root.children) {
      for (const auto& entry : top.children) {
        const QString path = entry.block                    ? entry.value(u"path")
                             : isNumber(entry.key.toUtf8()) ? entry.text
                                                            : QString();
        if (!path.isEmpty()) {
          paths.append(path);
        }
      }
    }

    return paths;
  }

  QStringList libraryFolders(const QDir& steamDir)
  {
    const auto root =
        vdf::parseFile(steamDir.absoluteFilePath("steamapps/libraryfolders.vdf"));

    return root ? parseLibraryFolders(*root) : QStringList();
  }

}  // namespace

QString findSteam()
{
  // native package, the ~/.steam/steam link of older installs, Flatpak
  static const char* const candidates[] = {
      ".local/share/Steam",
      ".steam/steam",
      ".var/app/com.valvesoftware.Steam/.local/share/Steam",
  };

  const QDir home(QDir::homePath());

  for (const char* candidate : candidates) {
    const QString path = home.absoluteFilePath(candidate);
    if (QFileInfo(path).isDir()) {
      return QDir::cleanPath(path);
    }
  }

  return "";
}

QString findSteamGame(const QString& appName, const QString& validFile)
{
  const QString steam = findSteam();
  if (steam.isEmpty()) {
    return "";
  }

  const QDir steamDir(steam);

  // the Steam install is always a library
  QStringList libraries = {steamDir.absolutePath()};
  for (const QString& folder : libraryFolders(steamDir)) {
    const QString clean = QDir::cleanPath(folder);
    if (QDir::isAbsolutePath(clean) && !libraries.contains(clean)) {
      libraries.append(clean);
    }
  }

  for (const QString& library : libraries) {
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
