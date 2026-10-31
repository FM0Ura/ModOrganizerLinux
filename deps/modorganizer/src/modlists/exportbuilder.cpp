#include "exportbuilder.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>

namespace modlists
{

namespace
{

  // the files to bundle under dir, relative to it and sorted; symbolic links are
  // skipped, they could point outside the mod
  QStringList bundledFilesOf(const QString& dir)
  {
    QStringList out;
    // an empty path would be the current directory
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
      return out;
    }
    const QDir root(dir);
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      it.next();
      const QFileInfo info = it.fileInfo();
      if (info.isSymLink()) {
        continue;
      }
      const QString relative = root.relativeFilePath(info.filePath());
      if (relative.compare(QLatin1String("meta.ini"), Qt::CaseInsensitive) == 0 ||
          isExcludedFromPackage(relative)) {
        continue;
      }
      out.append(relative);
    }
    out.sort();
    return out;
  }

  // a relative path that stays inside its folder
  bool isSafeRelative(const QString& path)
  {
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.startsWith('\\')) {
      return false;
    }
    const auto parts = QString(path).replace('\\', '/').split('/');
    return !parts.contains(QStringLiteral(".."));
  }

  // The INIs are bytes in an unknown encoding (often Windows-1252). Read as Latin-1
  // they come back unchanged, and the markers, encoded the same way from UTF-8, still
  // match the UTF-8 paths in them.
  QByteArray sanitizedIni(const QByteArray& data, const PathMarkers& markers)
  {
    const auto bytes = [](const QString& s) {
      return QString::fromLatin1(s.toUtf8());
    };
    const PathMarkers latin1{bytes(markers.instance), bytes(markers.game),
                             bytes(markers.protonPrefix), bytes(markers.home)};
    return toMarkers(QString::fromLatin1(data), latin1).toLatin1();
  }

  ExecutableEntry sanitizedExecutable(ExecutableEntry e, const PathMarkers& markers)
  {
    e.binary           = toMarkers(e.binary, markers);
    e.arguments        = toMarkers(e.arguments, markers);
    e.workingDirectory = toMarkers(e.workingDirectory, markers);
    for (auto& variable : e.environment) {
      variable = toMarkers(variable, markers);
    }
    return e;
  }

  bool isNexus(const ExportFile& f)
  {
    return f.modId > 0 && f.fileId > 0;
  }

  class Builder
  {
  public:
    Builder(const ExportOptions& options, ExportResult& result)
        : m_options(options), m_result(result)
    {}

    void add(const ExportMod& mod)
    {
      ModEntry entry;
      entry.name        = mod.name;
      entry.version     = mod.version;
      entry.isSeparator = mod.isSeparator;

      if (mod.isSeparator) {
        m_result.package.plan.mods.push_back(entry);
        return;
      }

      if (!mod.enabled) {
        if (!m_options.includeInactive) {
          return;
        }
        entry.optional = true;
        entry.enabled  = false;
      }

      std::vector<const ExportFile*> nexus;
      for (const auto& f : mod.files) {
        if (isNexus(f)) {
          nexus.push_back(&f);
        }
      }

      if (!nexus.empty()) {
        addNexus(entry, mod, nexus);
      } else {
        switch (mod.nonNexus) {
        case NonNexusChoice::Manual:
          addManual(entry, mod);
          break;
        case NonNexusChoice::Bundled:
          addBundled(entry, bundledFilesOf(mod.absolutePath), mod.absolutePath);
          break;
        case NonNexusChoice::Exclude:
          warn(ExportWarning::Kind::Excluded, mod.name);
          ++m_result.excludedCount;
          return;
        }
      }

      m_result.package.plan.mods.push_back(std::move(entry));
    }

    void addOverwrite(const QString& overwriteDir)
    {
      QStringList files;
      if (overwriteDir.isEmpty()) {
        return;
      }
      for (const auto& relative : m_options.overwriteFiles) {
        if (isSafeRelative(relative) && !isExcludedFromPackage(relative)) {
          const QFileInfo info(QDir(overwriteDir).filePath(relative));
          if (info.isFile() && !info.isSymLink()) {
            files.append(QDir::cleanPath(QString(relative).replace('\\', '/')));
          }
        }
      }
      if (files.isEmpty()) {
        return;
      }

      ModEntry entry;
      entry.name = overwriteModName(m_options.listName);
      addBundled(entry, files, overwriteDir);
      m_result.package.plan.mods.push_back(std::move(entry));
    }

  private:
    const ExportOptions& m_options;
    ExportResult& m_result;

    void warn(ExportWarning::Kind kind, const QString& mod)
    {
      m_result.warnings.push_back({kind, mod});
    }

    void fomodWarning(const ExportFile& f, const QString& mod)
    {
      if (f.fomodWithoutChoices && !f.fomodChoices) {
        warn(ExportWarning::Kind::NoFomodChoices, mod);
      }
    }

    void addNexus(ModEntry& entry, const ExportMod& mod,
                  const std::vector<const ExportFile*>& files)
    {
      bool noMd5 = false, noChoices = false;
      for (const auto* f : files) {
        Source s;
        s.kind         = SourceKind::Nexus;
        s.modId        = f->modId;
        s.fileId       = f->fileId;
        s.md5          = f->md5;
        s.size         = f->size;
        s.fomodChoices = f->fomodChoices;
        entry.sources.push_back(s);
        noMd5     = noMd5 || f->md5.isEmpty();
        noChoices = noChoices || (f->fomodWithoutChoices && !f->fomodChoices);
      }

      // one warning of each kind per mod
      if (noMd5) {
        warn(ExportWarning::Kind::NoMd5, mod.name);
      }
      if (noChoices) {
        warn(ExportWarning::Kind::NoFomodChoices, mod.name);
      }
      if (!mod.hasInstallOrder && files.size() > 1) {
        warn(ExportWarning::Kind::NoInstallOrder, mod.name);
      }
      ++m_result.nexusCount;
    }

    void addManual(ModEntry& entry, const ExportMod& mod)
    {
      Source s;
      s.kind     = SourceKind::Manual;
      s.url      = mod.manualUrl;
      s.fileName = mod.manualFileName;
      s.md5      = mod.manualMd5;
      // the most recent archive of the mod is the one to download
      if (!mod.files.empty()) {
        const auto& last = mod.files.back();
        s.size           = last.size;
        s.fomodChoices   = last.fomodChoices;
        fomodWarning(last, mod.name);
      }
      entry.sources.push_back(s);

      if (mod.manualUrl.isEmpty()) {
        warn(ExportWarning::Kind::ManualWithoutUrl, mod.name);
      }
      ++m_result.manualCount;
    }

    void addBundled(ModEntry& entry, const QStringList& files, const QString& dir)
    {
      Source s;
      s.kind       = SourceKind::Bundled;
      s.bundledDir = bundledDirName(entry.name);
      entry.sources.push_back(s);

      const QDir root(dir);
      for (const auto& relative : files) {
        m_result.package.bundledFiles.emplace_back(s.bundledDir + "/" + relative,
                                                   root.absoluteFilePath(relative));
      }
      ++m_result.bundledCount;
    }
  };

}  // namespace

bool isExcludedFromPackage(const QString& relativePath)
{
  const auto parts =
      QString(relativePath).replace('\\', '/').split('/', Qt::SkipEmptyParts);
  if (parts.isEmpty()) {
    return false;
  }

  for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
    if (parts[i].compare(QLatin1String("saves"), Qt::CaseInsensitive) == 0) {
      return true;
    }
  }

  const QString& name = parts.back();
  return name.compare(QLatin1String("ModOrganizer.ini"), Qt::CaseInsensitive) == 0 ||
         name.endsWith(QLatin1String(".ess"), Qt::CaseInsensitive) ||
         name.endsWith(QLatin1String(".skse"), Qt::CaseInsensitive) ||
         name.endsWith(QLatin1String(".log"), Qt::CaseInsensitive);
}

QString overwriteModName(const QString& listName)
{
  return "[" + listName + "] Overwrite";
}

ExportResult buildExport(const std::vector<ExportMod>& modsLowToHigh,
                         const std::vector<PluginEntry>& plugins,
                         const std::map<QString, QByteArray>& profileInis,
                         const std::vector<ExecutableEntry>& executables,
                         const ProfileConfig& profile, const PathMarkers& markers,
                         const QString& overwriteDir, const ExportOptions& options,
                         const Header& base)
{
  ExportResult result;
  Plan& plan = result.package.plan;

  plan.header         = base;
  plan.header.name    = options.listName;
  plan.header.author  = options.author;
  plan.header.version = options.version;
  plan.header.origin  = QStringLiteral("mo2list");

  Builder builder(options, result);
  for (const auto& mod : modsLowToHigh) {
    builder.add(mod);
  }
  builder.addOverwrite(overwriteDir);

  plan.plugins = plugins;

  plan.profile          = profile;
  plan.profile.iniFiles = {};
  for (const auto& [name, data] : profileInis) {
    if (isSafeRelative(name) && !isExcludedFromPackage(name)) {
      result.package.profileFiles[name] = sanitizedIni(data, markers);
      plan.profile.iniFiles.append(name);
    }
  }

  for (const auto& e : executables) {
    plan.executables.push_back(sanitizedExecutable(e, markers));
  }

  result.package.readme = options.readme;
  return result;
}

}  // namespace modlists
