#include "importsummary.h"

namespace modlists
{

namespace
{

  void section(QStringList& out, const QString& title, const QStringList& lines)
  {
    if (lines.isEmpty()) {
      return;
    }
    if (!out.isEmpty()) {
      out.append(QString());
    }
    out.append(QString("%1 (%2)").arg(title).arg(lines.size()));
    for (const auto& line : lines) {
      out.append("  " + line);
    }
  }

  QStringList rowsOf(const Summary& summary, Outcome outcome)
  {
    QStringList lines;
    for (const auto& row : summary.rows) {
      if (row.outcome == outcome) {
        lines.append(row.detail.isEmpty() ? row.mod
                                          : QString("%1: %2").arg(row.mod, row.detail));
      }
    }
    return lines;
  }

  QStringList toList(const std::vector<QString>& v)
  {
    return QStringList(v.begin(), v.end());
  }

}  // namespace

QString summaryText(const Summary& summary)
{
  QStringList out;
  section(out, "Installed", rowsOf(summary, Outcome::Installed));
  section(out, "Reused", rowsOf(summary, Outcome::Reused));
  section(out, "Failed", rowsOf(summary, Outcome::Failed));
  section(out, "Skipped", rowsOf(summary, Outcome::Skipped));
  section(out, "Plugins not installed", toList(summary.pluginsNotInstalled));

  QStringList masters;
  for (const auto& [plugin, missing] : summary.missingMasters) {
    masters.append(QString("%1: %2").arg(plugin, missing.join(", ")));
  }
  section(out, "Missing masters", masters);

  section(out, "INI files not applied", toList(summary.inisNotApplied));
  section(out, "Executables without a target",
          toList(summary.executablesWithoutTarget));

  QStringList notes;
  if (summary.userPluginOrderChanged) {
    notes.append("Your plugin order was changed to fit the list's plugins.");
  }
  if (summary.offerLoot) {
    notes.append("Sort the plugins with LOOT.");
  }
  if (!notes.isEmpty()) {
    if (!out.isEmpty()) {
      out.append(QString());
    }
    out.append(notes);
  }

  return out.join("\n") + "\n";
}

std::vector<int> retryable(const Summary& summary,
                           const std::map<QString, int>& entryOfMod)
{
  std::vector<int> out;
  for (const auto& row : summary.rows) {
    if (row.outcome != Outcome::Failed) {
      continue;
    }
    auto it = entryOfMod.find(row.mod);
    if (it != entryOfMod.end()) {
      out.push_back(it->second);
    }
  }
  return out;
}

bool canInstallManually(const QString& detail)
{
  return detail == "FOMOD without recorded choices" ||
         detail == "Needs a manual installation" || detail.startsWith("FOMOD ");
}

SummaryRow failedRow(const QString& mod, const QString& detail)
{
  return SummaryRow{mod, Outcome::Failed, detail, canInstallManually(detail)};
}

}  // namespace modlists
