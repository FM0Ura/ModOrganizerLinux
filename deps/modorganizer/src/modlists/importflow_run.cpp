#include "importflow.h"

#include "guideddownloadsdialog.h"
#include "modlistinstaller.h"
#include "summarydialog.h"

#include "downloadmanager.h"
#include "organizercore.h"
#include "profile.h"

#include <uibase/log.h>

#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>

#include <algorithm>

// The import once the user chose: the installer with its progress window, the guided
// downloads, the summary with "Retry failed" and "Install manually". The checks and
// the choices before it live in importflow.cpp.

using namespace MOBase;

namespace modlists
{

namespace
{

  // a progress window that stays open: its button and the close button only emit
  // canceled(), which gives up on the downloads
  class ImportProgress : public QProgressDialog
  {
  public:
    using QProgressDialog::QProgressDialog;

    void reject() override { emit canceled(); }

  protected:
    void closeEvent(QCloseEvent* event) override
    {
      event->ignore();
      emit canceled();
    }
  };

}  // namespace

void ImportFlow::run(Mo2ListPackage package, PendingImport pending)
{
  // the lists of the profile the user leaves
  m_core.saveCurrentLists();

  auto* installer =
      new ModlistInstaller(m_core, std::move(package), std::move(pending), m_parent);
  m_installer = installer;

  connect(installer, &ModlistInstaller::progress, this,
          [this](int done, int total, QString what) {
            if (m_progress) {
              m_progress->setMaximum(std::max(total, 1));
              m_progress->setValue(std::min(done, std::max(total, 1)));
              m_progress->setLabelText(
                  tr("%1 (%2 of %3)").arg(what).arg(done).arg(total));
            }
          });
  connect(installer, &ModlistInstaller::needsGuidedDownloads, this,
          [this](std::vector<GuidedItem> items) {
            showGuided(std::move(items));
          });
  connect(installer, &ModlistInstaller::finished, this, [this](Summary summary) {
    onFinished(summary);
  });

  showProgress();
  installer->start();

  // start() selected the destination profile
  if (m_installer && m_core.currentProfile()) {
    emit profileSelected(m_core.currentProfile()->name());
  }
}

void ImportFlow::showProgress()
{
  if (!m_progress) {
    auto* progress = new ImportProgress(tr("Importing the modlist…"),
                                        tr("Stop waiting for downloads"), 0, 0, m_parent);
    progress->setWindowTitle(tr("Import modlist"));
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setMinimumDuration(0);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    connect(progress, &QProgressDialog::canceled, this, [this] {
      stopWaiting();
    });
    m_progress = progress;
  }
  m_progress->show();
}

void ImportFlow::stopWaiting()
{
  if (!m_installer || m_nested > 0) {
    return;
  }

  // the question runs a nested event loop in which the import can finish
  // (onFinished() -> closeProgress()): parented to the main window, and the progress
  // window is only deleted once the question returned
  QPointer<ImportFlow> self(this);
  QMessageBox::StandardButton r;
  ++m_nested;
  r = QMessageBox::question(
      m_parent, tr("Stop waiting"),
      tr("Give up on the downloads of this import that are not over (paused, "
         "removed or still running)? The rest of the list is installed."),
      QMessageBox::Yes | QMessageBox::No);
  if (!self) {
    return;
  }
  --m_nested;

  // not once the import is over (onFinished() ran during the question)
  if (r == QMessageBox::Yes && m_installer && !m_closeProgressPending) {
    // the same way as the guided window's "Stop waiting / Cancel": the guided items
    // first, then the downloads still running
    if (!(m_guided && m_guided->stopFromOutside())) {
      m_installer->stopWaitingForDownloads();
    }
  }
  if (m_closeProgressPending) {
    closeProgress();
  }
}

void ImportFlow::closeProgress()
{
  if (m_nested > 0) {
    m_closeProgressPending = true;
    return;
  }
  m_closeProgressPending = false;
  if (m_progress) {
    m_progress->deleteLater();
    m_progress = nullptr;
  }
}

void ImportFlow::showGuided(std::vector<GuidedItem> items)
{
  if (!m_installer) {
    return;
  }
  // a window of an earlier run is over; it keeps its queue until it is destroyed
  if (m_guided) {
    m_guided->dismiss();
  }
  m_queue = std::make_shared<GuidedQueue>(std::move(items));

  auto* dialog = new GuidedDownloadsDialog(*m_queue, m_installer->plan(), m_parent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &QObject::destroyed, this, [queue = m_queue] {
    (void)queue;
  });
  m_guided = dialog;

  connect(dialog, &GuidedDownloadsDialog::manualFileChosen, this,
          [this](int entry, int source, const QString& file) {
            if (m_installer) {
              m_installer->setManualFile(entry, source, file);
            }
          });
  connect(dialog, &GuidedDownloadsDialog::guidedFinished, this,
          [this, queue = m_queue.get()] {
            m_nxmConnection.disconnect();
            if (m_installer && m_queue.get() == queue) {
              showProgress();
              m_installer->guidedDownloadsFinished(m_queue->items());
            }
          });
  connect(dialog, &GuidedDownloadsDialog::stopWaiting, this, [this] {
    if (m_installer) {
      m_installer->stopWaitingForDownloads();
    }
  });

  // the nxm:// links the user clicks; the installer is connected before this, so it
  // knows the download when the window moves to the next file
  m_nxmConnection = m_core.downloadManager()->onNXMDownloadQueued(
      [this](const QString&, int modId, int fileId, int) {
        if (m_guided) {
          m_guided->nxmQueued(modId, fileId);
        }
      });

  if (m_progress) {
    m_progress->hide();
  }
  dialog->show();
  dialog->raise();
  dialog->start();
}

void ImportFlow::onFinished(const Summary& summary)
{
  m_nxmConnection.disconnect();
  // both close later if a nested loop of theirs is open
  if (m_guided) {
    m_guided->dismiss();
  }
  closeProgress();
  emit executablesChanged();

  if (!m_installer) {
    return;
  }

  if (!m_summary) {
    auto* dialog = new SummaryDialog(summary, m_parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_summary = dialog;

    connect(dialog, &SummaryDialog::retryRequested, this, [this] {
      if (!m_installer || !m_summary) {
        return;
      }
      m_summary->setBusy(true);
      showProgress();
      m_installer->retry(retryable(m_lastSummary, m_installer->entryOfMod()));
    });
    connect(dialog, &SummaryDialog::installManuallyRequested, this,
            [this](const QString& mod) {
              installManually(mod);
            });
    connect(dialog, &SummaryDialog::sortWithLootRequested, this, [this] {
      emit sortWithLootRequested();
    });
    connect(dialog, &QDialog::finished, this, [this] {
      onSummaryClosed();
    });
  } else {
    m_summary->setSummary(summary);
  }

  m_lastSummary = summary;
  m_summary->setCanRetry(!retryable(summary, m_installer->entryOfMod()).empty());
  m_summary->setBusy(false);
  m_summary->show();
  m_summary->raise();
}

void ImportFlow::installManually(const QString& mod)
{
  if (!m_installer || !m_summary) {
    return;
  }
  const auto& entries = m_installer->entryOfMod();
  const auto it       = entries.find(mod);

  QStringList archives;
  if (it != entries.end()) {
    for (const auto& archive : m_installer->archivesOf(it->second)) {
      if (!archive.isEmpty() && QFileInfo::exists(archive)) {
        archives << archive;
      }
    }
  }
  if (archives.isEmpty()) {
    QMessageBox::information(m_summary, tr("Install manually"),
                             tr("The archive of %1 is not available. Download it and "
                                "install it with \"Install Mod\".")
                                 .arg(mod));
    return;
  }

  // the usual installation, with its dialogs (the batch is over)
  m_summary->setBusy(true);
  for (const auto& archive : archives) {
    try {
      m_core.installArchive(archive);
    } catch (const std::exception& e) {
      log::error("cannot install {}: {}", archive, e.what());
      QMessageBox::warning(
          m_summary, tr("Install manually"),
          tr("%1 cannot be installed: %2").arg(archive, QString::fromUtf8(e.what())));
    }
  }
  if (m_summary) {
    m_summary->setBusy(false);
  }
}

void ImportFlow::onSummaryClosed()
{
  // finish() removed the record unless something is left to install
  if (QFile::exists(pendingPath())) {
    QMessageBox box(QMessageBox::Question, tr("Import modlist"),
                    tr("Some mods of the list are not installed. Keep the import to "
                       "continue it the next time Mod Organizer starts?"),
                    QMessageBox::NoButton, m_parent);
    auto* keep = box.addButton(tr("Keep"), QMessageBox::AcceptRole);
    box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.setDefaultButton(keep);
    box.exec();
    if (box.clickedButton() != keep) {
      discardPending();
    }
  }

  if (m_installer) {
    m_installer->deleteLater();
  }
  m_installer = nullptr;
  m_summary   = nullptr;
}

}  // namespace modlists
