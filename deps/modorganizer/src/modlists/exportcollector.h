#ifndef MODLISTS_EXPORTCOLLECTOR_H
#define MODLISTS_EXPORTCOLLECTOR_H

#include "exportbuilder.h"

#include <QByteArray>
#include <QString>

#include <map>
#include <vector>

class OrganizerCore;
class Profile;

// Reads what buildExport() needs from Mod Organizer: the mods of a profile, its INIs,
// the executables, the plugins and the paths to replace by markers. Nothing here
// changes the instance.
namespace modlists
{

class ExportCollector
{
public:
  ExportCollector(OrganizerCore& core, const Profile& profile);

  // the regular mods and separators of the profile, from the lowest priority to the
  // highest; the MD5 and size of each file come from its archive in the downloads
  // folder when it is still there (empty MD5 otherwise)
  std::vector<ExportMod> mods() const;

  // instance, game, the Proton prefix of the game (the WINEPREFIX of the Proton
  // launcher) and the home folder
  PathMarkers markers() const;

  // the game INIs of the profile folder, by file name; empty without local settings
  std::map<QString, QByteArray> profileInis() const;

  std::vector<ExecutableEntry> executables() const;

  // in load order
  std::vector<PluginEntry> plugins() const;

  // local INIs and saves of the profile; iniFiles is left to buildExport()
  ProfileConfig profileConfig() const;

  // gameName and nexusDomain of the managed game
  Header baseHeader() const;

  QString overwriteDir() const;

private:
  OrganizerCore& m_core;
  const Profile& m_profile;
};

}  // namespace modlists

#endif  // MODLISTS_EXPORTCOLLECTOR_H
