#ifndef MODLISTS_GUIDEDDOWNLOADSDIALOG_H
#define MODLISTS_GUIDEDDOWNLOADSDIALOG_H

#include "guidedmatch.h"
#include "plan.h"

#include <QDialog>

#include <memory>
#include <optional>

namespace Ui
{
class GuidedDownloadsDialog;
}

namespace modlists
{

// The guided downloads of a user without Premium (§5.1): one file at a time, the page
// of the current file opens in the browser and the nxm:// that comes back is matched
// against the queue. Manual sources get "Open page" and "Choose downloaded file…"
// (checked against the size and MD5 of the plan). Not modal: the nxm:// links arrive
// while it is open.
class GuidedDownloadsDialog : public QDialog
{
  Q_OBJECT

public:
  GuidedDownloadsDialog(GuidedQueue& queue, const Plan& plan, QWidget* parent = nullptr);
  ~GuidedDownloadsDialog() override;

  // an nxm:// download was queued by the download manager
  void nxmQueued(int modId, int fileId);

  // opens the page of the current file, as when the window opens
  void start();

  // asks before leaving files out, then emits guidedFinished()
  void reject() override;

  // ends the window as "Stop waiting / Cancel" does, without asking: emits
  // guidedFinished() then stopWaiting(); false if the window was already over (then
  // nothing is emitted). Deferred while a nested window of this one is open.
  bool stopFromOutside();

  // closes the window without emitting anything (the import is over); deferred while
  // a nested window of this one is open
  void dismiss();

signals:
  void manualFileChosen(int entryIndex, int sourceIndex, const QString& path);
  // the window is over: the queue says what is queued, skipped or left pending
  void guidedFinished();
  // the user gave up on the downloads that are still running (after guidedFinished())
  void stopWaiting();

private:
  std::unique_ptr<Ui::GuidedDownloadsDialog> ui;
  GuidedQueue& m_queue;
  const Plan& m_plan;
  bool m_over = false;

  // Nested event loops (question boxes, the file dialog, the MD5 progress): an
  // nxm:// arriving in them can end the window (advanced() -> finishGuided()); the
  // window must not be closed (WA_DeleteOnClose deletes it) under its own nested
  // loop, so the end is remembered and done when the last nested loop returns.
  int m_nested         = 0;
  bool m_finishPending = false;
  bool m_finishStop    = false;
  bool m_dismissPending = false;

  class NestedLoop;
  // QMessageBox::question() under the guard; nullopt when the window is gone
  std::optional<bool> ask(const QString& title, const QString& text);
  // runs the end remembered while a nested loop was open
  void runDeferred();

  const Source& sourceOf(const GuidedItem& item) const;
  QString describe(const GuidedItem& item) const;
  // opens the Nexus page of the current item, if it is a Nexus file
  void openCurrent();
  void openManualPage();
  void chooseFile();
  // true if the file has the size and the MD5 of the source (asks nothing)
  bool checkFile(const Source& source, const QString& path, QString& why);
  void refresh();
  // the current item changed: open the next page
  void advanced();
  // ends the window; `stop` gives up on the running downloads too
  void finishGuided(bool stop);
};

}  // namespace modlists

#endif  // MODLISTS_GUIDEDDOWNLOADSDIALOG_H
