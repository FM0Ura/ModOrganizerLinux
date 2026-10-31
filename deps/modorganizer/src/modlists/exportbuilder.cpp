#include "exportbuilder.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>

namespace modlists
{

namespace
{

  // the canonical path of a folder, empty if it is not one; an empty path would be
  // the current directory
  QString canonicalRoot(const QString& dir)
  {
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
      return {};
    }
    return QFileInfo(dir).canonicalFilePath();
  }

  // a regular file, not a symbolic link, that really is under root (canonical): a
  // symbolic link, or a file under a linked folder, could point outside the mod
  bool isFileInside(const QString& root, const QFileInfo& info)
  {
    if (root.isEmpty() || !info.isFile() || info.isSymLink()) {
      return false;
    }
    const QString canonical = info.canonicalFilePath();
    return !canonical.isEmpty() && canonical.startsWith(root + '/');
  }

  // the files to bundle under dir, relative to it and sorted
  QStringList bundledFilesOf(const QString& dir)
  {
    QStringList out;
    const QString canonical = canonicalRoot(dir);
    if (canonical.isEmpty()) {
      return out;
    }
    const QDir root(dir);
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      it.next();
      const QFileInfo info = it.fileInfo();
      if (!isFileInside(canonical, info)) {
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

  // an environment variable whose name looks like it holds a credential
  bool isSecretVariable(const QString& variable)
  {
    static const char* const words[] = {"KEY",  "TOKEN",  "SECRET", "PASS",
                                        "AUTH", "COOKIE", "SESSION"};
    const QString name               = variable.section('=', 0, 0);
    return std::any_of(std::begin(words), std::end(words), [&](const char* w) {
      return name.contains(QLatin1String(w), Qt::CaseInsensitive);
    });
  }

  // the paths with markers, without the secret variables; `dropped` tells if any was
  ExecutableEntry sanitizedExecutable(ExecutableEntry e, const PathMarkers& markers,
                                      bool& dropped)
  {
    e.binary           = toMarkers(e.binary, markers);
    e.arguments        = toMarkers(e.arguments, markers);
    e.workingDirectory = toMarkers(e.workingDirectory, markers);

    QStringList environment;
    dropped = false;
    for (const auto& variable : e.environment) {
      if (isSecretVariable(variable)) {
        dropped = true;
      } else {
        environment.append(toMarkers(variable, markers));
      }
    }
    e.environment = environment;
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
        // the files without ids cannot be downloaded by the importer
        if (nexus.size() < mod.files.size()) {
          warn(ExportWarning::Kind::MixedSources, mod.name);
        }
        addNexus(entry, mod, nexus);
      } else {
        switch (mod.nonNexus) {
        case NonNexusChoice::Manual:
          addManual(entry, mod);
          break;
        case NonNexusChoice::Bundled: {
          // the files of a Nexus mod are never redistributed
          if (mod.nexusId > 0) {
            exclude(ExportWarning::Kind::NexusModNotBundled, mod.name);
            return;
          }
          const auto files = bundledFilesOf(mod.absolutePath);
          if (files.isEmpty()) {
            exclude(ExportWarning::Kind::EmptyBundled, mod.name);
            return;
          }
          addBundled(entry, files, mod.absolutePath);
          break;
        }
        case NonNexusChoice::Exclude:
          exclude(ExportWarning::Kind::Excluded, mod.name);
          return;
        }
      }

      m_result.package.plan.mods.push_back(std::move(entry));
    }

    void addOverwrite(const QString& overwriteDir)
    {
      QStringList files;
      const QString root = canonicalRoot(overwriteDir);
      if (root.isEmpty()) {
        return;
      }
      for (const auto& relative : m_options.overwriteFiles) {
        if (isSafeRelative(relative) && !isExcludedFromPackage(relative)) {
          const QFileInfo info(QDir(overwriteDir).filePath(relative));
          if (isFileInside(root, info)) {
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

    void exclude(ExportWarning::Kind kind, const QString& mod)
    {
      warn(kind, mod);
      ++m_result.excludedCount;
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
      if (mod.manualMd5.isEmpty()) {
        warn(ExportWarning::Kind::NoMd5, mod.name);
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
    bool dropped = false;
    plan.executables.push_back(sanitizedExecutable(e, markers, dropped));
    if (dropped) {
      result.warnings.push_back({ExportWarning::Kind::SecretEnvDropped, e.title});
    }
  }

  result.package.readme = options.readme;
  return result;
}

}  // namespace modlists
