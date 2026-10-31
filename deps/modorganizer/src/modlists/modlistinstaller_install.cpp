#include "modlistinstaller.h"

#include "downloadmanager.h"
#include "installationmanager.h"
#include "modinfo.h"
#include "organizercore.h"
#include "settings.h"

#include <uibase/guessedvalue.h>
#include <uibase/imodinterface.h>
#include <uibase/log.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <climits>

// Steps 4 and 5 of the import: the batch installation, in the order of the phases and
// the priorities, with "modlist=<list>" in the meta.ini of every mod it creates.

using namespace MOBase;

namespace modlists
{

namespace
{

  const QString FomodInstaller  = QStringLiteral("Fomod Installer");
  const QString SettingsSection = QStringLiteral("Modlists");
  const QString ModlistKey      = QStringLiteral("modlist");
  // set while a mod of several files misses some of them (a later file failed)
  const QString IncompleteKey   = QStringLiteral("incomplete");
  const QString SeparatorSuffix = QStringLiteral("_separator");

  // setBatch(false), clearInstallerOptions() and setBatchMessages(false) on every way
  // out of the batch, exceptions included
  class BatchGuard
  {
  public:
    BatchGuard(InstallationManager& installer, DownloadManager& downloads)
        : m_installer(installer), m_downloads(downloads)
    {
      m_installer.setBatch(true);
    }

    ~BatchGuard()
    {
      m_installer.setBatch(false);
      m_installer.clearInstallerOptions();
      m_installer.setBatchMergeTarget({});
      m_downloads.setBatchMessages(false);
    }

    BatchGuard(const BatchGuard&)            = delete;
    BatchGuard& operator=(const BatchGuard&) = delete;

  private:
    InstallationManager& m_installer;
    DownloadManager& m_downloads;
  };

  // the mods a plan entry can match: regular mods, not this import's own
  std::vector<InstalledMod>
  installedMods(const std::set<QString, CaseInsensitive>& exclude)
  {
    std::vector<InstalledMod> out;
    for (unsigned int i = 0; i < ModInfo::getNumMods(); ++i) {
      const auto mod = ModInfo::getByIndex(i);
      if (!mod->isRegular() || mod->isBackup() || mod->isOverwrite() ||
          mod->isForeign() || mod->isSeparator() || exclude.count(mod->name())) {
        continue;
      }
      InstalledMod m;
      m.name  = mod->name();
      m.modId = mod->nexusId();
      for (const auto& [modId, fileId] : mod->installedFiles()) {
        if (modId == m.modId || m.modId <= 0) {
          m.fileIds.push_back(fileId);
        }
      }
      m.version = mod->version().isValid() ? mod->version().canonicalString() : "";
      out.push_back(m);
    }
    return out;
  }

  // the user's mod that classify() matched: the first with the same Nexus mod, the
  // one with every file of the entry first
  QString matchedMod(const ModEntry& entry, const std::vector<InstalledMod>& installed)
  {
    if (entry.sources.empty() || entry.sources.front().modId <= 0) {
      return {};
    }
    const int modId = entry.sources.front().modId;

    QString first;
    for (const auto& mod : installed) {
      if (mod.modId != modId) {
        continue;
      }
      if (first.isEmpty()) {
        first = mod.name;
      }
      bool all = false;
      for (const auto& s : entry.sources) {
        if (s.kind != SourceKind::Nexus || s.fileId == 0) {
          continue;
        }
        all = std::find(mod.fileIds.begin(), mod.fileIds.end(), s.fileId) !=
              mod.fileIds.end();
        if (!all) {
          break;
        }
      }
      if (all) {
        return mod.name;
      }
    }
    return first;
  }

  // by a scan, not getIndex(): a mod made by createMod() is in the collection but not
  // in the index by name until the next refresh
  ModInfo::Ptr modByName(const QString& name)
  {
    for (unsigned int i = 0; i < ModInfo::getNumMods(); ++i) {
      auto mod = ModInfo::getByIndex(i);
      if (mod->name().compare(name, Qt::CaseInsensitive) == 0) {
        return mod;
      }
    }
    return nullptr;
  }

  // moves the files of `from` into `to` (same file system: rename, else copy);
  // existing files of `to` with the same path are replaced
  bool moveTree(const QString& from, const QString& to)
  {
    const QDir source(from);
    QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString path   = it.next();
      const QString target = QDir(to).filePath(source.relativeFilePath(path));

      if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        log::error("cannot create the folder of {}", target);
        return false;
      }
      if (QFile::exists(target) && !QFile::remove(target)) {
        log::error("cannot replace {}", target);
        return false;
      }
      if (!QFile::rename(path, target) && !QFile::copy(path, target)) {
        log::error("cannot move {} to {}", path, target);
        return false;
      }
    }
    return true;
  }

}  // namespace

bool ModlistInstaller::willInstall(int entry) const
{
  const ModEntry& e = plan().mods[entry];
  if (e.isSeparator) {
    return false;
  }

  auto it = m_entries.find(entry);
  if (it == m_entries.end()) {
    return false;
  }
  if (it->second.status == RowStatus::AlreadyInstalled) {
    return false;
  }
  if (it->second.status == RowStatus::DifferentVersion) {
    auto choice = m_pending.versionChoices.find(entry);
    return choice == m_pending.versionChoices.end() ||
           choice->second != VersionChoice::KeepMine;
  }
  return true;
}

void ModlistInstaller::classifyEntries(const std::vector<int>& entries)
{
  // neither this import's mods nor the ones it left incomplete count as the user's
  auto exclude = m_ownMods;
  for (unsigned int i = 0; i < ModInfo::getNumMods(); ++i) {
    const auto mod = ModInfo::getByIndex(i);
    if (mod->isRegular() &&
        mod->pluginSetting(SettingsSection, ModlistKey, QVariant()).toString() ==
            plan().header.name &&
        mod->pluginSetting(SettingsSection, IncompleteKey, false).toBool()) {
      exclude.insert(mod->name());
    }
  }

  const auto installed = installedMods(exclude);
  for (int e : entries) {
    const ModEntry& entry = plan().mods[e];
    EntryWork work;
    work.status = classify(entry, installed, m_pending.destination);
    if (work.status == RowStatus::AlreadyInstalled ||
        work.status == RowStatus::DifferentVersion) {
      work.userMod = matchedMod(entry, installed);
    }
    work.sources.resize(entry.sources.size());
    m_entries[e] = std::move(work);
  }
}

void ModlistInstaller::resolveEarlierEntries()
{
  const QString listName = plan().header.name;
  const auto installed   = installedMods({});
  std::vector<int> kept;

  for (int e : m_pending.installedEntries) {
    if (e < 0 || e >= static_cast<int>(plan().mods.size())) {
      continue;
    }
    const ModEntry& entry = plan().mods[e];
    const QString base    = installName(entry, listName, {});
    QString found;
    bool own = false;

    if (entry.isSeparator) {
      if (ModInfo::getIndex(base + SeparatorSuffix) != UINT_MAX) {
        found = base + SeparatorSuffix;
        own   = true;
      }
    } else {
      found = findOwnMod(entry, false);
      own   = !found.isEmpty();
      if (found.isEmpty()) {
        found = matchedMod(entry, installed);
      }
    }

    if (found.isEmpty()) {
      // gone since: installed again by this run
      log::warn("modlist import: the mod of {} is gone, it is installed again",
                entry.name);
      continue;
    }

    kept.push_back(e);
    m_modOfEntry[e] = found;
    if (own) {
      m_ownMods.insert(found);
      m_assignedNames.insert(found);
    }

    auto choice = m_pending.versionChoices.find(e);
    if (own && choice != m_pending.versionChoices.end() &&
        choice->second == VersionChoice::UseList) {
      const QString mine = matchedMod(entry, installedMods(m_ownMods));
      if (!mine.isEmpty()) {
        m_replacedUserMods.push_back(mine);
      }
    }

    addRow(e, own ? Outcome::Installed : Outcome::Reused,
           tr("Installed before Mod Organizer was restarted"));
  }

  m_pending.installedEntries = kept;
}

QString ModlistInstaller::regularInstallName(
    const ModEntry& entry, const std::set<QString, CaseInsensitive>& taken) const
{
  const QString name = installName(entry, plan().header.name, taken);
  if (ModInfo::isRegularName(name)) {
    return name;
  }
  ModEntry renamed = entry;
  renamed.name     = name + " (mod)";
  return installName(renamed, plan().header.name, taken);
}

QString ModlistInstaller::findOwnMod(const ModEntry& entry, bool incompleteOnly) const
{
  const QString listName = plan().header.name;
  const QString base     = regularInstallName(entry, {});

  for (unsigned int i = 0; i < ModInfo::getNumMods(); ++i) {
    const auto mod     = ModInfo::getByIndex(i);
    const QString name = mod->name();
    if (!mod->isRegular() || mod->isSeparator() ||
        mod->pluginSetting(SettingsSection, ModlistKey, QVariant()).toString() !=
            listName) {
      continue;
    }
    if (incompleteOnly &&
        !mod->pluginSetting(SettingsSection, IncompleteKey, false).toBool()) {
      continue;
    }
    if (name.compare(base, Qt::CaseInsensitive) == 0 ||
        name.startsWith(base + " [", Qt::CaseInsensitive)) {
      return name;
    }
  }
  return {};
}

std::set<QString, CaseInsensitive> ModlistInstaller::existingModNames() const
{
  std::set<QString, CaseInsensitive> names;
  for (unsigned int i = 0; i < ModInfo::getNumMods(); ++i) {
    names.insert(ModInfo::getByIndex(i)->name());
  }
  const QDir mods(m_core.settings().paths().mods());
  for (const auto& name : mods.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    names.insert(name);
  }
  return names;
}

bool ModlistInstaller::safeModName(const QString& name) const
{
  if (name.isEmpty() || name == "." || name == ".." || name.contains('/') ||
      name.contains('\\')) {
    return false;
  }

  const QString root = QDir(m_core.settings().paths().mods()).canonicalPath();
  if (root.isEmpty()) {
    return false;
  }

  const QFileInfo target(QDir(root).filePath(name));
  if (target.exists()) {
    // a link to somewhere else is not inside the mods folder
    return target.canonicalFilePath().startsWith(root + "/");
  }
  return QDir::cleanPath(target.absolutePath()) == root && target.fileName() == name;
}

void ModlistInstaller::setModlistSetting(IModInterface* mod) const
{
  if (mod != nullptr) {
    mod->setPluginSetting(SettingsSection, ModlistKey, plan().header.name);
  }
}

void ModlistInstaller::installEntries()
{
  InstallationManager& installer = *m_core.installationManager();
  BatchGuard guard(installer, *m_core.downloadManager());
  installer.setModsDirectory(m_core.settings().paths().mods());

  for (const auto& name : existingModNames()) {
    m_assignedNames.insert(name);
  }

  const int total = static_cast<int>(m_work.size());
  for (int i = 0; i < total; ++i) {
    const int e = m_work[i];
    emit progress(i, total, plan().mods[e].name);
    try {
      installEntry(e);
    } catch (const std::exception& ex) {
      addRow(e, Outcome::Failed, QString::fromUtf8(ex.what()));
    } catch (...) {
      addRow(e, Outcome::Failed, tr("Unknown error"));
    }
  }
  emit progress(total, total, tr("Installed"));
}

void ModlistInstaller::installEntry(int e)
{
  const ModEntry& entry = plan().mods[e];
  EntryWork& work       = m_entries[e];

  if (entry.isSeparator) {
    const QString name = installName(entry, plan().header.name, {}) + SeparatorSuffix;
    if (!safeModName(name)) {
      addRow(e, Outcome::Failed, QStringLiteral("Unsafe mod name"));
      return;
    }
    if (ModInfo::getIndex(name) != UINT_MAX || QDir(m_core.settings().paths().mods())
                                                   .exists(name)) {
      // a separator holds no files, the user's one serves the list too
      addRow(e, Outcome::Reused);
      markInstalled(e, name);
      return;
    }

    GuessedValue<QString> guessed;
    guessed.update(name, GUESS_USER);
    IModInterface* mod = m_core.createMod(guessed);
    if (mod == nullptr) {
      addRow(e, Outcome::Failed, m_core.installationManager()->lastInstallerDetail());
      return;
    }
    setModlistSetting(mod);
    m_ownMods.insert(name);
    m_assignedNames.insert(name);
    addRow(e, Outcome::Installed);
    markInstalled(e, name);
    return;
  }

  if (!willInstall(e)) {
    // AlreadyInstalled, or DifferentVersion and the user keeps theirs
    const QString detail = work.userMod.compare(entry.name, Qt::CaseInsensitive) == 0
                               ? QString()
                               : tr("Your mod \"%1\"").arg(work.userMod);
    addRow(e, Outcome::Reused, detail);
    markInstalled(e, work.userMod);
    return;
  }

  for (std::size_t s = 0; s < entry.sources.size(); ++s) {
    SourceWork& sw = work.sources[s];
    if (sw.state == SourceState::Skipped) {
      addRow(e, Outcome::Skipped, sw.detail);
      return;
    }
    if (sw.state != SourceState::Ready) {
      addRow(e, Outcome::Failed, sw.detail.isEmpty() ? tr("Not downloaded") : sw.detail);
      return;
    }
    if (entry.sources[s].kind != SourceKind::Bundled) {
      const QString problem = checkArchive(entry.sources[s], sw);
      if (!problem.isEmpty()) {
        addRow(e, Outcome::Failed, problem);
        return;
      }
    }
  }

  if (entry.sources.empty()) {
    addRow(e, Outcome::Failed, tr("The list has no file for this mod"));
    return;
  }

  // a mod a failed earlier attempt left incomplete gets the files again, merged
  auto partialIt = m_partialOfEntry.find(e);
  QString name   = partialIt != m_partialOfEntry.end() &&
                         QDir(m_core.settings().paths().mods()).exists(partialIt->second)
                       ? partialIt->second
                       : findOwnMod(entry, true);
  const bool mergeFirst = !name.isEmpty();
  if (!mergeFirst) {
    name = regularInstallName(entry, m_assignedNames);
  }
  m_assignedNames.insert(name);
  if (!safeModName(name)) {
    addRow(e, Outcome::Failed, QStringLiteral("Unsafe mod name"));
    return;
  }

  QString detail, partial;
  const auto installed = installSources(e, name, mergeFirst, partial, detail);
  if (!installed) {
    if (!partial.isEmpty()) {
      // this list's mod: a retry merges into it instead of making "<name> [<list>]"
      m_ownMods.insert(partial);
      m_assignedNames.insert(partial);
      m_partialOfEntry[e] = partial;
    }
    addRow(e, Outcome::Failed, detail);
    return;
  }

  m_partialOfEntry.erase(e);
  m_ownMods.insert(*installed);
  m_assignedNames.insert(*installed);

  QString note;
  if (work.status == RowStatus::DifferentVersion && !work.userMod.isEmpty()) {
    // the user's mod stays, off in this profile
    m_replacedUserMods.push_back(work.userMod);
    note = tr("Installed next to your \"%1\"").arg(work.userMod);
  } else if (*installed != entry.name) {
    note = tr("Installed as \"%1\"").arg(*installed);
  }
  addRow(e, Outcome::Installed, note);
  markInstalled(e, *installed);
}

std::optional<QString> ModlistInstaller::installSources(int e, const QString& name,
                                                        bool mergeFirst,
                                                        QString& partial,
                                                        QString& detail)
{
  const ModEntry& entry          = plan().mods[e];
  EntryWork& work                = m_entries[e];
  InstallationManager& installer = *m_core.installationManager();

  ModInfo::Ptr current;
  QString modName = name;
  // the mod folder exists and has files of this list
  bool hasFiles = mergeFirst;

  const auto markIncomplete = [&](bool incomplete) {
    ModInfo::Ptr mod = current != nullptr ? current : modByName(modName);
    if (mod != nullptr) {
      mod->setPluginSetting(SettingsSection, IncompleteKey, incomplete);
    }
  };

  for (std::size_t s = 0; s < entry.sources.size(); ++s) {
    const Source& source = entry.sources[s];
    // the next files go into the mod the first one made
    const bool merge = hasFiles;
    installer.setBatchMergeTarget(merge ? modName : QString());
    installer.clearInstallerOptions();

    const QString file = source.kind == SourceKind::Bundled
                             ? source.bundledDir
                             : QFileInfo(work.sources[s].archive).fileName();
    const auto incomplete = [&](const QString& why) -> std::optional<QString> {
      if (hasFiles) {
        partial = modName;
        detail  = tr("%1: %2 (the mod \"%3\" is incomplete, a retry installs into "
                     "it)")
                     .arg(file, why, modName);
        markIncomplete(true);
      } else {
        detail = why;
      }
      return std::nullopt;
    };

    try {
      if (source.kind == SourceKind::Bundled) {
        QString why;
        if (!installBundled(source, modName, merge, why)) {
          return incomplete(why);
        }
        hasFiles = true;
        continue;
      }

      if (source.fomodChoices) {
        installer.setInstallerOptions(
            FomodInstaller,
            {{"choices", QString::fromUtf8(QJsonDocument(*source.fomodChoices)
                                               .toJson(QJsonDocument::Compact))}});
      }

      if (installer.isRunning()) {
        return incomplete(tr("Another installation is in progress"));
      }

      // not through installDownload(): its errors open a dialog
      ModInfo::Ptr mod =
          m_core.installArchive(work.sources[s].archive, -1, merge, current, modName);
      if (mod == nullptr) {
        QString why = installer.lastInstallerDetail();
        if (why.isEmpty()) {
          why = tr("The installation did not finish");
        }
        return incomplete(why);
      }

      current  = mod;
      modName  = mod->name();
      hasFiles = true;
      setModlistSetting(mod.data());
    } catch (const std::exception& ex) {
      return incomplete(QString::fromUtf8(ex.what()));
    }
  }

  if (mergeFirst) {
    markIncomplete(false);
  }
  return modName;
}

bool ModlistInstaller::installBundled(const Source& source, const QString& name,
                                      bool merge, QString& detail)
{
  // a fresh folder next to the instance, never an existing mod folder
  QTemporaryDir temp(QDir(m_core.settings().paths().base())
                         .filePath("modlist-bundled-XXXXXX"));
  if (!temp.isValid()) {
    detail = tr("Cannot create a temporary folder");
    return false;
  }

  // throws on I/O errors and on entries that would leave the folder
  m_package.extractBundled(source.bundledDir, temp.path());

  if (!merge) {
    GuessedValue<QString> guessed;
    guessed.update(name, GUESS_USER);
    IModInterface* mod = m_core.createMod(guessed);
    if (mod == nullptr) {
      detail = m_core.installationManager()->lastInstallerDetail();
      if (detail.isEmpty()) {
        detail = tr("Cannot create the mod");
      }
      return false;
    }
    setModlistSetting(mod);
  }

  if (!safeModName(name)) {
    detail = QStringLiteral("Unsafe mod name");
    return false;
  }

  const QString target = QDir(m_core.settings().paths().mods()).filePath(name);
  if (!moveTree(temp.path(), target)) {
    detail = tr("Cannot move the bundled files into the mod");
    return false;
  }
  return true;
}

}  // namespace modlists
