#ifndef MODLISTS_IMPORTREVIEW_H
#define MODLISTS_IMPORTREVIEW_H

#include "importplan.h"
#include "mo2listzip.h"

#include <QString>
#include <QStringList>

#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

// What the windows of the import show before anything is installed: the state of each
// row, the selection, how many downloads the user will click and the executable
// content of the package. No widgets here.
namespace modlists
{

// (entry, source) indices into Plan::mods and ModEntry::sources
using SourceRef = std::pair<int, int>;

// classify() for every entry of the plan, in its order
std::vector<RowStatus> classifyAll(const Plan& plan,
                                   const std::vector<InstalledMod>& installed,
                                   Destination destination);

// every entry that is not optional, separators included
std::vector<int> defaultSelection(const Plan& plan);

// the Nexus sources whose archive is in the downloads folder, by the ids of its .meta
// (the installer compares the MD5 again before using it)
std::set<SourceRef> reusableSources(const Plan& plan,
                                    const std::vector<DownloadCandidate>& candidates);

// the entry will be installed in the destination: not a separator, and not kept as
// the user's (AlreadyInstalled, or DifferentVersion with KeepMine) unless the
// destination is a new instance, where nothing is installed yet
bool installsEntry(const ModEntry& entry, RowStatus status, Destination destination,
                  std::optional<VersionChoice> choice);

// the Nexus files of the selected entries that will be installed and are not
// reusable: the downloads a user without Premium clicks on the Nexus
int nexusClicks(const Plan& plan, const std::vector<RowStatus>& statuses,
                Destination destination, const std::vector<int>& selected,
                const std::map<int, VersionChoice>& choices,
                const std::set<SourceRef>& reusable);

// "Premium account ✔" or "No Premium: you will click N downloads on Nexus"
QString downloadsFooter(bool premium, int clicks);

// the version of the user's mod for the entry (same Nexus mod), empty when unknown
QString userVersion(const ModEntry& entry, const std::vector<InstalledMod>& installed);

// the status column of a row
QString statusText(const ModEntry& entry, RowStatus status, const QString& userVersion);

// the sum of the known sizes of the sources, -1 when none is known
qint64 entrySize(const ModEntry& entry);

struct EnvironmentSplit
{
  // "NAME=VALUE", as in the plan
  QStringList kept;
  // the names
  QStringList dropped;
};

// the variables an imported executable keeps (isAllowedImportedEnv(), on one line)
// and the names of the others
EnvironmentSplit splitImportedEnvironment(const QStringList& environment);

// the package has something to confirm in the review
bool hasExecutableContent(const std::vector<ExecFinding>& findings,
                          const std::vector<ExecutableEntry>& executables);

// plain text of the "Executable content" section: every finding, then every
// executable of the plan with its binary, arguments, working directory and the
// environment variables that are kept and left out
QString executableContentText(const std::vector<ExecFinding>& findings,
                              const std::vector<ExecutableEntry>& executables);

// the game of the plan is the game of the instance (Header::gameName is the
// gameShortName() of the game plugin; compared without case)
bool sameGame(const Header& header, const QString& instanceGameShortName);

// a path that names a .mo2list file (by its extension, without case)
bool isMo2ListPath(const QString& path);

}  // namespace modlists

#endif  // MODLISTS_IMPORTREVIEW_H
