#ifndef MODLISTS_GUIDEDDOWNLOADSDIALOG_H
#define MODLISTS_GUIDEDDOWNLOADSDIALOG_H

#include "guidedmatch.h"
#include "plan.h"

#include <QDialog>

#include <memory>

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
