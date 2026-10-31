#ifndef MODLISTS_MODLISTINSTALLER_H
#define MODLISTS_MODLISTINSTALLER_H

#include "guidedmatch.h"
#include "importsummary.h"
#include "mo2listzip.h"
#include "pendingimport.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <boost/signals2.hpp>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class DownloadManager;
class OrganizerCore;
namespace MOBase
{
class IModInterface;
}

// Turns the plan of a .mo2list into installed mods in the current profile: reuses or
// downloads the archives, installs them without dialogs (InstallationManager batch
// mode), then applies the mod order, the plugins, the INIs and the executables, and
// reports everything in a Summary. The destination profile must be the current
// profile (start() selects PendingImport::profileName when it is not).
//
// The record of the import (<instance>/modlist-import.json) is rewritten after each
// installed entry, so an interrupted import can be resumed without installing twice.
namespace modlists
{

class ModlistInstaller : public QObject
{
  Q_OBJECT

public:
  ModlistInstaller(OrganizerCore& core, Mo2ListPackage package, PendingImport pending,
                   QWidget* parent);
  ~ModlistInstaller() override;

  // installs the entries of the record that are not installed yet
  void start();

  // installs these entries again (indices into Plan::mods, see retryable()); the
  // INIs and the executables are not applied a second time
  void retry(std::vector<int> entries);

  // Guided downloads (the window of Task 9). The nxm:// downloads of the items are
  // matched by the installer itself (DownloadManager::onNXMDownloadQueued); a manual
  // source gets its file here.
  void setManualFile(int entryIndex, int sourceIndex, const QString& path);
  // the guided window closed: Skipped items are skipped, Pending ones not downloaded;
  // the installation starts when the queued downloads are over
  void guidedDownloadsFinished(const std::vector<GuidedItem>& items);
  // gives up on the downloads still running or queued (they become Failed)
  void stopWaitingForDownloads();

  const Plan& plan() const { return m_package.plan(); }
  // SummaryRow::mod -> index into Plan::mods, for retryable()
  const std::map<QString, int>& entryOfMod() const { return m_entryOfMod; }
  // the archive of each source of an entry, as far as it was found or downloaded;
  // "Install manually" uses them
  std::vector<QString> archivesOf(int entryIndex) const;

signals:
  void progress(int done, int total, QString what);
  void needsGuidedDownloads(std::vector<GuidedItem> items);
  void finished(Summary summary);

private:
  enum class SourceState
  {
    Missing,
    Waiting,
    Ready,
    Failed,
    Skipped
  };

  struct SourceWork
  {
    SourceState state = SourceState::Missing;
    QString archive;
    // the MD5 was already compared when the download was reused
    bool verified  = false;
    int downloadId = 0;
    QString detail;
  };

  struct EntryWork
  {
    RowStatus status = RowStatus::New;
    // the user's mod for AlreadyInstalled and DifferentVersion
    QString userMod;
    std::vector<SourceWork> sources;
  };

  // modlistinstaller.cpp: the flow
  void run(std::vector<int> entries);
  // afterDownloads() once nothing is waiting any more, once per run
  void maybeAfterDownloads();
  void afterDownloads();
  void afterModOrder();
  void finish();
  // runs f; an exception becomes a Failed row named `what`
  void guarded(const QString& what, const std::function<void()>& f);
  void fail(const QString& message);
  void savePending() const;
  QString pendingPath() const;
  void addRow(int entry, Outcome outcome, const QString& detail = {});
  void markInstalled(int entry, const QString& modName);
  QString rowName(int entry);

  // modlistinstaller_downloads.cpp: steps 1 to 3
  void findDownloads();
  void requestDownloads();
  void startQueuedDownloads();
  void onNXMQueued(int modId, int fileId, int downloadId);
  void onDownloadDone(int downloadId, bool ok);
  bool waitingForDownloads() const;
  // empty when the archive matches the size and MD5 of the plan
  QString checkArchive(const Source& source, SourceWork& work);

  // modlistinstaller_install.cpp: steps 4 and 5
  void installEntries();
  void installEntry(int entry);
  bool willInstall(int entry) const;
  void setModlistSetting(MOBase::IModInterface* mod) const;
  void classifyEntries(const std::vector<int>& entries);
  void resolveEarlierEntries();
  // the name installName() gives, made regular: a name MO2 would read as a separator
  // or a backup ("..._separator", "...backup2") gets " (mod)"
  QString regularInstallName(const ModEntry& entry,
                             const std::set<QString, CaseInsensitive>& taken) const;
  // a mod of this list for the entry, installed earlier (complete or partial)
  // incompleteOnly: only one a failed file left incomplete
  QString findOwnMod(const ModEntry& entry, bool incompleteOnly) const;
  // `partial` is set to the mod when some files were installed before the failure;
  // `mergeFirst` installs the first file into the existing mod `name` too
  std::optional<QString> installSources(int entry, const QString& name,
                                        bool mergeFirst, QString& partial,
                                        QString& detail);
  bool installBundled(const Source& source, const QString& name, bool merge,
                      QString& detail);
  bool safeModName(const QString& name) const;
  std::set<QString, CaseInsensitive> existingModNames() const;

  // modlistinstaller_profile.cpp: steps 6 to 9
  void applyModOrder();
  void applyPlugins();
  void applyInis();
  void applyExecutables();
  // runs f once no directory refresh is running and the lists are refreshed
  void whenIdle(std::function<void()> f);
  void refreshThen(std::function<void()> f);

  OrganizerCore& m_core;
  Mo2ListPackage m_package;
  PendingImport m_pending;
  QPointer<QWidget> m_parent;

  // this run
  std::vector<int> m_work;
  std::map<int, EntryWork> m_entries;
  std::multimap<int, std::pair<int, int>> m_downloads;
  std::deque<std::pair<int, int>> m_downloadQueue;
  // the Premium downloads started by the installer and not over yet
  std::set<int> m_activeIds;
  bool m_startingDownload = false;
  bool m_guided           = false;
  bool m_guidedDone       = false;
  bool m_downloadsOver    = false;
  bool m_installing       = false;
  std::vector<boost::signals2::scoped_connection> m_connections;
  // DownloadManager::setBatchMessages() while the import waits for downloads
  struct BatchMessages
  {
    explicit BatchMessages(DownloadManager& manager);
    ~BatchMessages();
    DownloadManager& manager;
  };
  std::unique_ptr<BatchMessages> m_batchMessages;

  // all runs
  Summary m_summary;
  std::map<int, std::size_t> m_rowOfEntry;
  std::map<QString, int> m_entryOfMod;
  // installed or reused mod of each entry
  std::map<int, QString> m_modOfEntry;
  // the mods this import installed (not the user's)
  std::set<QString, CaseInsensitive> m_ownMods;
  std::vector<QString> m_replacedUserMods;
  // the mods a failed later file left incomplete, by entry; a retry merges into them
  std::map<int, QString> m_partialOfEntry;
  std::set<QString, CaseInsensitive> m_assignedNames;
  bool m_profileApplied = false;
};

}  // namespace modlists

#endif  // MODLISTS_MODLISTINSTALLER_H
