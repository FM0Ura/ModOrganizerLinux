#ifndef MODLISTS_EXPORTREVIEW_H
#define MODLISTS_EXPORTREVIEW_H

#include "exportbuilder.h"
#include "pathmarkers.h"

#include <QString>

#include <vector>

// What the export dialog shows the author before saving: readable warnings, the
// command lines of the executables, what still holds the home folder or the user name
// and the size of the package. No widgets here.
namespace modlists
{

// readable text of a warning, naming its mod or executable
QString warningText(const ExportWarning& warning);

// the binary, quoted when it has a space, then the arguments
QString commandLine(const ExecutableEntry& executable);

// the mod has files with Nexus ids (the others follow its nonNexus choice)
bool hasNexusFiles(const ExportMod& mod);

// the mod goes in the export of `options`: not a separator, enabled or inactive with
// includeInactive
bool isExported(const ExportMod& mod, const ExportOptions& options);

// a copy where what ExportCollector::fillMd5() would hash has a placeholder MD5, so a
// preview of buildExport() only warns about the MD5 that stays missing (the archive is
// not in the downloads folder)
std::vector<ExportMod> withPendingMd5(std::vector<ExportMod> mods);

// every executable with its full command line (binary and arguments), working
// directory, Steam app id, Proton and environment, as plain text
QString executablesReport(const std::vector<ExecutableEntry>& executables);

// $USER, or the last component of `home` when it is empty
QString leakUserName(const QString& envUser, const QString& home);

// bundled files larger than this are not scanned for leaks
inline constexpr qint64 MaxLeakScanSize = qint64(2) << 20;

// findLeaks() on every profile file ("profile/<name>"), on the binary, arguments,
// working directory and environment (one line per variable) of every executable, on
// the README, on the manifest ("manifest.json") and on every bundled file that looks
// like text ("bundled/<path inside bundled/>": at most MaxLeakScanSize bytes, no NUL
// byte in its first 8 KiB; UTF-8, or Latin-1 when it is not valid UTF-8)
std::vector<Leak> findPackageLeaks(const PackageInput& package, const QString& userName,
                                   const QString& home);

// bytes before compression: the manifest, the profile files, the README and the
// bundled files (as they are on disk now)
qint64 packageSize(const PackageInput& package);

}  // namespace modlists

#endif  // MODLISTS_EXPORTREVIEW_H
