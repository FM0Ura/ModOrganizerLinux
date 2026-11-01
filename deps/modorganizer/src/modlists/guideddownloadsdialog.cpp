#include "guideddownloadsdialog.h"
#include "ui_guideddownloadsdialog.h"

#include "importplan.h"

#include <uibase/log.h>
#include <uibase/utility.h>

#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QUrl>

#include <algorithm>

using namespace MOBase;

namespace modlists
{

namespace
{

  // the index of the item in the queue, on its list row
  constexpr int ItemRole = Qt::UserRole;

  bool isNexus(const GuidedItem& item) { return item.modId > 0 && item.fileId > 0; }

}  // namespace

// counts a nested event loop of the window for its lifetime
class GuidedDownloadsDialog::NestedLoop
{
public:
  explicit NestedLoop(GuidedDownloadsDialog& d) : m_dialog(&d) { ++d.m_nested; }
  ~NestedLoop()
  {
    if (m_dialog) {
      --m_dialog->m_nested;
    }
  }
  NestedLoop(const NestedLoop&)            = delete;
  NestedLoop& operator=(const NestedLoop&) = delete;

private:
  QPointer<GuidedDownloadsDialog> m_dialog;
};

GuidedDownloadsDialog::GuidedDownloadsDialog(GuidedQueue& queue, const Plan& plan,
                                             QWidget* parent)
    : QDialog(parent), ui(new Ui::GuidedDownloadsDialog), m_queue(queue), m_plan(plan)
{
  ui->setupUi(this);

  connect(ui->openAgain, &QPushButton::clicked, this, [this] {
    openCurrent();
  });
  connect(ui->openPage, &QPushButton::clicked, this, [this] {
    openManualPage();
  });
  connect(ui->chooseFile, &QPushButton::clicked, this, [this] {
    chooseFile();
  });
  connect(ui->skip, &QPushButton::clicked, this, [this] {
    m_queue.skip();
    ui->messageLabel->clear();
    advanced();
  });
  connect(ui->unskip, &QPushButton::clicked, this, [this] {
    const auto* row = ui->items->currentItem();
    if (row == nullptr) {
      return;
    }
    const auto before = m_queue.current();
    m_queue.unskip(row->data(ItemRole).toULongLong());
    if (m_queue.current() != before) {
      advanced();
    } else {
      refresh();
    }
  });
  connect(ui->items, &QListWidget::currentRowChanged, this, [this] {
    refresh();
  });
  connect(ui->continueButton, &QPushButton::clicked, this, [this] {
    reject();
  });
  connect(ui->stopWaiting, &QPushButton::clicked, this, [this] {
    const auto yes =
        ask(tr("Stop waiting"),
            tr("Give up on the files that are not downloaded yet? The downloads of "
               "this import that are still running are left out too; the rest of "
               "the list is installed."));
    if (!yes) {
      return;  // the window is gone
    }
    if (*yes) {
      finishGuided(true);
    }
    runDeferred();
  });

  refresh();
}

GuidedDownloadsDialog::~GuidedDownloadsDialog() = default;

std::optional<bool> GuidedDownloadsDialog::ask(const QString& title,
                                               const QString& text)
{
  QPointer<GuidedDownloadsDialog> self(this);
  QMessageBox::StandardButton r;
  {
    NestedLoop nested(*this);
    r = QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No);
  }
  if (!self) {
    return std::nullopt;
  }
  return r == QMessageBox::Yes;
}

void GuidedDownloadsDialog::runDeferred()
{
  if (m_nested > 0) {
    return;
  }
  if (m_finishPending) {
    m_finishPending = false;
    finishGuided(m_finishStop);
  }
  if (m_dismissPending) {
    m_dismissPending = false;
    dismiss();
  }
}

bool GuidedDownloadsDialog::stopFromOutside()
{
  if (m_over) {
    return false;
  }
  finishGuided(true);
  return true;
}

void GuidedDownloadsDialog::dismiss()
{
  if (m_nested > 0) {
    m_dismissPending = true;
    return;
  }
  m_over = true;
  QDialog::reject();
}

void GuidedDownloadsDialog::start()
{
  openCurrent();
}

const Source& GuidedDownloadsDialog::sourceOf(const GuidedItem& item) const
{
  return m_plan.mods.at(item.entryIndex).sources.at(item.sourceIndex);
}

QString GuidedDownloadsDialog::describe(const GuidedItem& item) const
{
  const ModEntry& entry = m_plan.mods.at(item.entryIndex);
  const Source& source  = sourceOf(item);
  QString text          = entry.name;
  if (entry.sources.size() > 1 || !source.fileName.isEmpty()) {
    text += source.fileName.isEmpty()
                ? QString(" (%1/%2)").arg(item.sourceIndex + 1).arg(entry.sources.size())
                : QString(" (%1)").arg(source.fileName);
  }
  return text;
}

void GuidedDownloadsDialog::openCurrent()
{
  const auto cur = m_queue.current();
  if (!cur || !isNexus(m_queue.items()[*cur])) {
    return;
  }
  const auto& item = m_queue.items()[*cur];
  QString domain   = m_plan.header.nexusDomain;
  shell::Open(nexusFilePageUrl(domain, item.modId, item.fileId));
}

void GuidedDownloadsDialog::openManualPage()
{
  const auto cur = m_queue.current();
  if (!cur) {
    return;
  }
  // the link comes from the list: only a web page is opened
  const QUrl url(sourceOf(m_queue.items()[*cur]).url, QUrl::StrictMode);
  if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http")) {
    ui->messageLabel->setText(tr("The list has no web page for this file."));
    return;
  }
  shell::Open(url);
}

bool GuidedDownloadsDialog::checkFile(const Source& source, const QString& path,
                                      QString& why)
{
  const QFileInfo info(path);
  if (source.size >= 0 && info.size() != source.size) {
    why = tr("The file has %1 bytes, the list expects %2.")
              .arg(info.size())
              .arg(source.size);
    return false;
  }
  if (source.md5.isEmpty()) {
    return true;
  }

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    why = tr("The file cannot be read: %1").arg(file.errorString());
    return false;
  }

  // by blocks, so a large file does not freeze the window
  QProgressDialog progress(tr("Checking %1…").arg(info.fileName()), tr("Cancel"), 0,
                           1000, this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(500);
  QCryptographicHash hash(QCryptographicHash::Md5);
  const qint64 size = std::max<qint64>(info.size(), 1);
  qint64 read       = 0;
  while (!file.atEnd()) {
    const QByteArray block = file.read(qint64(4) << 20);
    if (block.isEmpty() && file.error() != QFileDevice::NoError) {
      why = tr("The file cannot be read: %1").arg(file.errorString());
      return false;
    }
    hash.addData(block);
    read += block.size();
    progress.setValue(static_cast<int>(read * 1000 / size));
    if (progress.wasCanceled()) {
      why = tr("The check was cancelled.");
      return false;
    }
  }

  if (QString::fromLatin1(hash.result().toHex())
          .compare(source.md5, Qt::CaseInsensitive) != 0) {
    why = tr("The file is not the one of the list (the MD5 does not match).");
    return false;
  }
  return true;
}

void GuidedDownloadsDialog::chooseFile()
{
  const auto cur = m_queue.current();
  if (!cur) {
    return;
  }
  const GuidedItem item = m_queue.items()[*cur];
  const Source& source  = sourceOf(item);

  // the file dialog and the MD5 progress run nested event loops
  QPointer<GuidedDownloadsDialog> self(this);
  QString path, why;
  bool ok = false;
  {
    NestedLoop nested(*this);
    path = QFileDialog::getOpenFileName(
        this, tr("The downloaded file of %1").arg(describe(item)), QString(),
        tr("Archives (*.7z *.zip *.rar *.fomod *.001);;All files (*)"));
    if (self && !path.isEmpty()) {
      ok = checkFile(source, path, why);
    }
  }
  if (!self) {
    return;
  }
  if (path.isEmpty() || !ok) {
    if (!why.isEmpty()) {
      ui->messageLabel->setText(why);
    }
    runDeferred();
    return;
  }

  // the item may have changed (or the window ended) while the MD5 was computed
  if (m_over || m_finishPending || m_dismissPending || m_queue.current() != cur ||
      !m_queue.setQueued(*cur)) {
    runDeferred();
    return;
  }
  ui->messageLabel->setText(tr("%1: file accepted.").arg(describe(item)));
  emit manualFileChosen(item.entryIndex, item.sourceIndex, path);
  advanced();
}

void GuidedDownloadsDialog::nxmQueued(int modId, int fileId)
{
  if (m_over) {
    return;
  }

  switch (m_queue.match(modId, fileId)) {
  case MatchResult::Current:
    ui->messageLabel->clear();
    advanced();
    break;
  case MatchResult::OtherItem:
    ui->messageLabel->setText(tr("A later file of the list was queued."));
    refresh();
    break;
  case MatchResult::AlreadyQueued:
    ui->messageLabel->setText(tr("That file is already queued."));
    break;
  case MatchResult::SkippedItem:
    ui->messageLabel->setText(tr("That file was skipped. Select it in the list and "
                                 "click \"Download it after all\" to count it."));
    break;
  case MatchResult::NotInList:
    // a download of the user, not of the list
    break;
  }
}

void GuidedDownloadsDialog::advanced()
{
  refresh();

  bool skipped = false;
  for (const auto& item : m_queue.items()) {
    skipped = skipped || item.state == GuidedItem::Skipped;
  }

  if (!m_queue.current()) {
    // every file is queued: nothing to ask
    if (!skipped) {
      finishGuided(false);
    }
    return;
  }
  openCurrent();
}

void GuidedDownloadsDialog::refresh()
{
  const auto& items = m_queue.items();
  const auto cur    = m_queue.current();

  std::size_t queued = 0, skipped = 0;
  for (const auto& item : items) {
    queued += item.state == GuidedItem::Queued ? 1 : 0;
    skipped += item.state == GuidedItem::Skipped ? 1 : 0;
  }

  if (cur) {
    ui->progressLabel->setText(tr("%1 of %2 (%3 queued, %4 skipped)")
                                   .arg(queued + skipped + 1)
                                   .arg(m_queue.total())
                                   .arg(queued)
                                   .arg(skipped));
    const bool nexus = isNexus(items[*cur]);
    ui->currentLabel->setText(
        nexus ? tr("Now: %1").arg(describe(items[*cur]))
              : tr("Now: %1. This file is not on Nexus: open its page, download "
                   "it and choose the downloaded file.")
                    .arg(describe(items[*cur])));
    ui->openAgain->setVisible(nexus);
    ui->openPage->setVisible(!nexus);
    ui->chooseFile->setVisible(!nexus);
  } else {
    ui->progressLabel->setText(tr("%1 of %2 queued, %3 skipped")
                                   .arg(queued)
                                   .arg(m_queue.total())
                                   .arg(skipped));
    ui->currentLabel->setText(
        tr("Nothing left to download. Click Continue to install the list once the "
           "downloads are over."));
    ui->openAgain->setVisible(false);
    ui->openPage->setVisible(false);
    ui->chooseFile->setVisible(false);
  }
  ui->skip->setEnabled(cur.has_value());

  // the list: what is left first is what matters, so every item with its state
  const int selected = ui->items->currentRow();
  {
    const QSignalBlocker blocker(ui->items);
    ui->items->clear();
    for (std::size_t i = 0; i < items.size(); ++i) {
      QString state;
      switch (items[i].state) {
      case GuidedItem::Pending:
        state = cur && *cur == i ? tr("now") : tr("to download");
        break;
      case GuidedItem::Queued:
        state = tr("queued");
        break;
      case GuidedItem::Skipped:
        state = tr("skipped");
        break;
      }
      auto* row = new QListWidgetItem(
          QString("%1 — %2").arg(describe(items[i]), state), ui->items);
      row->setData(ItemRole, static_cast<qulonglong>(i));
    }
    ui->items->setCurrentRow(selected);
  }

  const auto* row = ui->items->currentItem();
  ui->unskip->setEnabled(row != nullptr &&
                         items[row->data(ItemRole).toULongLong()].state ==
                             GuidedItem::Skipped);
  ui->continueButton->setDefault(!cur.has_value());
}

void GuidedDownloadsDialog::reject()
{
  if (m_over) {
    QDialog::reject();
    return;
  }

  std::size_t pending = 0;
  for (const auto& item : m_queue.items()) {
    pending += item.state == GuidedItem::Pending ? 1 : 0;
  }
  if (pending > 0) {
    // a second close while the question is open is ignored
    if (m_nested > 0) {
      return;
    }
    const auto yes =
        ask(tr("Files left"),
            tr("%1 files of the list are not downloaded yet. Continue without them?")
                .arg(pending));
    if (!yes) {
      return;  // the window is gone
    }
    if (!*yes) {
      runDeferred();
      return;
    }
  }
  finishGuided(false);
  runDeferred();
}

void GuidedDownloadsDialog::finishGuided(bool stop)
{
  if (m_over) {
    return;
  }
  if (m_nested > 0) {
    // closing now would delete the window under its own nested loop
    m_finishPending = true;
    m_finishStop    = m_finishStop || stop;
    return;
  }
  stop   = stop || m_finishStop;
  m_over = true;
  emit guidedFinished();
  if (stop) {
    emit stopWaiting();
  }
  QDialog::reject();
}

}  // namespace modlists
