#include "modlistinstaller.h"

#include "apiuseraccount.h"
#include "downloadmanager.h"
#include "nexusinterface.h"
#include "organizercore.h"
#include "settings.h"

#include <uibase/iplugingame.h>
#include <uibase/log.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

// Steps 1 to 3 of the import: reuse the archives already downloaded, download the
// rest (Premium) or ask for the guided downloads, and compare the size and MD5 of the
// archives with the plan.

using namespace MOBase;

namespace modlists
{

namespace
{

  // Premium downloads running at the same time
  constexpr std::size_t MaxParallelDownloads = 3;

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

  // the archives of a downloads folder, with the Nexus ids of their .meta
  std::vector<DownloadCandidate> candidatesIn(const QString& dir)
  {
    std::vector<DownloadCandidate> out;
    if (dir.isEmpty()) {
      return out;
    }

    const QDir downloads(dir);
    for (const auto& info : downloads.entryInfoList(QDir::Files | QDir::Hidden)) {
      const QString name = info.fileName();
      if (name.endsWith(".meta", Qt::CaseInsensitive) ||
          name.endsWith(".unfinished", Qt::CaseInsensitive)) {
        continue;
      }

      DownloadCandidate c;
      c.path = info.absoluteFilePath();
      c.size = info.size();

      const QString meta = c.path + ".meta";
      if (QFile::exists(meta)) {
        const QSettings settings(meta, QSettings::IniFormat);
        c.modId  = settings.value("modID", 0).toInt();
        c.fileId = settings.value("fileID", 0).toInt();
      }
      out.push_back(c);
    }
    return out;
  }

  // "<base> (2).<ext>", ... when the name is taken
  QString freePath(const QDir& dir, const QString& fileName)
  {
    QString path = dir.filePath(fileName);
    if (!QFile::exists(path)) {
      return path;
    }
    const QFileInfo info(fileName);
    const QString suffix =
        info.completeSuffix().isEmpty() ? QString() : "." + info.completeSuffix();
    for (int n = 2;; ++n) {
      path = dir.filePath(QString("%1 (%2)%3").arg(info.baseName()).arg(n).arg(suffix));
      if (!QFile::exists(path)) {
        return path;
      }
    }
  }

  bool isDownloaded(SourceKind kind)
  {
    return kind == SourceKind::Nexus || kind == SourceKind::Manual;
  }

}  // namespace

void ModlistInstaller::findDownloads()
{
  const QString instanceDir = m_core.settings().paths().downloads();
  const QString otherDir =
      m_pending.sourceDownloadsDir.isEmpty() ||
              QFileInfo(m_pending.sourceDownloadsDir).canonicalFilePath() ==
                  QFileInfo(instanceDir).canonicalFilePath()
          ? QString()
          : m_pending.sourceDownloadsDir;

  const auto here  = candidatesIn(instanceDir);
  const auto there = candidatesIn(otherDir);

  std::map<QString, QString> hashes;
  const auto cachedMd5 = [&](const QString& path) {
    auto it = hashes.find(path);
    if (it == hashes.end()) {
      it = hashes.emplace(path, md5Of(path)).first;
    }
    return it->second;
  };

  int done = 0;
  for (int e : m_work) {
    emit progress(done++, static_cast<int>(m_work.size()),
                  tr("Looking for downloaded files"));
    if (!willInstall(e)) {
      continue;
    }

    const auto& entry = plan().mods[e];
    auto& work        = m_entries[e];
    for (std::size_t s = 0; s < entry.sources.size(); ++s) {
      const Source& source = entry.sources[s];
      SourceWork& sw       = work.sources[s];

      if (source.kind == SourceKind::Bundled) {
        sw.state = SourceState::Ready;
        continue;
      }

      if (auto found = findReusableDownload(source, here, cachedMd5)) {
        sw.state    = SourceState::Ready;
        sw.archive  = *found;
        sw.verified = !source.md5.isEmpty();
        continue;
      }

      auto found = findReusableDownload(source, there, cachedMd5);
      if (!found) {
        continue;
      }

      // from another instance: copied with its .meta, the original stays
      const QString target = freePath(QDir(instanceDir), QFileInfo(*found).fileName());
      if (!QFile::copy(*found, target)) {
        log::warn("cannot copy {} to {}, it will be downloaded", *found, target);
        continue;
      }
      if (QFile::exists(*found + ".meta") &&
          !QFile::copy(*found + ".meta", target + ".meta")) {
        log::warn("cannot copy the .meta of {}", *found);
      }
      sw.state    = SourceState::Ready;
      sw.archive  = target;
      sw.verified = !source.md5.isEmpty();
    }
  }
}

void ModlistInstaller::requestDownloads()
{
  std::vector<GuidedItem> guided;
  std::vector<std::pair<int, int>> nexus;

  for (int e : m_work) {
    if (!willInstall(e)) {
      continue;
    }
    const auto& entry = plan().mods[e];
    for (std::size_t s = 0; s < entry.sources.size(); ++s) {
      const Source& source = entry.sources[s];
      SourceWork& sw       = m_entries[e].sources[s];
      if (sw.state != SourceState::Missing || !isDownloaded(source.kind)) {
        continue;
      }

      sw.state = SourceState::Waiting;
      if (source.kind == SourceKind::Nexus && source.modId > 0 && source.fileId > 0) {
        nexus.emplace_back(e, static_cast<int>(s));
      } else if (source.kind == SourceKind::Nexus) {
        sw.state  = SourceState::Failed;
        sw.detail = tr("The list has no Nexus file for this mod");
      } else {
        GuidedItem item;
        item.entryIndex  = e;
        item.sourceIndex = static_cast<int>(s);
        guided.push_back(item);
      }
    }
  }

  if (nexus.empty() && guided.empty()) {
    maybeAfterDownloads();
    return;
  }

  DownloadManager& downloads = *m_core.downloadManager();
  m_batchMessages            = std::make_unique<BatchMessages>(downloads);

  m_connections.emplace_back(downloads.onNXMDownloadQueued(
      [this](const QString&, int modId, int fileId, int downloadId) {
        onNXMQueued(modId, fileId, downloadId);
      }));
  m_connections.emplace_back(downloads.onDownloadComplete([this](int id) {
    onDownloadDone(id, true);
  }));
  m_connections.emplace_back(downloads.onDownloadFailed([this](int id) {
    onDownloadDone(id, false);
  }));

  const bool premium = NexusInterface::instance().getAPIUserAccount().type() ==
                       APIUserAccountTypes::Premium;

  if (premium) {
    m_downloadQueue.assign(nexus.begin(), nexus.end());
  } else {
    for (const auto& [e, s] : nexus) {
      const Source& source = plan().mods[e].sources[s];
      GuidedItem item;
      item.entryIndex  = e;
      item.sourceIndex = s;
      item.modId       = source.modId;
      item.fileId      = source.fileId;
      guided.push_back(item);
    }
  }

  // the guided items in the order of the installation
  m_guided = !guided.empty();
  startQueuedDownloads();
  if (m_guided) {
    emit needsGuidedDownloads(guided);
  }

  maybeAfterDownloads();
}

void ModlistInstaller::startQueuedDownloads()
{
  const IPluginGame* game = m_core.managedGame();
  const QString gameName  = game != nullptr ? game->gameShortName() : QString();

  while (m_activeIds.size() < MaxParallelDownloads && !m_downloadQueue.empty()) {
    const auto [e, s] = m_downloadQueue.front();
    m_downloadQueue.pop_front();

    const Source& source = plan().mods[e].sources[s];
    SourceWork& sw       = m_entries[e].sources[s];

    m_startingDownload = true;
    int id             = 0;
    try {
      id = m_core.downloadManager()->startDownloadNexusFile(gameName, source.modId,
                                                            source.fileId);
    } catch (const std::exception& ex) {
      log::error("cannot start the download of {}/{}: {}", source.modId,
                 source.fileId, ex.what());
    }
    m_startingDownload = false;

    if (id <= 0) {
      sw.state  = SourceState::Failed;
      sw.detail = tr("The download could not be started");
      continue;
    }

    sw.downloadId = id;
    m_downloads.emplace(id, std::make_pair(e, s));
    m_activeIds.insert(id);
  }
}

void ModlistInstaller::onNXMQueued(int modId, int fileId, int downloadId)
{
  // the Premium downloads are tracked by startQueuedDownloads()
  if (m_startingDownload || downloadId <= 0) {
    return;
  }

  // every waiting source of that file, the same file can be in two entries
  for (auto& [e, work] : m_entries) {
    const auto& sources = plan().mods[e].sources;
    for (std::size_t s = 0; s < work.sources.size(); ++s) {
      SourceWork& sw = work.sources[s];
      if (sw.state == SourceState::Waiting && sw.downloadId == 0 &&
          sources[s].kind == SourceKind::Nexus && sources[s].modId == modId &&
          sources[s].fileId == fileId) {
        sw.downloadId = downloadId;
        m_downloads.emplace(downloadId, std::make_pair(e, static_cast<int>(s)));
      }
    }
  }
}

void ModlistInstaller::onDownloadDone(int downloadId, bool ok)
{
  const auto range = m_downloads.equal_range(downloadId);
  if (range.first == range.second) {
    return;
  }

  const QString path = ok ? m_core.downloadManager()->downloadPath(downloadId) : "";
  for (auto it = range.first; it != range.second; ++it) {
    SourceWork& sw = m_entries[it->second.first].sources[it->second.second];
    if (sw.state != SourceState::Waiting) {
      continue;
    }
    if (!path.isEmpty() && QFileInfo::exists(path)) {
      sw.state   = SourceState::Ready;
      sw.archive = path;
    } else {
      sw.state  = SourceState::Failed;
      sw.detail = tr("Download failed");
    }
  }
  m_downloads.erase(range.first, range.second);
  m_activeIds.erase(downloadId);

  int ready = 0, total = 0;
  for (const auto& [e, work] : m_entries) {
    for (const auto& sw : work.sources) {
      if (sw.state != SourceState::Missing) {
        ++total;
        ready += sw.state != SourceState::Waiting ? 1 : 0;
      }
    }
  }
  emit progress(ready, total, tr("Downloading"));

  startQueuedDownloads();
  maybeAfterDownloads();
}

bool ModlistInstaller::waitingForDownloads() const
{
  for (const auto& [e, work] : m_entries) {
    for (const auto& sw : work.sources) {
      if (sw.state == SourceState::Waiting) {
        return true;
      }
    }
  }
  return false;
}

void ModlistInstaller::setManualFile(int entryIndex, int sourceIndex,
                                     const QString& path)
{
  auto it = m_entries.find(entryIndex);
  if (it == m_entries.end() || sourceIndex < 0 ||
      sourceIndex >= static_cast<int>(it->second.sources.size())) {
    return;
  }
  SourceWork& sw = it->second.sources[sourceIndex];
  if (sw.state == SourceState::Waiting) {
    sw.state    = SourceState::Ready;
    sw.archive  = path;
    sw.verified = false;
  }
  maybeAfterDownloads();
}

void ModlistInstaller::guidedDownloadsFinished(const std::vector<GuidedItem>& items)
{
  for (const GuidedItem& item : items) {
    auto it = m_entries.find(item.entryIndex);
    if (it == m_entries.end() || item.sourceIndex < 0 ||
        item.sourceIndex >= static_cast<int>(it->second.sources.size())) {
      continue;
    }
    SourceWork& sw = it->second.sources[item.sourceIndex];
    if (sw.state != SourceState::Waiting) {
      continue;
    }

    if (item.state == GuidedItem::Skipped) {
      sw.state  = SourceState::Skipped;
      sw.detail = tr("Skipped by the user");
    } else if (item.state == GuidedItem::Pending || sw.downloadId == 0) {
      // a queued Nexus item whose nxm:// never reached the downloads, or a manual
      // file that was not chosen
      sw.state  = SourceState::Failed;
      sw.detail = tr("Not downloaded");
    }
  }

  m_guidedDone = true;
  maybeAfterDownloads();
}

void ModlistInstaller::stopWaitingForDownloads()
{
  m_downloadQueue.clear();
  for (auto& [e, work] : m_entries) {
    for (auto& sw : work.sources) {
      if (sw.state == SourceState::Waiting) {
        sw.state  = SourceState::Failed;
        sw.detail = tr("Not downloaded");
      }
    }
  }
  m_guidedDone = true;
  maybeAfterDownloads();
}

QString ModlistInstaller::checkArchive(const Source& source, SourceWork& work)
{
  const QFileInfo info(work.archive);
  if (work.archive.isEmpty() || !info.exists()) {
    return tr("The archive is missing");
  }
  if (source.size >= 0 && info.size() != source.size) {
    return QStringLiteral("Checksum mismatch");
  }
  if (!source.md5.isEmpty() && !work.verified) {
    if (QString::compare(md5Of(work.archive), source.md5, Qt::CaseInsensitive) != 0) {
      return QStringLiteral("Checksum mismatch");
    }
    work.verified = true;
  }
  return {};
}

}  // namespace modlists
