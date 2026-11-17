#ifndef MODLISTS_EXPORTCOLLECTOR_H
#define MODLISTS_EXPORTCOLLECTOR_H

#include "exportbuilder.h"

#include <QByteArray>
#include <QString>

#include <functional>
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
  // highest; the archive path and size of each file come from the downloads folder
  // when the archive is still there. No MD5 is computed here, see fillMd5().
  std::vector<ExportMod> mods() const;

  // Computes the MD5 of the archives that the export of `options` will use: the
  // Nexus files of the exported mods (enabled, or all with includeInactive), and the
  // last archive of a mod without Nexus files exported as Manual whose manualMd5 is
  // empty. progress(done, total) is called before each archive and once at the end;
  // when it returns false, the rest is left without MD5.
  void fillMd5(std::vector<ExportMod>& mods, const ExportOptions& options,
               const std::function<bool(int done, int total)>& progress) const;

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
