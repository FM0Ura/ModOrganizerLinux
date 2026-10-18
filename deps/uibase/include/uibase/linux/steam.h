// Steam installations, installed games, Proton prefixes and Proton versions, read from
// the files of the Steam client (Linux only).

#ifndef UIBASE_LINUX_STEAM_H
#define UIBASE_LINUX_STEAM_H

#include <uibase/dllimport.h>

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace MOBase::steam
{
struct QDLLEXPORT App
{
  QString appId;
  QString root;        // Steam installation (STEAM_COMPAT_CLIENT_INSTALL_PATH)
  QString library;     // library with the game
  QString gamePath;    // <library>/steamapps/common/<installdir>
  QString compatData;  // compatdata/<appid> with pfx/, or empty (never run with Proton)
};

struct QDLLEXPORT Proton
{
  QString displayName;   // "Proton - Experimental", "GE-Proton11-7"
  QString internalName;  // name in config.vdf: "proton_experimental", "GE-Proton11-7"
  QString path;          // the directory with the "proton" script (PROTONPATH)
};

// the Steam installations of this user, native first, then ~/.steam/steam, then
// Flatpak; paths that lead to the same directory count once (the first spelling is
// kept)
QDLLEXPORT QStringList roots();

// the libraries of a Steam installation: the installation itself, then those of its
// steamapps/libraryfolders.vdf (absolute, existing, each once)
QDLLEXPORT QStringList libraries(const QString& root);

// the installed game with this app ID (digits only) in any library of any installation
QDLLEXPORT std::optional<App> findApp(const QString& appId);

// the Proton versions of every installation: the official ones
// (steamapps/common/Proton* of each library, with a "proton" script), then the
// compatibility tools (compatibilitytools.d/*/compatibilitytool.vdf); each directory
// once
QDLLEXPORT std::vector<Proton> installedProtons();

// the internal name of the compatibility tool Steam uses for the app:
// CompatToolMapping of config/config.vdf for the app ID, otherwise the default one
// (app ID "0"); empty when there is none
QDLLEXPORT QString compatToolName(const QString& appId);

// the installed Proton Steam uses for the app: the one named by compatToolName() if it
// is installed (nullopt if it is not); without a name, the one whose "proton" script
// has CURRENT_PREFIX_VERSION="<compatdata/version>" (the first in installedProtons()
// order); nullopt when none matches
QDLLEXPORT std::optional<Proton> protonForApp(const App& app);

// the Proton with this internal or display name (ignoring case) among `protons`
QDLLEXPORT std::optional<Proton> findProton(const std::vector<Proton>& protons,
                                            const QString& name);
}  // namespace MOBase::steam

#endif  // UIBASE_LINUX_STEAM_H
