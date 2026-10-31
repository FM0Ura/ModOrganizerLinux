#ifndef MODLISTS_IMPORTFLOW_H
#define MODLISTS_IMPORTFLOW_H

#include "guidedmatch.h"
#include "importsummary.h"
#include "mo2listzip.h"
#include "pendingimport.h"

#include <QObject>
#include <QPointer>
#include <QWidget>

#include <boost/signals2.hpp>

#include <memory>
#include <optional>

class OrganizerCore;
class QProgressDialog;
namespace MOBase
{
class IPluginGame;
}

namespace modlists
{

class GuidedDownloadsDialog;
class ModlistInstaller;
class SummaryDialog;

// The import of a .mo2list from the user's side: open and check the file, the
// destination, the review, then a new instance (created, the import recorded in it
// and Mod Organizer restarted into it) or the installation in this one, with the
// guided downloads and the summary. Also resumes an import recorded in the current
// instance (<instance>/modlist-import.json). Nothing here runs a program of the list.
class ImportFlow : public QObject
{
  Q_OBJECT

public:
  ImportFlow(OrganizerCore& core, QWidget* parent);
  ~ImportFlow() override;

  void startFromFile(const QString& path);

  // asks to continue the import recorded in the current instance, if there is one
  void resumePending();

  // an import is running or its summary is open
  bool busy() const;

signals:
  // the installer selected this profile: the main window shows it
  void profileSelected(const QString& profileName);
  // the import added executables
  void executablesChanged();
  void sortWithLootRequested();

private:
  OrganizerCore& m_core;
  QPointer<QWidget> m_parent;
  QPointer<ModlistInstaller> m_installer;
  std::unique_ptr<GuidedQueue> m_queue;
  QPointer<GuidedDownloadsDialog> m_guided;
  QPointer<SummaryDialog> m_summary;
  QPointer<QProgressDialog> m_progress;
  boost::signals2::scoped_connection m_nxmConnection;
  // the summary on screen, for "Retry failed"
  Summary m_lastSummary;

  QString pendingPath() const;
  QString packageCopyPath() const;
  // removes the record and the copy of the package (never the user's file)
  void discardPending();

  // creates the instance, records the import in it and restarts into it
  void startInNewInstance(const QString& name, MOBase::IPluginGame* game,
                          const QString& packagePath, PendingImport pending);
  // creates the profile with local INIs and saves; empty on a failure
  QString createProfile(const QString& name);

  void run(Mo2ListPackage package, PendingImport pending);
  void showProgress();
  void showGuided(std::vector<GuidedItem> items);
  void onFinished(const Summary& summary);
  void installManually(const QString& mod);
  void onSummaryClosed();
};

}  // namespace modlists

#endif  // MODLISTS_IMPORTFLOW_H
