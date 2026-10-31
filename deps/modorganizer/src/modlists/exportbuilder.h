#ifndef MODLISTS_EXPORTBUILDER_H
#define MODLISTS_EXPORTBUILDER_H

#include "mo2listzip.h"
#include "pathmarkers.h"
#include "plan.h"

#include <QByteArray>
#include <QJsonArray>
#include <QString>

#include <map>
#include <optional>
#include <vector>

// The export of a profile: from what ExportCollector read of Mod Organizer to the
// PackageInput of a .mo2list. The package is shared with other people, so it never
// carries personal data (saves, ModOrganizer.ini, logs, machine paths) nor the files of
// Nexus mods.
namespace modlists
{

// what to do with a mod without Nexus files
enum class NonNexusChoice
{
  // the importer downloads it by hand (url, file name)
  Manual,
  // its files go in the package, under bundled/
  Bundled,
  // left out of the list
  Exclude
};

// one installed archive of a mod
struct ExportFile
{
  // both > 0 for a Nexus file
  int modId  = 0;
  int fileId = 0;
  // fileKey() of the archive
  QString key;
  QString md5;
  qint64 size = -1;
  std::optional<QJsonArray> fomodChoices;
  // the archive has a FOMOD installer and no choices were recorded
  bool fomodWithoutChoices = false;
};

struct ExportMod
{
  QString name;
  QString version;
  bool isSeparator = false;
  bool enabled     = true;
  // in the recorded install order; without one, in the order of installedFiles() and
  // hasInstallOrder is false
  std::vector<ExportFile> files;
  bool hasInstallOrder    = true;
  NonNexusChoice nonNexus = NonNexusChoice::Manual;
  QString manualUrl;
  QString manualFileName;
  QString manualMd5;
  // the folder of the mod, for Bundled
  QString absolutePath;
};

struct ExportOptions
{
  bool includeInactive = false;
  // files of the overwrite folder to bundle, relative to it; none by default
  std::vector<QString> overwriteFiles;
  QString listName, author, version, readme;
};

struct ExportWarning
{
  enum class Kind
  {
    NoFomodChoices,
    NoInstallOrder,
    NoMd5,
    Excluded,
    ManualWithoutUrl
  };

  Kind kind;
  QString mod;
};

struct ExportResult
{
  PackageInput package;
  std::vector<ExportWarning> warnings;
  int nexusCount    = 0;
  int manualCount   = 0;
  int bundledCount  = 0;
  int excludedCount = 0;
};

// True for what never goes in a package: ModOrganizer.ini, *.ess, *.skse and *.log
// (ignoring case) and anything under a "saves" folder. `relativePath` uses '/' or '\'.
bool isExcludedFromPackage(const QString& relativePath);

// Name of the bundled mod made of the chosen overwrite files.
QString overwriteModName(const QString& listName);

// Builds the package of a profile:
// - the inactive mods only with includeInactive, then as optional and disabled; the
//   separators always;
// - a mod with Nexus files is one entry with a nexus source per file, in order;
// - a mod without them follows its nonNexus choice; Bundled takes the files under
//   absolutePath except meta.ini and symbolic links;
// - overwriteFiles become the bundled mod overwriteModName(listName), the last one
//   (highest priority);
// - the profile INIs, the executables and their paths go through toMarkers();
// - what isExcludedFromPackage() is never in the package, whatever the input.
// bundledFiles are (path inside bundled/, absolute path on disk), the first being
// "<bundledDirName(mod)>/<relative path>". base gives gameName and nexusDomain.
ExportResult buildExport(const std::vector<ExportMod>& modsLowToHigh,
                         const std::vector<PluginEntry>& plugins,
                         const std::map<QString, QByteArray>& profileInis,
                         const std::vector<ExecutableEntry>& executables,
                         const ProfileConfig& profile, const PathMarkers& markers,
                         const QString& overwriteDir, const ExportOptions& options,
                         const Header& base);

}  // namespace modlists

#endif  // MODLISTS_EXPORTBUILDER_H
