#ifndef MODLISTS_IMPORTSUMMARY_H
#define MODLISTS_IMPORTSUMMARY_H

#include <QString>
#include <QStringList>

#include <map>
#include <utility>
#include <vector>

// The summary of an import: what happened to each mod of the list, and what is left
// for the user to look at (plugins, masters, INIs, executables). Pure.
namespace modlists
{

enum class Outcome
{
  Installed,
  Reused,
  Failed,
  Skipped
};

struct SummaryRow
{
  QString mod;
  Outcome outcome;
  QString detail;
  // the failure needs the user's choices: the row offers "Install manually"
  bool canInstallManually = false;
};

struct Summary
{
  std::vector<SummaryRow> rows;
  std::vector<QString> pluginsNotInstalled;
  // (plugin, its masters that are not there)
  std::vector<std::pair<QString, QStringList>> missingMasters;
  std::vector<QString> inisNotApplied;
  std::vector<QString> executablesWithoutTarget;
  bool offerLoot              = false;
  bool userPluginOrderChanged = false;
};

// plain text in sections, for the clipboard; empty sections are left out
QString summaryText(const Summary& summary);

// the entries of the Failed rows, by entryOfMod[row.mod], in the order of the rows;
// a row missing from the map is left out
std::vector<int> retryable(const Summary& summary,
                           const std::map<QString, int>& entryOfMod);

// true for "FOMOD without recorded choices", "Needs a manual installation" and any
// detail that starts with "FOMOD "
bool canInstallManually(const QString& detail);

// a Failed row with canInstallManually set from the detail
SummaryRow failedRow(const QString& mod, const QString& detail);

}  // namespace modlists

#endif  // MODLISTS_IMPORTSUMMARY_H
