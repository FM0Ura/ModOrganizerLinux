#include "importflow.h"

#include "background.h"
#include "destinationdialog.h"
#include "guideddownloadsdialog.h"
#include "importreview.h"
#include "modlistinstaller.h"
#include "reviewdialog.h"
#include "summarydialog.h"

#include "apiuseraccount.h"
#include "createinstancedialog.h"
#include "downloadmanager.h"
#include "instancemanager.h"
#include "modinfo.h"
#include "nexusinterface.h"
#include "organizercore.h"
#include "plugincontainer.h"
#include "profile.h"
#include "settings.h"
#include "shared/appconfig.h"
#include "shared/util.h"

#include <uibase/iplugingame.h>
#include <uibase/log.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>

#include <algorithm>
#include <atomic>
#include <memory>

// The import from the user's side up to the installation: the checks of the file,
// the destination and the review, the new instance and the resume of an import
// recorded in the instance. The installation itself is in importflow_run.cpp.

using namespace MOBase;

namespace modlists
{

namespace
{

  const char* const PendingFile = "modlist-import.json";
  const char* const PackageCopy = "modlist-import.mo2list";

  // the archives of the downloads folder with the Nexus ids of their .meta
  std::vector<DownloadCandidate> downloadCandidates(const QString& dir)
  {
    std::vector<DownloadCandidate> out;
    for (const auto& info : QDir(dir).entryInfoList(QDir::Files)) {
      if (info.suffix().compare("meta", Qt::CaseInsensitive) == 0 ||
          info.suffix().compare("unfinished", Qt::CaseInsensitive) == 0) {
        continue;
      }
      DownloadCandidate c;
      c.path = info.absoluteFilePath();
      c.size = info.size();
      if (QFile::exists(c.path + ".meta")) {
        const QSettings meta(c.path + ".meta", QSettings::IniFormat);
        c.modId  = meta.value("modID", 0).toInt();
        c.fileId = meta.value("fileID", 0).toInt();
      }
      out.push_back(c);
    }
    return out;
  }

  // the mods of the instance a row can match (as ModlistInstaller compares them)
  std::vector<InstalledMod> installedMods()
  {
    std::vector<InstalledMod> out;
    for (unsigned int i = 0; i < ModInfo::getNumMods(); ++i) {
      const auto mod = ModInfo::getByIndex(i);
      if (!mod->isRegular() || mod->isBackup() || mod->isOverwrite() ||
          mod->isForeign() || mod->isSeparator()) {
        continue;
      }
      InstalledMod m;
      m.name  = mod->name();
      m.modId = mod->nexusId();
      for (const auto& [modId, fileId] : mod->installedFiles()) {
        if (modId == m.modId || m.modId <= 0) {
          m.fileIds.push_back(fileId);
        }
      }
      m.version = mod->version().isValid() ? mod->version().canonicalString() : "";
      out.push_back(m);
    }
    return out;
  }

  bool writePending(const QString& path, const PendingImport& pending)
  {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(toJson(pending)).toJson()) < 0 || !file.commit()) {
      log::error("cannot write {}: {}", path, file.errorString());
      return false;
    }
    return true;
  }

  // copies the package unless it is already there
  bool copyPackage(const QString& from, const QString& to)
  {
    const QFileInfo src(from), dst(to);
    if (dst.exists() && src.canonicalFilePath() == dst.canonicalFilePath()) {
      return true;
    }
    if (dst.exists() && !QFile::remove(to)) {
      return false;
    }
    return QFile::copy(from, to);
  }

  IPluginGame* gamePlugin(OrganizerCore& core, const QString& shortName)
  {
    for (auto* game : core.pluginContainer().plugins<IPluginGame>()) {
      if (game->gameShortName().compare(shortName, Qt::CaseInsensitive) == 0) {
        return game;
      }
    }
    return nullptr;
  }

}  // namespace

// holds ImportFlow::m_starting while it lives
class ImportFlow::Starting
{
public:
  explicit Starting(ImportFlow& flow) : m_flow(&flow) { flow.m_starting = true; }
  ~Starting()
  {
    if (m_flow) {
      m_flow->m_starting = false;
    }
  }
  Starting(const Starting&)            = delete;
  Starting& operator=(const Starting&) = delete;

private:
  QPointer<ImportFlow> m_flow;
};

bool ImportFlow::copyPackageInBackground(const QString& from, const QString& to)
{
  // a package with bundled mods can be big: copied on another thread, the window
  // responsive (a nested event loop; the caller checks that it still exists)
  QPointer<QProgressDialog> progress =
      new QProgressDialog(tr("Copying the modlist…"), QString(), 0, 0, m_parent);
  progress->setWindowTitle(tr("Import modlist"));
  progress->setWindowModality(Qt::WindowModal);
  progress->setMinimumDuration(500);
  progress->setCancelButton(nullptr);

  auto ok = std::make_shared<std::atomic<bool>>(false);
  runInBackground(
      [ok, from, to] {
        *ok = copyPackage(from, to);
      },
      [] {});
  delete progress.data();
  return *ok;
}

ImportFlow::ImportFlow(OrganizerCore& core, QWidget* parent)
    : QObject(parent), m_core(core), m_parent(parent)
{}

ImportFlow::~ImportFlow()
{
  // the dialogs refer to the queue and the installer
  delete m_guided.data();
  delete m_summary.data();
  delete m_progress.data();
  delete m_installer.data();
}

bool ImportFlow::busy() const
{
  return m_starting || !m_installer.isNull();
}

QString ImportFlow::pendingPath() const
{
  return QDir(m_core.settings().paths().base()).filePath(PendingFile);
}

QString ImportFlow::packageCopyPath() const
{
  return QDir(m_core.settings().paths().base()).filePath(PackageCopy);
}

void ImportFlow::discardPending(const QString& keep)
{
  if (QFile::exists(pendingPath()) && !QFile::remove(pendingPath())) {
    log::error("cannot remove {}", pendingPath());
  }
  // the copy the import made, under its own name; the user's file is never removed,
  // nor the copy when it is the file being imported now
  const QFileInfo copy(packageCopyPath());
  if (!copy.exists()) {
    return;
  }
  if (!keep.isEmpty() && QFileInfo(keep).canonicalFilePath() == copy.canonicalFilePath()) {
    return;
  }
  if (!QFile::remove(copy.absoluteFilePath())) {
    log::error("cannot remove {}", copy.absoluteFilePath());
  }
}

void ImportFlow::startFromFile(const QString& path)
{
  const QString title = tr("Import modlist");
  if (busy()) {
    QMessageBox::information(m_parent, title,
                             tr("An import is already running. Finish it (close its "
                                "windows or its summary) first."));
    return;
  }
  const Starting starting(*this);

  std::optional<Mo2ListPackage> package;
  try {
    package.emplace(Mo2ListPackage::open(path));
  } catch (const PlanError& e) {
    QMessageBox::warning(m_parent, title,
                         e.kind() == PlanError::Kind::NewerSchema
                             ? tr("This modlist was made by a newer Mod Organizer. "
                                  "Update Mod Organizer to import it.")
                             : tr("This file cannot be imported:\n\n%1")
                                   .arg(QString::fromUtf8(e.what())));
    return;
  } catch (const std::exception& e) {
    QMessageBox::warning(
        m_parent, title,
        tr("This file cannot be opened:\n\n%1").arg(QString::fromUtf8(e.what())));
    return;
  }
  const Plan& plan = package->plan();

  // §6.1: another game only goes into a new instance of that game
  const IPluginGame* managed = m_core.managedGame();
  const bool same            = sameGame(plan.header, managed->gameShortName());
  IPluginGame* game          = gamePlugin(m_core, plan.header.gameName);

  QString differentGame, newInstanceProblem;
  if (!same) {
    differentGame =
        tr("This list is for %1 and this instance manages %2: it can only be "
           "imported into a new instance.")
            .arg(game ? game->gameName() : plan.header.gameName, managed->gameName());
  }
  if (!InstanceManager::singleton().allowedToChangeInstance()) {
    newInstanceProblem = tr("This Mod Organizer only uses its portable instance, so "
                            "no new instance can be made.");
  } else if (game == nullptr) {
    newInstanceProblem =
        tr("Mod Organizer has no support for the game %1.").arg(plan.header.gameName);
  } else if (!same && !game->isInstalled()) {
    newInstanceProblem = tr("%1 was not found on this computer.").arg(game->gameName());
  } else if (!same && game->gameVariants().size() > 1) {
    newInstanceProblem =
        tr("%1 has several editions: create an instance of it first and import the "
           "list from there.")
            .arg(game->gameName());
  }

  const QStringList profiles = QDir(m_core.settings().paths().profiles())
                                   .entryList(QDir::AllDirs | QDir::NoDotAndDotDot);

  DestinationDialog destination(plan.header, profiles, differentGame,
                                newInstanceProblem, m_parent);
  if (destination.exec() != QDialog::Accepted) {
    return;
  }
  const Destination where = destination.destination();

  if (where != Destination::NewInstance && QFile::exists(pendingPath())) {
    const auto r = QMessageBox::question(
        m_parent, title,
        tr("An import that did not finish is recorded in this instance. Discard it "
           "and import this list? The mods it installed stay installed."),
        QMessageBox::Yes | QMessageBox::No);
    if (r != QMessageBox::Yes) {
      return;
    }
    discardPending(path);
  }

  const bool premium = NexusInterface::instance().getAPIUserAccount().type() ==
                       APIUserAccountTypes::Premium;
  const QString downloads = m_core.settings().paths().downloads();
  const auto reusable     = reusableSources(plan, downloadCandidates(downloads));

  ReviewDialog review(plan, installedMods(), where, premium, reusable,
                      findExecutableContent(*package), m_parent);
  if (review.exec() != QDialog::Accepted || !review.confirmedExecutableContent()) {
    return;
  }

  PendingImport pending;
  pending.destination        = where;
  pending.selectedEntries    = review.selectedEntries();
  pending.sourceDownloadsDir = downloads;
  if (where != Destination::NewInstance) {
    pending.versionChoices = review.versionChoices();
  }

  switch (where) {
  case Destination::NewInstance:
    startInNewInstance(destination.instanceName(), game, path, std::move(pending));
    return;

  case Destination::NewProfile:
    pending.profileName = createProfile(destination.profileName());
    if (pending.profileName.isEmpty()) {
      return;
    }
    break;

  case Destination::ExistingProfile:
    pending.profileName = destination.profileName();
    break;
  }

  // recorded with a copy of the package, so the import can be resumed after a
  // restart even if the file is moved
  pending.packagePath = path;
  QPointer<ImportFlow> self(this);
  const bool copied = copyPackageInBackground(path, packageCopyPath());
  if (!self) {
    return;
  }
  if (copied) {
    pending.packagePath = packageCopyPath();
  } else {
    log::warn("cannot copy {} to {}, the import refers to the original", path,
              packageCopyPath());
  }
  writePending(pendingPath(), pending);

  run(std::move(*package), std::move(pending));
}

QString ImportFlow::createProfile(const QString& name)
{
  try {
    // §5.0: a profile made by the import uses local INIs and local saves
    Profile profile(name, m_core.managedGame(), m_core.gameFeatures(), false);
    profile.enableLocalSettings(true);
    profile.enableLocalSaves(true);
    m_core.profileCreated(&profile);
    return profile.name();
  } catch (const std::exception& e) {
    QMessageBox::warning(
        m_parent, tr("Import modlist"),
        tr("The profile cannot be created: %1").arg(QString::fromUtf8(e.what())));
    return {};
  }
}

void ImportFlow::startInNewInstance(const QString& name, IPluginGame* game,
                                    const QString& packagePath, PendingImport pending)
{
  const QString title = tr("Import modlist");
  if (game == nullptr) {
    return;
  }

  const bool same = game->gameShortName().compare(
                        m_core.managedGame()->gameShortName(), Qt::CaseInsensitive) == 0;
  const auto defaultPath = [](const std::wstring& dir) {
    return QDir::toNativeSeparators(
        PathSettings::makeDefaultPath(QString::fromStdWString(dir)));
  };

  // the same game, game folder and edition as this instance, the default folders
  // inside the new instance, local INIs and saves (§5.0)
  CreateInstanceDialog::CreationInfo ci;
  ci.type         = CreateInstanceDialog::Global;
  ci.game         = game;
  ci.gameLocation = QDir::toNativeSeparators(
      same ? m_core.managedGame()->gameDirectory().absolutePath()
           : game->gameDirectory().absolutePath());
  if (same) {
    ci.gameVariant = m_core.settings().game().edition().value_or(QString());
  } else if (game->gameVariants().size() == 1) {
    ci.gameVariant = game->gameVariants().front();
  }
  ci.instanceName = name;
  ci.dataPath =
      QDir::toNativeSeparators(InstanceManager::singleton().instancePath(name));
  ci.iniPath =
      ci.dataPath + "/" + QString::fromStdWString(AppConfig::iniFileName());
  ci.paths.base      = ci.dataPath;
  ci.paths.downloads = defaultPath(AppConfig::downloadPath());
  ci.paths.mods      = defaultPath(AppConfig::modsPath());
  ci.paths.profiles  = defaultPath(AppConfig::profilesPath());
  ci.paths.overwrite = defaultPath(AppConfig::overwritePath());
  ci.profileSettings = {true, true, m_core.settings().profileArchiveInvalidation()};

  QStringList lines;
  if (!createInstanceFiles(ci, [&](QString s) {
        lines << s;
        log::debug("{}", s);
      })) {
    QMessageBox::warning(m_parent, title,
                         tr("The instance cannot be created:\n\n%1")
                             .arg(lines.join("\n")));
    return;
  }

  // the record and the copy of the package go into the new instance
  QDir().mkpath(ci.paths.base);
  const QString copy = QDir(ci.paths.base).filePath(PackageCopy);
  QPointer<ImportFlow> self(this);
  const bool copied = copyPackageInBackground(packagePath, copy);
  if (!self) {
    return;
  }
  if (!copied) {
    QMessageBox::warning(m_parent, title,
                         tr("The instance \"%1\" was created, but the list cannot be "
                            "copied into it (%2).")
                             .arg(name, copy));
    return;
  }

  pending.packagePath = copy;
  pending.profileName = QString::fromStdWString(AppConfig::defaultProfileName());
  pending.versionChoices.clear();
  if (!writePending(QDir(ci.paths.base).filePath(PendingFile), pending)) {
    QFile::remove(copy);
    QMessageBox::warning(m_parent, title,
                         tr("The instance \"%1\" was created, but the import cannot "
                            "be recorded in it.")
                             .arg(name));
    return;
  }

  QMessageBox::information(m_parent, title,
                           tr("The instance \"%1\" was created. Mod Organizer restarts "
                              "into it now and continues the import there.")
                               .arg(name));

  InstanceManager::singleton().setCurrentInstance(name);
  ExitModOrganizer(Exit::Restart);
}

void ImportFlow::resumePending()
{
  const QString path = pendingPath();
  // a .mo2list being opened asks about the record itself
  if (busy() || !QFile::exists(path)) {
    return;
  }
  const Starting starting(*this);
  const QString title = tr("Unfinished modlist import");

  // Later keeps the record for the next start, Discard removes it
  const auto offerDiscard = [&](const QString& text) {
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::NoButton,
                    m_parent);
    auto* discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == discard) {
      discardPending();
    }
  };

  PendingImport pending;
  try {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      throw std::runtime_error(file.errorString().toStdString());
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
      throw std::runtime_error(error.errorString().toStdString());
    }
    pending = pendingFromJson(doc.object());
  } catch (const std::exception& e) {
    offerDiscard(tr("The record of an unfinished modlist import (%1) cannot be "
                    "read:\n\n%2")
                     .arg(path, QString::fromUtf8(e.what())));
    return;
  }

  std::optional<Mo2ListPackage> package;
  try {
    package.emplace(Mo2ListPackage::open(pending.packagePath));
  } catch (const std::exception& e) {
    offerDiscard(tr("The modlist of an unfinished import (%1) cannot be opened:\n\n%2")
                     .arg(pending.packagePath, QString::fromUtf8(e.what())));
    return;
  }

  const Plan& plan     = package->plan();
  const auto remaining = remainingEntries(pending, plan);

  QMessageBox box(QMessageBox::Question, title,
                  tr("Continue importing \"%1\"?").arg(plan.header.name),
                  QMessageBox::NoButton, m_parent);
  box.setInformativeText(tr("%1 of the %2 selected mods are not installed yet.")
                             .arg(remaining.size())
                             .arg(pending.selectedEntries.size()));
  auto* cont    = box.addButton(tr("Continue"), QMessageBox::AcceptRole);
  auto* discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
  box.addButton(tr("Later"), QMessageBox::RejectRole);
  box.setDefaultButton(cont);
  box.exec();

  if (box.clickedButton() == cont) {
    run(std::move(*package), std::move(pending));
  } else if (box.clickedButton() == discard) {
    discardPending();
  }
}

}  // namespace modlists
