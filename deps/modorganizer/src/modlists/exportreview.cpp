#include "exportreview.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

#include <algorithm>

namespace modlists
{

namespace
{

  QString tr(const char* text)
  {
    return QCoreApplication::translate("modlists::ExportDialog", text);
  }

  void append(std::vector<Leak>& out, std::vector<Leak> leaks)
  {
    out.insert(out.end(), std::make_move_iterator(leaks.begin()),
               std::make_move_iterator(leaks.end()));
  }

}  // namespace

QString warningText(const ExportWarning& warning)
{
  using Kind = ExportWarning::Kind;

  switch (warning.kind) {
  case Kind::NoFomodChoices:
    return tr("%1: the choices of its FOMOD installer were not recorded; whoever "
              "imports the list picks the options by hand.")
        .arg(warning.mod);
  case Kind::NoInstallOrder:
    return tr("%1: several Nexus files and no recorded install order; they are "
              "installed in the order Mod Organizer lists them.")
        .arg(warning.mod);
  case Kind::NoMd5:
    return tr("%1: no checksum (the archive is not in the downloads folder); the "
              "download cannot be verified on import.")
        .arg(warning.mod);
  case Kind::Excluded:
    return tr("%1: left out of the list.").arg(warning.mod);
  case Kind::ManualWithoutUrl:
    return tr("%1: manual source without a link; whoever imports the list has to find "
              "the file.")
        .arg(warning.mod);
  case Kind::SecretEnvDropped:
    return tr("Executable \"%1\": environment variables that look like credentials "
              "were left out.")
        .arg(warning.mod);
  case Kind::MixedSources:
    return tr("%1: only its Nexus files are exported; its other files are left out.")
        .arg(warning.mod);
  case Kind::EmptyBundled:
    return tr("%1: no file to include; left out.").arg(warning.mod);
  case Kind::NexusModNotBundled:
    return tr("%1: a Nexus mod cannot have its files included; left out.")
        .arg(warning.mod);
  }

  return warning.mod;
}

QString commandLine(const ExecutableEntry& executable)
{
  QString binary = executable.binary;
  if (binary.contains(' ')) {
    binary = '"' + binary + '"';
  }
  return executable.arguments.isEmpty() ? binary : binary + ' ' + executable.arguments;
}

bool hasNexusFiles(const ExportMod& mod)
{
  return std::any_of(mod.files.begin(), mod.files.end(), [](const ExportFile& f) {
    return f.modId > 0 && f.fileId > 0;
  });
}

bool isExported(const ExportMod& mod, const ExportOptions& options)
{
  return !mod.isSeparator && (mod.enabled || options.includeInactive);
}

std::vector<ExportMod> withPendingMd5(std::vector<ExportMod> mods)
{
  const QString pending = QStringLiteral("pending");
  for (auto& mod : mods) {
    if (hasNexusFiles(mod)) {
      for (auto& f : mod.files) {
        if (f.md5.isEmpty() && !f.archivePath.isEmpty()) {
          f.md5 = pending;
        }
      }
    } else if (mod.manualMd5.isEmpty() && !mod.files.empty() &&
               !mod.files.back().archivePath.isEmpty()) {
      mod.manualMd5 = pending;
    }
  }
  return mods;
}

QString executablesReport(const std::vector<ExecutableEntry>& executables)
{
  if (executables.empty()) {
    return tr("No executables.");
  }

  QStringList lines;
  for (const auto& e : executables) {
    lines << e.title;
    lines << "  " + tr("Command line: %1").arg(commandLine(e));
    lines << "  " + tr("Working directory: %1")
                        .arg(e.workingDirectory.isEmpty() ? tr("(none)")
                                                          : e.workingDirectory);
    if (!e.steamAppId.isEmpty()) {
      lines << "  " + tr("Steam app id: %1").arg(e.steamAppId);
    }
    if (!e.proton.isEmpty()) {
      lines << "  " + tr("Proton: %1").arg(e.proton);
    }
    if (e.environment.isEmpty()) {
      lines << "  " + tr("Environment: (none)");
    } else {
      lines << "  " + tr("Environment:");
      for (const auto& variable : e.environment) {
        lines << "    " + variable;
      }
    }
    lines << QString();
  }
  return lines.join('\n');
}

QString leakUserName(const QString& envUser, const QString& home)
{
  if (!envUser.isEmpty()) {
    return envUser;
  }
  return QDir(home).dirName();
}

std::vector<Leak> findPackageLeaks(const PackageInput& package, const QString& userName,
                                   const QString& home)
{
  std::vector<Leak> out;

  for (const auto& [name, data] : package.profileFiles) {
    append(out, findLeaks("profile/" + name, QString::fromUtf8(data), userName, home));
  }

  for (const auto& e : package.plan.executables) {
    const QString prefix = tr("Executable \"%1\"").arg(e.title);
    append(out, findLeaks(prefix + tr(": binary"), e.binary, userName, home));
    append(out, findLeaks(prefix + tr(": arguments"), e.arguments, userName, home));
    append(out, findLeaks(prefix + tr(": working directory"), e.workingDirectory,
                          userName, home));
    append(out, findLeaks(prefix + tr(": environment"), e.environment.join('\n'),
                          userName, home));
  }

  append(out, findLeaks("README.md", package.readme, userName, home));
  return out;
}

qint64 packageSize(const PackageInput& package)
{
  qint64 size =
      QJsonDocument(toJson(package.plan)).toJson(QJsonDocument::Indented).size();
  size += package.readme.toUtf8().size();
  for (const auto& [name, data] : package.profileFiles) {
    size += data.size();
  }
  for (const auto& [inside, disk] : package.bundledFiles) {
    size += QFileInfo(disk).size();
  }
  return size;
}

}  // namespace modlists
