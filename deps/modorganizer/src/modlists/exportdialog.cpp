#include "exportdialog.h"
#include "ui_exportdialog.h"

#include "exportreview.h"
#include "profile.h"

#include <uibase/log.h>
#include <uibase/utility.h>

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <functional>
#include <stdexcept>

using namespace MOBase;

namespace modlists
{

namespace
{

  // the relative path of an overwrite file, on its tree item
  constexpr int RelativePathRole = Qt::UserRole;

  // a progress dialog that does not hide when cancelled: Escape and the close button
  // only emit canceled()
  class CancellingProgress : public QProgressDialog
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

  // runs `work` on another thread and returns when it is done, the GUI responsive
  // meanwhile; `tick` runs on the GUI thread every 100 ms
  void runInBackground(const std::function<void()>& work,
                       const std::function<void()>& tick)
  {
    QEventLoop loop;
    QFutureWatcher<void> watcher;
    QObject::connect(&watcher, &QFutureWatcher<void>::finished, &loop,
                     &QEventLoop::quit);
    QTimer timer;
    timer.setInterval(100);
    QObject::connect(&timer, &QTimer::timeout, tick);

    QFuture<void> future = QtConcurrent::run(work);
    watcher.setFuture(future);
    timer.start();
    if (!future.isFinished()) {
      loop.exec();
    }
    future.waitForFinished();
  }

}  // namespace

ExportDialog::ExportDialog(OrganizerCore& core, const Profile& profile, QWidget* parent)
    : QDialog(parent), ui(new Ui::ExportDialog), m_collector(core, profile)
{
  ui->setupUi(this);

  m_mods         = m_collector.mods();
  m_plugins      = m_collector.plugins();
  m_inis         = m_collector.profileInis();
  m_executables  = m_collector.executables();
  m_config       = m_collector.profileConfig();
  m_markers      = m_collector.markers();
  m_base         = m_collector.baseHeader();
  m_overwriteDir = m_collector.overwriteDir();

  ui->nameEdit->setText(profile.name());
  ui->buttons->button(QDialogButtonBox::Save)->setText(tr("Save..."));

  auto* header = ui->nonNexusTable->horizontalHeader();
  header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(1, QHeaderView::ResizeToContents);

  fillNonNexusTable();
  fillOverwriteTree();

  connect(ui->nameEdit, &QLineEdit::textChanged, this, [this] {
    ui->buttons->button(QDialogButtonBox::Save)
        ->setEnabled(!ui->nameEdit->text().trimmed().isEmpty());
    queueRefresh();
  });
  connect(ui->includeInactive, &QCheckBox::toggled, this, [this] {
    fillNonNexusTable();
    queueRefresh();
  });
  connect(ui->overwriteTree, &QTreeWidget::itemChanged, this, [this] {
    queueRefresh();
  });
  connect(ui->buttons, &QDialogButtonBox::accepted, this, &ExportDialog::save);
  connect(ui->buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  refreshPreview();
}

ExportDialog::~ExportDialog() = default;

ExportOptions ExportDialog::options() const
{
  ExportOptions o;
  o.includeInactive = ui->includeInactive->isChecked();
  o.overwriteFiles  = checkedOverwriteFiles();
  o.listName        = ui->nameEdit->text().trimmed();
  o.author          = ui->authorEdit->text().trimmed();
  o.version         = ui->versionEdit->text().trimmed();
  o.readme          = ui->readmeEdit->toPlainText();
  return o;
}

void ExportDialog::fillNonNexusTable()
{
  const ExportOptions o = options();
  auto* table           = ui->nonNexusTable;
  table->setRowCount(0);

  for (std::size_t i = 0; i < m_mods.size(); ++i) {
    ExportMod& mod = m_mods[i];
    if (!isExported(mod, o) || hasNexusFiles(mod)) {
      continue;
    }

    const int row = table->rowCount();
    table->insertRow(row);

    auto* name = new QTableWidgetItem(mod.enabled ? mod.name
                                                  : tr("%1 (inactive)").arg(mod.name));
    name->setFlags(Qt::ItemIsEnabled);
    table->setItem(row, 0, name);

    auto* source = new QComboBox;
    source->addItem(tr("Manual source"), int(NonNexusChoice::Manual));
    source->addItem(tr("Made by me — include files"), int(NonNexusChoice::Bundled));
    source->addItem(tr("Leave out"), int(NonNexusChoice::Exclude));
    if (mod.nexusId > 0) {
      // the builder never bundles the files of a Nexus mod
      if (auto* model = qobject_cast<QStandardItemModel*>(source->model())) {
        auto* item = model->item(1);
        item->setEnabled(false);
        item->setToolTip(tr("This mod comes from Nexus (mod %1); its files cannot be "
                            "included. Use a manual source or leave it out.")
                             .arg(mod.nexusId));
      }
      if (mod.nonNexus == NonNexusChoice::Bundled) {
        mod.nonNexus = NonNexusChoice::Manual;
      }
    }
    source->setCurrentIndex(source->findData(int(mod.nonNexus)));
    table->setCellWidget(row, 1, source);

    auto* link = new QLineEdit(mod.manualUrl);
    link->setPlaceholderText(tr("Where to download it"));
    link->setEnabled(mod.nonNexus == NonNexusChoice::Manual);
    table->setCellWidget(row, 2, link);

    connect(source, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, i, source, link] {
              m_mods[i].nonNexus =
                  static_cast<NonNexusChoice>(source->currentData().toInt());
              link->setEnabled(m_mods[i].nonNexus == NonNexusChoice::Manual);
              queueRefresh();
            });
    connect(link, &QLineEdit::textEdited, this, [this, i](const QString& text) {
      m_mods[i].manualUrl = text.trimmed();
      queueRefresh();
    });
  }

  ui->nonNexusHelp->setText(
      table->rowCount() == 0
          ? tr("Every exported mod comes from Nexus.")
          : tr("These mods were not downloaded from Nexus. Choose how the list gets "
               "them."));
}

void ExportDialog::fillOverwriteTree()
{
  auto* tree = ui->overwriteTree;
  QSignalBlocker blocker(tree);
  tree->clear();

  QStringList files;
  if (!m_overwriteDir.isEmpty()) {
    const QDir root(m_overwriteDir);
    QDirIterator it(m_overwriteDir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      it.next();
      if (!it.fileInfo().isSymLink()) {
        files << root.relativeFilePath(it.filePath());
      }
    }
  }
  files.sort(Qt::CaseInsensitive);

  std::map<QString, QTreeWidgetItem*> dirs;
  const auto dirItem = [&](const QString& relativeDir) {
    QTreeWidgetItem* parent = nullptr;
    QString path;
    for (const auto& part : relativeDir.split('/', Qt::SkipEmptyParts)) {
      path = path.isEmpty() ? part : path + '/' + part;
      auto& item = dirs[path];
      if (item == nullptr) {
        item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
        item->setText(0, part);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable |
                       Qt::ItemIsAutoTristate);
        item->setCheckState(0, Qt::Unchecked);
      }
      parent = item;
    }
    return parent;
  };

  for (const auto& relative : files) {
    const QFileInfo info(relative);
    QTreeWidgetItem* parent = dirItem(info.path() == "." ? QString() : info.path());
    auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    item->setText(0, info.fileName());
    item->setData(0, RelativePathRole, relative);
    item->setCheckState(0, Qt::Unchecked);
    if (isExcludedFromPackage(relative)) {
      item->setFlags(Qt::NoItemFlags);
      item->setToolTip(0, tr("Never included: saves, logs and ModOrganizer.ini stay "
                             "on this computer."));
    } else {
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    }
  }

  if (files.isEmpty()) {
    ui->overwriteHelp->setText(tr("The overwrite folder is empty."));
  }
}

std::vector<QString> ExportDialog::checkedOverwriteFiles() const
{
  std::vector<QString> out;
  for (QTreeWidgetItemIterator it(ui->overwriteTree, QTreeWidgetItemIterator::Checked);
       *it; ++it) {
    const QString relative = (*it)->data(0, RelativePathRole).toString();
    if (!relative.isEmpty()) {
      out.push_back(relative);
    }
  }
  return out;
}

void ExportDialog::queueRefresh()
{
  if (!m_refreshQueued) {
    m_refreshQueued = true;
    QTimer::singleShot(0, this, [this] {
      m_refreshQueued = false;
      refreshPreview();
    });
  }
}

void ExportDialog::refreshPreview()
{
  // the worker of computeMd5() writes m_mods meanwhile; setBusy(false) refreshes
  if (m_busy) {
    return;
  }

  const ExportOptions o = options();
  const ExportResult r =
      buildExport(withPendingMd5(m_mods), m_plugins, m_inis, m_executables, m_config,
                  m_markers, m_overwriteDir, o, m_base);

  // overwrite files are bundled too
  ui->bundledWarning->setVisible(
      !o.overwriteFiles.empty() ||
      std::any_of(m_mods.begin(), m_mods.end(), [&](auto& m) {
        return isExported(m, o) && !hasNexusFiles(m) &&
               m.nonNexus == NonNexusChoice::Bundled;
      }));

  showWarnings(r.warnings);

  ui->executablesText->setPlainText(executablesReport(r.package.plan.executables));

  const int total = r.nexusCount + r.manualCount + r.bundledCount + r.excludedCount;
  ui->summaryLabel->setText(
      tr("%1 mods (%2 Nexus / %3 manual / %4 included / %5 left out), package size %6")
          .arg(total)
          .arg(r.nexusCount)
          .arg(r.manualCount)
          .arg(r.bundledCount)
          .arg(r.excludedCount)
          .arg(localizedByteSize(
              static_cast<unsigned long long>(packageSize(r.package)))));
}

void ExportDialog::showWarnings(const std::vector<ExportWarning>& warnings)
{
  ui->warningsList->clear();
  for (const auto& w : warnings) {
    ui->warningsList->addItem(warningText(w));
  }
  if (warnings.empty()) {
    ui->warningsList->addItem(tr("No warnings."));
  }
  ui->tabs->setTabText(ui->tabs->indexOf(ui->warningsTab),
                       tr("Warnings (%1)").arg(warnings.size()));
}

void ExportDialog::setBusy(bool busy)
{
  m_busy = busy;
  // not setEnabled() on the dialog: it would disable the progress dialog too
  for (auto* child : findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
    if (!child->isWindow()) {
      child->setEnabled(!busy);
    }
  }
  if (!busy) {
    queueRefresh();
  }
}

void ExportDialog::reject()
{
  if (!m_busy) {
    QDialog::reject();
  }
}

void ExportDialog::save()
{
  ExportOptions o = options();
  if (m_busy || o.listName.isEmpty()) {
    return;
  }

  // the missing MD5 the preview already showed (archives not in the downloads folder)
  const auto noMd5 = [](const ExportResult& result) {
    return std::count_if(result.warnings.begin(), result.warnings.end(), [](auto& w) {
      return w.kind == ExportWarning::Kind::NoMd5;
    });
  };
  const ExportResult preview =
      buildExport(withPendingMd5(m_mods), m_plugins, m_inis, m_executables, m_config,
                  m_markers, m_overwriteDir, o, m_base);
  const auto expected = noMd5(preview);

  // before the checksums, which take long: the package could not be written
  const QString tooLarge = packageSizeProblem(preview.package);
  if (!tooLarge.isEmpty()) {
    QMessageBox::warning(this, tr("Export modlist"),
                         tr("The modlist cannot be saved.\n\n%1").arg(tooLarge));
    return;
  }

  QString path = QFileDialog::getSaveFileName(
      this, tr("Save modlist"), QDir(QDir::homePath()).filePath(o.listName + ".mo2list"),
      tr("Mod Organizer modlist (*.mo2list)"));
  if (path.isEmpty()) {
    return;
  }
  if (!path.endsWith(".mo2list", Qt::CaseInsensitive)) {
    path += ".mo2list";
    if (QFile::exists(path) &&
        QMessageBox::question(this, tr("Save modlist"),
                              tr("%1 already exists. Replace it?").arg(path)) !=
            QMessageBox::Yes) {
      return;
    }
  }

  if (!computeMd5(o)) {
    return;
  }

  const ExportResult r = buildExport(m_mods, m_plugins, m_inis, m_executables, m_config,
                                     m_markers, m_overwriteDir, o, m_base);
  showWarnings(r.warnings);

  const auto failed = noMd5(r) - expected;
  if (failed > 0 &&
      QMessageBox::question(
          this, tr("Export modlist"),
          tr("%1 archive(s) could not be checksummed; the importer cannot verify those "
             "downloads. Save anyway?")
              .arg(failed),
          QMessageBox::Save | QMessageBox::Cancel,
          QMessageBox::Cancel) != QMessageBox::Save) {
    return;
  }

  if (!confirmLeaks(r.package)) {
    return;
  }

  if (writePackage(path, r.package)) {
    QMessageBox::information(this, tr("Export modlist"),
                             tr("The modlist was saved to %1.").arg(path));
    accept();
  }
}

bool ExportDialog::computeMd5(const ExportOptions& options)
{
  std::atomic<bool> cancelled{false};
  std::atomic<int> done{0}, total{0};

  CancellingProgress progress(tr("Computing the checksums of the archives..."),
                              QString(), 0, 0, this);
  auto* cancel = new QPushButton(tr("Cancel"));
  progress.setCancelButton(cancel);
  progress.setWindowTitle(tr("Export modlist"));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setAutoClose(false);
  progress.setAutoReset(false);
  // stays visible until the worker stops (after the archive it is hashing)
  disconnect(&progress, SIGNAL(canceled()), &progress, SLOT(cancel()));
  connect(&progress, &QProgressDialog::canceled, this, [&] {
    cancelled = true;
    progress.setLabelText(tr("Cancelling..."));
    cancel->setEnabled(false);
  });
  progress.show();

  // the dialog stays disabled while the worker uses m_mods
  setBusy(true);
  runInBackground(
      [&] {
        m_collector.fillMd5(m_mods, options, [&](int d, int t) {
          done  = d;
          total = t;
          return !cancelled.load();
        });
      },
      [&] {
        if (!cancelled) {
          progress.setMaximum(total);
          progress.setValue(done);
        }
      });
  setBusy(false);

  return !cancelled;
}

bool ExportDialog::confirmLeaks(const PackageInput& package)
{
  const QString home = QDir::cleanPath(QDir::homePath());
  const QString user = leakUserName(qEnvironmentVariable("USER"), home);
  const auto leaks   = findPackageLeaks(package, user, home);
  if (leaks.empty()) {
    return true;
  }

  QStringList lines;
  for (const auto& leak : leaks) {
    lines << QString("%1, line %2: %3").arg(leak.file).arg(leak.line).arg(leak.excerpt);
  }

  QMessageBox box(QMessageBox::Warning, tr("Export modlist"),
                  tr("The list still holds your home folder or your user name in %1 "
                     "place(s). Anyone you share it with can read them.")
                      .arg(leaks.size()),
                  QMessageBox::Cancel, this);
  constexpr int Shown = 10;
  QStringList shown   = lines.mid(0, Shown);
  if (lines.size() > Shown) {
    shown << tr("... and %1 more (see the details)").arg(lines.size() - Shown);
  }
  box.setInformativeText(shown.join('\n'));
  box.setDetailedText(lines.join('\n'));
  auto* saveAnyway = box.addButton(tr("Save anyway"), QMessageBox::AcceptRole);
  box.setDefaultButton(QMessageBox::Cancel);
  box.exec();

  return box.clickedButton() == saveAnyway;
}

bool ExportDialog::writePackage(const QString& path, const PackageInput& package)
{
  QProgressDialog progress(tr("Writing %1...").arg(QFileInfo(path).fileName()),
                           QString(), 0, 0, this);
  progress.setWindowTitle(tr("Export modlist"));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setCancelButton(nullptr);
  progress.show();

  QString error;
  setBusy(true);
  runInBackground(
      [&] {
        try {
          writeMo2List(path, package);
        } catch (const std::exception& e) {
          error = QString::fromUtf8(e.what());
        }
      },
      [] {});
  setBusy(false);
  progress.hide();

  if (!error.isEmpty()) {
    log::error("cannot write the modlist {}: {}", path, error);
    QMessageBox::critical(this, tr("Export modlist"),
                          tr("The modlist could not be saved:\n%1").arg(error));
    return false;
  }
  return true;
}

}  // namespace modlists
