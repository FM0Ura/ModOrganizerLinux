#include "importreview.h"

#include "exportreview.h"

#include <QCoreApplication>

#include <algorithm>

namespace modlists
{

namespace
{

  QString tr(const char* text)
  {
    return QCoreApplication::translate("modlists::ImportReview", text);
  }

}  // namespace

std::vector<RowStatus> classifyAll(const Plan& plan,
                                   const std::vector<InstalledMod>& installed,
                                   Destination destination)
{
  std::vector<RowStatus> out;
  out.reserve(plan.mods.size());
  for (const auto& entry : plan.mods) {
    out.push_back(classify(entry, installed, destination));
  }
  return out;
}

std::vector<int> defaultSelection(const Plan& plan)
{
  std::vector<int> out;
  for (std::size_t i = 0; i < plan.mods.size(); ++i) {
    if (!plan.mods[i].optional) {
      out.push_back(static_cast<int>(i));
    }
  }
  return out;
}

std::set<SourceRef> reusableSources(const Plan& plan,
                                    const std::vector<DownloadCandidate>& candidates)
{
  std::set<SourceRef> out;
  for (std::size_t e = 0; e < plan.mods.size(); ++e) {
    const auto& sources = plan.mods[e].sources;
    for (std::size_t s = 0; s < sources.size(); ++s) {
      const Source& source = sources[s];
      if (source.kind != SourceKind::Nexus || source.modId <= 0 || source.fileId <= 0) {
        continue;
      }
      const bool found =
          std::any_of(candidates.begin(), candidates.end(), [&](const auto& c) {
            return c.modId == source.modId && c.fileId == source.fileId;
          });
      if (found) {
        out.emplace(static_cast<int>(e), static_cast<int>(s));
      }
    }
  }
  return out;
}

bool installsEntry(const ModEntry& entry, RowStatus status, Destination destination,
                   std::optional<VersionChoice> choice)
{
  if (entry.isSeparator) {
    return false;
  }
  if (destination == Destination::NewInstance) {
    return true;
  }
  if (status == RowStatus::AlreadyInstalled) {
    return false;
  }
  if (status == RowStatus::DifferentVersion) {
    return choice != VersionChoice::KeepMine;
  }
  return true;
}

int nexusClicks(const Plan& plan, const std::vector<RowStatus>& statuses,
                Destination destination, const std::vector<int>& selected,
                const std::map<int, VersionChoice>& choices,
                const std::set<SourceRef>& reusable)
{
  int clicks = 0;
  std::set<int> seen;
  for (int e : selected) {
    if (e < 0 || e >= static_cast<int>(plan.mods.size()) ||
        e >= static_cast<int>(statuses.size()) || !seen.insert(e).second) {
      continue;
    }
    const ModEntry& entry = plan.mods[e];
    const auto choice     = choices.find(e);
    if (!installsEntry(entry, statuses[e], destination,
                       choice == choices.end()
                           ? std::nullopt
                           : std::optional<VersionChoice>(choice->second))) {
      continue;
    }
    for (std::size_t s = 0; s < entry.sources.size(); ++s) {
      const Source& source = entry.sources[s];
      if (source.kind == SourceKind::Nexus && source.modId > 0 && source.fileId > 0 &&
          !reusable.count({e, static_cast<int>(s)})) {
        ++clicks;
      }
    }
  }
  return clicks;
}

QString downloadsFooter(bool premium, int clicks)
{
  if (premium) {
    return tr("Premium account ✔");
  }
  return tr("No Premium: you will click %1 downloads on Nexus").arg(clicks);
}

QString userVersion(const ModEntry& entry, const std::vector<InstalledMod>& installed)
{
  if (entry.sources.empty() || entry.sources.front().modId <= 0) {
    return {};
  }
  for (const auto& mod : installed) {
    if (mod.modId == entry.sources.front().modId && !mod.version.isEmpty()) {
      return mod.version;
    }
  }
  return {};
}

QString statusText(const ModEntry& entry, RowStatus status, const QString& userVersion)
{
  if (entry.isSeparator) {
    return tr("Separator");
  }

  switch (status) {
  case RowStatus::AlreadyInstalled:
    return tr("✔ Already installed");
  case RowStatus::DifferentVersion:
    return userVersion.isEmpty() ? tr("⚠ You have another version")
                                 : tr("⚠ You have %1").arg(userVersion);
  case RowStatus::Manual:
    return tr("Download manually");
  case RowStatus::Bundled:
    return tr("Included in the list");
  case RowStatus::New:
    break;
  }

  const bool fomod =
      std::any_of(entry.sources.begin(), entry.sources.end(), [](const Source& s) {
        return s.fomodChoices.has_value();
      });
  return fomod ? tr("New (FOMOD with the author's choices)") : tr("New");
}

qint64 entrySize(const ModEntry& entry)
{
  qint64 total = -1;
  for (const auto& s : entry.sources) {
    if (s.size >= 0) {
      total = std::max<qint64>(total, 0) + s.size;
    }
  }
  return total;
}

EnvironmentSplit splitImportedEnvironment(const QStringList& environment)
{
  // the same test as ModlistInstaller::applyExecutables()
  EnvironmentSplit out;
  for (const auto& variable : environment) {
    const QString name  = variable.section('=', 0, 0).trimmed();
    const QString value = variable.section('=', 1);
    const bool oneLine  = !variable.contains('\n') && !variable.contains('\r');
    if (oneLine && isAllowedImportedEnv(name, value)) {
      out.kept.append(variable);
    } else if (!name.isEmpty()) {
      out.dropped.append(name);
    }
  }
  return out;
}

bool hasExecutableContent(const std::vector<ExecFinding>& findings,
                          const std::vector<ExecutableEntry>& executables)
{
  return !findings.empty() || !executables.empty();
}

QString executableContentText(const std::vector<ExecFinding>& findings,
                              const std::vector<ExecutableEntry>& executables)
{
  QStringList lines;

  const auto kindText = [](ExecFinding::Kind kind) {
    switch (kind) {
    case ExecFinding::Kind::NativeFile:
      return tr("Native file");
    case ExecFinding::Kind::Executable:
      return tr("Executable");
    case ExecFinding::Kind::IniLoadsCode:
      return tr("INI loads code or changes paths");
    }
    return QString();
  };

  if (!findings.empty()) {
    lines << tr("Found in the package:");
    for (const auto& f : findings) {
      lines << QString("  - %1: %2%3")
                   .arg(kindText(f.kind), f.what,
                        f.detail.isEmpty() ? QString() : " (" + f.detail + ")");
    }
  }

  if (!executables.empty()) {
    if (!lines.isEmpty()) {
      lines << QString();
    }
    lines << tr("Executables the import adds to the list (never run by the import):");
    for (const auto& exe : executables) {
      const auto env = splitImportedEnvironment(exe.environment);
      lines << QString("  %1").arg(exe.title);
      lines << tr("    Binary: %1").arg(exe.binary);
      lines << tr("    Arguments: %1").arg(exe.arguments);
      lines << tr("    Working directory: %1").arg(exe.workingDirectory);
      lines << tr("    Command line: %1").arg(commandLine(exe));
      lines << tr("    Environment kept: %1")
                   .arg(env.kept.isEmpty() ? tr("(none)") : env.kept.join(" "));
      if (!env.dropped.isEmpty()) {
        lines << tr("    Environment left out: %1").arg(env.dropped.join(", "));
      }
    }
  }

  return lines.join('\n');
}

bool sameGame(const Header& header, const QString& instanceGameShortName)
{
  return header.gameName.compare(instanceGameShortName, Qt::CaseInsensitive) == 0;
}

bool isMo2ListPath(const QString& path)
{
  return path.size() > 8 && path.endsWith(".mo2list", Qt::CaseInsensitive);
}

}  // namespace modlists
