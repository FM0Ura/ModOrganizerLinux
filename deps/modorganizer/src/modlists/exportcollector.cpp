#include "exportcollector.h"

#include "executableslist.h"
#include "installorder.h"
#include "modinfo.h"
#include "organizercore.h"
#include "pluginlist.h"
#include "profile.h"
#include "settings.h"

#include <archive/archive.h>
#include <uibase/iplugingame.h>
#include <uibase/log.h>

#ifndef _WIN32
#include "linux/protonlauncher_linux.h"
#endif

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>

#include <algorithm>
#include <memory>
#include <set>

using namespace MOBase;

namespace modlists
{

namespace
{

  // the plugin setting the FOMOD installer records its choices in
  const QString FomodInstallerName = QStringLiteral("Fomod Installer");

  QString cleanDir(const QString& path)
  {
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
  }

  // the archives of the downloads folder by fileKey(): by Nexus ids from their .meta
  // and by archive name
  std::map<QString, QString> indexDownloads(const QString& dir)
  {
    std::map<QString, QString> index;
    const QDir downloads(dir);
    const auto names = downloads.entryList(QDir::Files | QDir::Hidden);

    for (const auto& name : names) {
      if (name.endsWith(".meta", Qt::CaseInsensitive) ||
          name.endsWith(".unfinished", Qt::CaseInsensitive)) {
        continue;
      }
      const QString path = downloads.absoluteFilePath(name);
      index.emplace(fileKey(name), path);

      const QString meta = path + ".meta";
      if (QFile::exists(meta)) {
        const QSettings settings(meta, QSettings::IniFormat);
        const int modId  = settings.value("modID", 0).toInt();
        const int fileId = settings.value("fileID", 0).toInt();
        if (modId > 0 && fileId > 0) {
          index.emplace(fileKey(modId, fileId), path);
        }
      }
    }

    return index;
  }

  // the .meta of the downloads does not keep the MD5
  QString md5Of(const QString& path)
  {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      return {};
    }
    QCryptographicHash hash(QCryptographicHash::Md5);
    if (!hash.addData(&file)) {
      return {};
    }
    return QString::fromLatin1(hash.result().toHex());
  }

  // lists the archive (no extraction) for fomod/ModuleConfig.xml, at its root or under
  // its folders, as the FOMOD installer finds it
  bool hasFomod(Archive& archive, const QString& path)
  {
    if (!archive.open(path.toStdWString(), [] {
          return std::wstring();
        })) {
      return false;
    }

    bool found = false;
    for (const auto* entry : archive.getFileList()) {
      if (entry->isDirectory()) {
        continue;
      }
      const auto name =
          QString::fromStdWString(entry->getArchiveFilePath()).replace('\\', '/');
      if (name.compare("fomod/ModuleConfig.xml", Qt::CaseInsensitive) == 0 ||
          name.endsWith("/fomod/ModuleConfig.xml", Qt::CaseInsensitive)) {
        found = true;
        break;
      }
    }

    archive.close();
    return found;
  }

  // the files of a mod as fileKeys with their ids, in install order; false in the
  // second when installedFiles() had files the recorded order misses
  std::pair<std::vector<ExportFile>, bool> filesOf(const ModInfo& mod)
  {
    std::vector<ExportFile> files;
    std::set<QString> seen;
    const QString archiveName = QFileInfo(mod.installationFile()).fileName();

    const auto add = [&](const QString& key, int modId, int fileId) {
      if (seen.insert(key).second) {
        ExportFile f;
        f.key    = key;
        f.modId  = modId;
        f.fileId = fileId;
        files.push_back(f);
        return true;
      }
      return false;
    };

    for (const auto& key : mod.installedFilesOrder()) {
      const auto parts = key.split('_');
      bool okMod = false, okFile = false;
      const int modId  = parts.size() == 2 ? parts[0].toInt(&okMod) : 0;
      const int fileId = parts.size() == 2 ? parts[1].toInt(&okFile) : 0;
      if (okMod && okFile && modId > 0 && fileId > 0) {
        add(key, modId, fileId);
      } else {
        add(key, 0, 0);
      }
    }

    bool complete = true;
    for (const auto& [modId, fileId] : mod.installedFiles()) {
      if (modId > 0 && fileId > 0) {
        complete = !add(fileKey(modId, fileId), modId, fileId) && complete;
      } else if (!archiveName.isEmpty()) {
        complete = !add(fileKey(archiveName), 0, 0) && complete;
      }
    }

    if (files.empty() && !archiveName.isEmpty()) {
      add(fileKey(archiveName), 0, 0);
    }

    return {files, complete || files.size() <= 1};
  }

  // a separator is the folder "<name>_separator"
  QString separatorName(QString name)
  {
    static const QString suffix = QStringLiteral("_separator");
    if (name.endsWith(suffix)) {
      name.chop(suffix.size());
    }
    return name;
  }

}  // namespace

ExportCollector::ExportCollector(OrganizerCore& core, const Profile& profile)
    : m_core(core), m_profile(profile)
{}

std::vector<ExportMod> ExportCollector::mods() const
{
  std::vector<std::pair<int, unsigned int>> byPriority;
  const auto count = std::min<std::size_t>(ModInfo::getNumMods(), m_profile.numMods());
  for (unsigned int i = 0; i < count; ++i) {
    const auto mod = ModInfo::getByIndex(i);
    if (!mod->isRegular() || mod->isBackup() || mod->isOverwrite() ||
        mod->isForeign()) {
      continue;
    }
    const int priority = m_profile.getModPriority(i);
    if (priority >= 0) {
      byPriority.emplace_back(priority, i);
    }
  }
  std::sort(byPriority.begin(), byPriority.end());

  const auto downloads = indexDownloads(m_core.settings().paths().downloads());
  std::unique_ptr<Archive> archive;
  const IPluginGame* game = m_core.managedGame();

  std::vector<ExportMod> out;
  for (const auto& [priority, index] : byPriority) {
    const auto mod = ModInfo::getByIndex(index);
    ExportMod e;
    e.absolutePath = mod->absolutePath();

    if (mod->isSeparator()) {
      e.name        = separatorName(mod->name());
      e.isSeparator = true;
      out.push_back(e);
      continue;
    }

    e.name    = mod->name();
    e.version = mod->version().isValid() ? mod->version().canonicalString() : QString();
    e.enabled = m_profile.modEnabled(index);
    std::tie(e.files, e.hasInstallOrder) = filesOf(*mod);

    for (auto& f : e.files) {
      const auto choices =
          mod->pluginSetting(FomodInstallerName, fomodChoicesKey(f.key), QVariant())
              .toString();
      if (!choices.isEmpty()) {
        const auto doc = QJsonDocument::fromJson(choices.toUtf8());
        if (doc.isArray()) {
          f.fomodChoices = doc.array();
        } else {
          log::warn("ignoring the FOMOD choices of {} ({}): not a JSON array",
                    mod->name(), f.key);
        }
      }

      const auto it = downloads.find(f.key);
      if (it == downloads.end()) {
        continue;
      }
      f.md5  = md5Of(it->second);
      f.size = QFileInfo(it->second).size();
      if (!f.fomodChoices) {
        if (!archive) {
          archive = CreateArchive();
        }
        f.fomodWithoutChoices = archive->isValid() && hasFomod(*archive, it->second);
      }
    }

    // used when the mod has no Nexus files and is exported as manual
    e.manualFileName = QFileInfo(mod->installationFile()).fileName();
    if (!e.files.empty()) {
      e.manualMd5 = e.files.back().md5;
    }
    if (mod->hasCustomURL() && !mod->url().isEmpty()) {
      e.manualUrl = mod->url();
    } else if (mod->nexusId() > 0 && game != nullptr &&
               !game->gameNexusName().isEmpty()) {
      e.manualUrl = QString("https://www.nexusmods.com/%1/mods/%2")
                        .arg(game->gameNexusName())
                        .arg(mod->nexusId());
    }

    out.push_back(std::move(e));
  }

  return out;
}

PathMarkers ExportCollector::markers() const
{
  PathMarkers m;
  m.instance = cleanDir(m_core.settings().paths().base());
  m.home     = cleanDir(QDir::homePath());

  if (const IPluginGame* game = m_core.managedGame()) {
    m.game = cleanDir(game->gameDirectory().absolutePath());
#ifndef _WIN32
    m.protonPrefix = cleanDir(proton::gamePrefix(game->steamAPPId()));
#endif
  }

  return m;
}

std::map<QString, QByteArray> ExportCollector::profileInis() const
{
  std::map<QString, QByteArray> out;
  const IPluginGame* game = m_core.managedGame();
  if (game == nullptr || !m_profile.localSettingsEnabled()) {
    return out;
  }

  // the profile INIs have the names of iniFiles(), maybe in another case on Linux
  const QDir dir(m_profile.absolutePath());
  const auto present = dir.entryList(QDir::Files | QDir::Hidden);
  for (const auto& iniFile : game->iniFiles()) {
    const QString wanted = QFileInfo(iniFile).fileName();
    for (const auto& name : present) {
      if (name.compare(wanted, Qt::CaseInsensitive) != 0) {
        continue;
      }
      QFile file(dir.absoluteFilePath(name));
      if (file.open(QIODevice::ReadOnly)) {
        out[name] = file.readAll();
      } else {
        log::warn("cannot read the profile INI {}", file.fileName());
      }
      break;
    }
  }

  return out;
}

std::vector<ExecutableEntry> ExportCollector::executables() const
{
  std::vector<ExecutableEntry> out;
  for (const auto& exe : *m_core.executablesList()) {
    ExecutableEntry e;
    e.title            = exe.title();
    e.binary           = exe.binaryInfo().absoluteFilePath();
    e.arguments        = exe.arguments();
    e.workingDirectory = exe.workingDirectory();
    e.steamAppId       = exe.steamAppID();
    e.proton           = exe.proton();
    e.environment      = exe.environment();
    e.showOnToolbar    = exe.isShownOnToolbar();
    out.push_back(e);
  }
  return out;
}

std::vector<PluginEntry> ExportCollector::plugins() const
{
  PluginList* list  = m_core.pluginList();
  QStringList names = list->pluginNames();
  std::sort(names.begin(), names.end(), [&](const QString& a, const QString& b) {
    return list->priority(a) < list->priority(b);
  });

  std::vector<PluginEntry> out;
  for (const auto& name : names) {
    out.push_back({name, list->isEnabled(name)});
  }
  return out;
}

ProfileConfig ExportCollector::profileConfig() const
{
  ProfileConfig config;
  config.localInis  = m_profile.localSettingsEnabled();
  config.localSaves = m_profile.localSavesEnabled();
  return config;
}

Header ExportCollector::baseHeader() const
{
  Header h;
  if (const IPluginGame* game = m_core.managedGame()) {
    h.gameName    = game->gameShortName();
    h.nexusDomain = game->gameNexusName();
  }
  return h;
}

QString ExportCollector::overwriteDir() const
{
  return m_core.overwritePath();
}

}  // namespace modlists
