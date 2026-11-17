#ifndef MODLISTS_SUMMARYDIALOG_H
#define MODLISTS_SUMMARYDIALOG_H

#include "importsummary.h"

#include <QDialog>

#include <memory>

namespace Ui
{
class SummaryDialog;
}

namespace modlists
{

// The end of an import: the result of each mod, with "Install manually" on the rows
// that need the user's choices, then the plugins, masters, INIs and executables to
// look at. "Copy" puts summaryText() on the clipboard.
class SummaryDialog : public QDialog
{
  Q_OBJECT

public:
  explicit SummaryDialog(const Summary& summary, QWidget* parent = nullptr);
  ~SummaryDialog() override;

  // after a retry
  void setSummary(const Summary& summary);
  // "Retry failed" has entries to retry
  void setCanRetry(bool canRetry);
  // disables the buttons while a retry or a manual installation runs
  void setBusy(bool busy);

  // ignored while busy
  void reject() override;

signals:
  void retryRequested();
  void installManuallyRequested(const QString& mod);
  void sortWithLootRequested();

private:
  std::unique_ptr<Ui::SummaryDialog> ui;
  Summary m_summary;
  bool m_canRetry = false;
  bool m_busy     = false;

  void updateButtons();
};

}  // namespace modlists

#endif  // MODLISTS_SUMMARYDIALOG_H
