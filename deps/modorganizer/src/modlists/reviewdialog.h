#ifndef MODLISTS_REVIEWDIALOG_H
#define MODLISTS_REVIEWDIALOG_H

#include "importreview.h"

#include <QDialog>

#include <map>
#include <memory>
#include <set>
#include <vector>

class QComboBox;
class QPushButton;
class QTreeWidgetItem;

namespace Ui
{
class ReviewDialog;
}

namespace modlists
{

// The review of a list before the import (6b §4, spec §5.6): one row per mod with
// its status, the selection (optional mods unchecked), the version choice of the mods
// the user has in another version, the executable content that must be confirmed
// (§6.3) and the downloads the user will click.
class ReviewDialog : public QDialog
{
  Q_OBJECT

public:
  // reusable: the sources whose archive is already downloaded (not counted in the
  // clicks); findings: findExecutableContent() of the package
  ReviewDialog(const Plan& plan, const std::vector<InstalledMod>& installed,
               Destination destination, bool premium,
               const std::set<SourceRef>& reusable,
               const std::vector<ExecFinding>& findings, QWidget* parent = nullptr);
  ~ReviewDialog() override;

  // indices into Plan::mods of the checked rows, in the order of the plan
  std::vector<int> selectedEntries() const;
  // the choice of every row the user has in another version
  std::map<int, VersionChoice> versionChoices() const;
  // the executable content was confirmed, or there is none
  bool confirmedExecutableContent() const;

private:
  std::unique_ptr<Ui::ReviewDialog> ui;
  const Plan& m_plan;
  Destination m_destination;
  bool m_premium;
  std::set<SourceRef> m_reusable;
  std::vector<RowStatus> m_statuses;
  bool m_hasExecutableContent = false;
  QPushButton* m_install      = nullptr;
  // one per entry, in the order of the plan
  std::vector<QTreeWidgetItem*> m_rows;
  // the version choice of the DifferentVersion rows
  std::map<int, QComboBox*> m_choices;

  void fillHeader();
  void fillRows(const std::vector<InstalledMod>& installed);
  void applyFilter();
  void setAllChecked(bool optionalOnly, bool checked);
  // footer and the "Download and install" button
  void updateFooter();
};

}  // namespace modlists

#endif  // MODLISTS_REVIEWDIALOG_H
