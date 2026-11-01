#ifndef MODLISTS_EXPORTDIALOG_H
#define MODLISTS_EXPORTDIALOG_H

#include "exportbuilder.h"
#include "exportcollector.h"

#include <QDialog>

#include <map>
#include <memory>
#include <vector>

class OrganizerCore;
class Profile;
class QTreeWidgetItem;

namespace Ui
{
class ExportDialog;
}

namespace modlists
{

// Exports a profile as a .mo2list: the header, what to do with the mods without Nexus
// files, the overwrite files to include, the executables and the warnings, then the
// leak check and the file.
class ExportDialog : public QDialog
{
  Q_OBJECT

public:
  // reads the profile on the GUI thread (its local settings may ask something)
  ExportDialog(OrganizerCore& core, const Profile& profile, QWidget* parent = nullptr);
  ~ExportDialog() override;

  // ignored while the checksums are computed or the file is written
  void reject() override;

private:
  std::unique_ptr<Ui::ExportDialog> ui;
  ExportCollector m_collector;

  // read once
  std::vector<ExportMod> m_mods;
  std::vector<PluginEntry> m_plugins;
  std::map<QString, QByteArray> m_inis;
  std::vector<ExecutableEntry> m_executables;
  ProfileConfig m_config;
  PathMarkers m_markers;
  Header m_base;
  QString m_overwriteDir;

  bool m_refreshQueued = false;
  bool m_busy          = false;

  ExportOptions options() const;

  // the table of the mods without Nexus files that the export uses now
  void fillNonNexusTable();
  void fillOverwriteTree();
  std::vector<QString> checkedOverwriteFiles() const;

  // rebuilds the warnings, the executables and the summary; the archives that
  // fillMd5() will hash count as having their MD5
  void refreshPreview();
  void queueRefresh();

  void showWarnings(const std::vector<ExportWarning>& warnings);
  void setBusy(bool busy);
  void save();
  // fillMd5() off the GUI thread with a cancellable progress dialog; false if
  // cancelled
  bool computeMd5(const ExportOptions& options);
  bool confirmLeaks(const PackageInput& package);
  bool writePackage(const QString& path, const PackageInput& package);
};

}  // namespace modlists

#endif  // MODLISTS_EXPORTDIALOG_H
