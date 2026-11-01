#include "modlistinstaller.h"

#include "downloadmanager.h"
#include "organizercore.h"
#include "profile.h"
#include "settings.h"

#include <uibase/log.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>

// The flow of the import. The steps live in modlistinstaller_downloads.cpp (reuse,
// downloads, checksums), modlistinstaller_install.cpp (the batch) and
// modlistinstaller_profile.cpp (mod order, plugins, INIs, executables).

using namespace MOBase;

namespace modlists
{

ModlistInstaller::BatchMessages::BatchMessages(DownloadManager& m) : manager(m)
{
  manager.setBatchMessages(true);
}

ModlistInstaller::BatchMessages::~BatchMessages()
{
  manager.setBatchMessages(false);
}

ModlistInstaller::ModlistInstaller(OrganizerCore& core, Mo2ListPackage package,
                                   PendingImport pending, QWidget* parent)
    : QObject(parent), m_core(core), m_package(std::move(package)),
      m_pending(std::move(pending)), m_parent(parent)
{}

ModlistInstaller::~ModlistInstaller() = default;

void ModlistInstaller::start()
{
  if (selectProfile() == ProfileState::Gone) {
    fail(tr("The profile \"%1\" cannot be selected").arg(m_pending.profileName));
    return;
  }

  resolveEarlierEntries();
  run(remainingEntries(m_pending, plan()));
}

ModlistInstaller::ProfileState ModlistInstaller::selectProfile()
{
  auto profile = m_core.currentProfile();
  if (m_pending.profileName.isEmpty()) {
    return profile != nullptr ? ProfileState::Current : ProfileState::Gone;
  }
  const auto isDestination = [&] {
    return profile != nullptr &&
           profile->name().compare(m_pending.profileName, Qt::CaseInsensitive) == 0;
  };
  if (isDestination()) {
    return ProfileState::Current;
  }

  // setCurrentProfile() falls back to another profile, with an error box, when the
  // profile does not exist
  const auto profiles = QDir(m_core.settings().paths().profiles())
                            .entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
  const bool exists =
      std::any_of(profiles.begin(), profiles.end(), [&](const QString& name) {
        return name.compare(m_pending.profileName, Qt::CaseInsensitive) == 0;
      });
  if (!exists) {
    log::error("modlist import: the profile {} does not exist", m_pending.profileName);
    return ProfileState::Gone;
  }

  try {
    // the lists of the profile the user switched to
    m_core.saveCurrentLists();
    m_core.setCurrentProfile(m_pending.profileName);
  } catch (const std::exception& e) {
    log::error("cannot select the profile {}: {}", m_pending.profileName, e.what());
  }
  profile = m_core.currentProfile();
  if (!isDestination()) {
    return ProfileState::Gone;
  }

  log::info("modlist import: selected the profile {} again", profile->name());
  emit profileSelected(profile->name());
  return ProfileState::Selected;
}

void ModlistInstaller::withProfile(std::function<void()> f)
{
  switch (selectProfile()) {
  case ProfileState::Current:
    f();
    return;

  case ProfileState::Gone:
    profileGone();
    return;

  case ProfileState::Selected:
    // the mods and plugins of the profile are read again first
    try {
      refreshThen([this, f] {
        withProfile(f);
      });
    } catch (const std::exception& e) {
      log::error("modlist import: refresh failed: {}", e.what());
      f();
    }
    return;
  }
}

void ModlistInstaller::profileGone()
{
  const QString message =
      tr("The profile \"%1\" no longer exists").arg(m_pending.profileName);
  for (int e : remainingEntries(m_pending, plan())) {
    addRow(e, Outcome::Failed, message);
  }
  m_summary.rows.push_back(failedRow(tr("Profile"), message));

  // the record stays, the import can be continued once the profile is back
  savePending();
  m_installing = false;
  emit finished(m_summary);
}

void ModlistInstaller::retry(std::vector<int> entries)
{
  const int count = static_cast<int>(plan().mods.size());
  std::vector<int> valid;
  for (int e : entries) {
    const bool selected =
        std::find(m_pending.selectedEntries.begin(), m_pending.selectedEntries.end(),
                  e) != m_pending.selectedEntries.end();
    const bool installed =
        std::find(m_pending.installedEntries.begin(), m_pending.installedEntries.end(),
                  e) != m_pending.installedEntries.end();
    if (e >= 0 && e < count && selected && !installed &&
        std::find(valid.begin(), valid.end(), e) == valid.end()) {
      valid.push_back(e);
    }
  }
  run(valid);
}

void ModlistInstaller::run(std::vector<int> entries)
{
  if (m_installing) {
    log::error("modlist import: a run is already in progress");
    return;
  }
  m_installing = true;

  // by phase, then by priority (the place in the plan)
  const auto& mods = plan().mods;
  std::stable_sort(entries.begin(), entries.end(), [&](int a, int b) {
    return std::make_pair(mods[a].phase, a) < std::make_pair(mods[b].phase, b);
  });

  m_work = std::move(entries);
  m_entries.clear();
  m_downloads.clear();
  m_downloadQueue.clear();
  m_activeIds.clear();
  m_guided        = false;
  m_guidedDone    = false;
  m_downloadsOver = false;
  m_archiveMd5.clear();

  classifyEntries(m_work);
  findDownloads();
  requestDownloads();
}

void ModlistInstaller::maybeAfterDownloads()
{
  if (m_lookingForDownloads || m_downloadsOver || waitingForDownloads() || (m_guided && !m_guidedDone)) {
    return;
  }
  m_downloadsOver = true;

  // not inside a DownloadManager callback or the slot of needsGuidedDownloads: the
  // installation changes the downloads and refreshes everything
  QTimer::singleShot(0, this, [this] {
    afterDownloads();
  });
}

void ModlistInstaller::afterDownloads()
{
  m_connections.clear();
  m_batchMessages.reset();

  // the checksums before the batch, which runs no event loop
  QPointer<ModlistInstaller> self(this);
  hashArchives();
  if (!self) {
    return;
  }

  // the main window stayed usable during the downloads: the mods go into the profile
  // the user chose in the review, whatever profile is current now
  withProfile([this] {
    guarded(tr("Installation"), [this] {
      installEntries();
    });

    // createMod() (separators, bundled mods) adds the mod to the collection but not
    // to the indices by name nor to the mod status of the profile; only a refresh
    // does, so the order waits for one after the batch
    whenIdle([this] {
      try {
        refreshThen([this] {
          withProfile([this] {
            guarded(tr("Mod order"), [this] {
              applyModOrder();
            });
            try {
              refreshThen([this] {
                withProfile([this] {
                  afterModOrder();
                });
              });
            } catch (const std::exception& e) {
              log::error("modlist import: refresh failed: {}", e.what());
              withProfile([this] {
                afterModOrder();
              });
            }
          });
        });
      } catch (const std::exception& e) {
        log::error("modlist import: refresh failed: {}", e.what());
        withProfile([this] {
          afterModOrder();
        });
      }
    });
  });
}

void ModlistInstaller::afterModOrder()
{
  guarded(tr("Plugins"), [this] {
    applyPlugins();
  });
  if (!m_profileApplied) {
    guarded(tr("INI files"), [this] {
      applyInis();
    });
    guarded(tr("Executables"), [this] {
      applyExecutables();
    });
    m_profileApplied = true;
  }
  finish();
}

void ModlistInstaller::guarded(const QString& what, const std::function<void()>& f)
{
  // nothing escapes into the event loop; the summary says what went wrong
  try {
    f();
  } catch (const std::exception& e) {
    log::error("modlist import: {} failed: {}", what, e.what());
    m_summary.rows.push_back(failedRow(what, QString::fromUtf8(e.what())));
  } catch (...) {
    log::error("modlist import: {} failed", what);
    m_summary.rows.push_back(failedRow(what, tr("Unknown error")));
  }
}

void ModlistInstaller::finish()
{
  const QString base = m_core.settings().paths().base();

  if (remainingEntries(m_pending, plan()).empty()) {
    if (QFile::exists(pendingPath()) && !QFile::remove(pendingPath())) {
      log::error("cannot remove {}", pendingPath());
    }

    // the copy made for the import, never the user's own file
    const QFileInfo copy(QDir(base).filePath("modlist-import.mo2list"));
    const QFileInfo package(m_pending.packagePath);
    if (copy.exists() && package.exists() &&
        copy.canonicalFilePath() == package.canonicalFilePath() &&
        !QFile::remove(copy.absoluteFilePath())) {
      log::error("cannot remove {}", copy.absoluteFilePath());
    }
  } else {
    savePending();
  }

  m_installing = false;
  emit finished(m_summary);
}

void ModlistInstaller::fail(const QString& message)
{
  for (int e : remainingEntries(m_pending, plan())) {
    addRow(e, Outcome::Failed, message);
  }
  m_installing = false;
  emit finished(m_summary);
}

QString ModlistInstaller::pendingPath() const
{
  return QDir(m_core.settings().paths().base()).filePath("modlist-import.json");
}

void ModlistInstaller::savePending() const
{
  QSaveFile file(pendingPath());
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(QJsonDocument(toJson(m_pending)).toJson()) < 0 || !file.commit()) {
    log::error("cannot write {}: {}", pendingPath(), file.errorString());
  }
}

QString ModlistInstaller::rowName(int entry)
{
  const QString name = plan().mods[entry].name;
  auto it            = m_entryOfMod.find(name);
  if (it == m_entryOfMod.end() || it->second == entry) {
    m_entryOfMod[name] = entry;
    return name;
  }

  // two entries with the same name: the second one is told apart by its place
  const QString other = QString("%1 (#%2)").arg(name).arg(entry + 1);
  m_entryOfMod[other] = entry;
  return other;
}

void ModlistInstaller::addRow(int entry, Outcome outcome, const QString& detail)
{
  SummaryRow row = outcome == Outcome::Failed ? failedRow(rowName(entry), detail)
                                              : SummaryRow{rowName(entry), outcome,
                                                           detail, false};

  auto it = m_rowOfEntry.find(entry);
  if (it != m_rowOfEntry.end()) {
    m_summary.rows[it->second] = row;
  } else {
    m_rowOfEntry[entry] = m_summary.rows.size();
    m_summary.rows.push_back(row);
  }

  if (outcome == Outcome::Failed) {
    log::warn("modlist import: {} failed: {}", row.mod, detail);
  }
}

void ModlistInstaller::markInstalled(int entry, const QString& modName)
{
  m_modOfEntry[entry] = modName;
  auto& installed     = m_pending.installedEntries;
  if (std::find(installed.begin(), installed.end(), entry) == installed.end()) {
    installed.push_back(entry);
  }
  savePending();
}

std::vector<QString> ModlistInstaller::archivesOf(int entryIndex) const
{
  std::vector<QString> out;
  auto it = m_entries.find(entryIndex);
  if (it != m_entries.end()) {
    for (const auto& s : it->second.sources) {
      out.push_back(s.archive);
    }
  }
  return out;
}

}  // namespace modlists
