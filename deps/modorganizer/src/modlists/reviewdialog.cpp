#include "reviewdialog.h"
#include "ui_reviewdialog.h"

#include <uibase/utility.h>

#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidgetItem>

namespace modlists
{

namespace
{

  enum Column
  {
    OrderColumn = 0,
    NameColumn,
    VersionColumn,
    SizeColumn,
    StatusColumn,
    ActionColumn
  };

  // the index of the entry in Plan::mods, on the row
  constexpr int EntryRole = Qt::UserRole;

  QString sizeText(qint64 size)
  {
    return size < 0 ? QString() : MOBase::localizedByteSize(size);
  }

}  // namespace

ReviewDialog::ReviewDialog(const Plan& plan, const std::vector<InstalledMod>& installed,
                           Destination destination, bool premium,
                           const std::set<SourceRef>& reusable,
                           const std::vector<ExecFinding>& findings, QWidget* parent)
    : QDialog(parent), ui(new Ui::ReviewDialog), m_plan(plan),
      m_destination(destination), m_premium(premium), m_reusable(reusable),
      m_statuses(classifyAll(plan, installed, destination))
{
  ui->setupUi(this);

  m_install = ui->buttons->addButton(tr("Download and install"),
                                     QDialogButtonBox::AcceptRole);
  connect(ui->buttons, &QDialogButtonBox::accepted, this, [this] {
    if (m_install->isEnabled()) {
      accept();
    }
  });

  fillHeader();
  fillRows(installed);

  // §6.3: whatever could run code is listed and must be confirmed
  m_hasExecutableContent = hasExecutableContent(findings, plan.executables);
  ui->executableGroup->setVisible(m_hasExecutableContent);
  ui->executableText->setPlainText(executableContentText(findings, plan.executables));
  ui->reviewedExecutables->setChecked(false);
  connect(ui->reviewedExecutables, &QCheckBox::toggled, this, [this] {
    updateFooter();
  });

  connect(ui->filter, &QLineEdit::textChanged, this, [this] {
    applyFilter();
  });
  connect(ui->hideInstalled, &QCheckBox::toggled, this, [this] {
    applyFilter();
  });
  connect(ui->checkAll, &QPushButton::clicked, this, [this] {
    setAllChecked(false, true);
  });
  connect(ui->uncheckOptional, &QPushButton::clicked, this, [this] {
    setAllChecked(true, false);
  });
  connect(ui->mods, &QTreeWidget::itemChanged, this,
          [this](QTreeWidgetItem*, int column) {
            if (column == NameColumn) {
              updateFooter();
            }
          });

  updateFooter();
}

ReviewDialog::~ReviewDialog() = default;

void ReviewDialog::fillHeader()
{
  const Header& h = m_plan.header;

  int mods = 0, alreadyInstalled = 0;
  qint64 download = 0;
  for (std::size_t i = 0; i < m_plan.mods.size(); ++i) {
    const ModEntry& entry = m_plan.mods[i];
    if (entry.isSeparator) {
      continue;
    }
    ++mods;
    if (m_statuses[i] == RowStatus::AlreadyInstalled) {
      ++alreadyInstalled;
    }
    for (const auto& s : entry.sources) {
      if (s.kind != SourceKind::Bundled && s.size > 0) {
        download += s.size;
      }
    }
  }

  QStringList lines;
  QString title = h.name;
  if (!h.version.isEmpty()) {
    title += " " + h.version;
  }
  lines << (h.author.isEmpty() ? title : tr("%1 by %2").arg(title, h.author));
  lines << tr("%1 mods, %2 to download at most, %3 already installed here")
               .arg(mods)
               .arg(MOBase::localizedByteSize(download))
               .arg(alreadyInstalled);
  ui->headerLabel->setText(lines.join("\n"));

  switch (m_destination) {
  case Destination::NewInstance:
    ui->destinationLabel->setText(
        tr("Destination: a new instance. The mods that are already installed here are "
           "not copied; their downloaded archives are reused when they are still in "
           "the downloads folder."));
    break;
  case Destination::NewProfile:
    ui->destinationLabel->setText(
        tr("Destination: a new profile of this instance. Your mods stay installed for "
           "your other profiles."));
    break;
  case Destination::ExistingProfile:
    ui->destinationLabel->setText(
        tr("Destination: an existing profile. The mods of the list go above yours; a "
           "different version is installed beside yours and yours is disabled in this "
           "profile only."));
    break;
  }

}

void ReviewDialog::fillRows(const std::vector<InstalledMod>& installed)
{
  ui->mods->setUpdatesEnabled(false);
  const QSignalBlocker blocker(ui->mods);

  int order = 0;
  for (std::size_t i = 0; i < m_plan.mods.size(); ++i) {
    const ModEntry& entry  = m_plan.mods[i];
    const RowStatus status = m_statuses[i];
    const int index        = static_cast<int>(i);

    auto* item = new QTreeWidgetItem(ui->mods);
    item->setData(NameColumn, EntryRole, index);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(NameColumn, entry.optional ? Qt::Unchecked : Qt::Checked);

    if (!entry.isSeparator) {
      item->setText(OrderColumn, QString::number(++order));
    }
    item->setText(NameColumn, entry.optional ? tr("%1 (optional)").arg(entry.name)
                                             : entry.name);
    item->setText(VersionColumn, entry.version);
    item->setText(SizeColumn, sizeText(entrySize(entry)));
    item->setText(StatusColumn,
                  statusText(entry, status, userVersion(entry, installed)));
    if (entry.isSeparator) {
      QFont f = item->font(NameColumn);
      f.setBold(true);
      item->setFont(NameColumn, f);
    }

    switch (status) {
    case RowStatus::AlreadyInstalled:
      item->setText(ActionColumn, m_destination == Destination::NewInstance
                                      ? tr("Reuse the download")
                                      : tr("Reuse"));
      break;

    case RowStatus::DifferentVersion: {
      auto* combo = new QComboBox;
      combo->addItem(tr("Download the list's version"),
                     static_cast<int>(VersionChoice::UseList));
      combo->addItem(tr("Use mine"), static_cast<int>(VersionChoice::KeepMine));
      connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        updateFooter();
      });
      ui->mods->setItemWidget(item, ActionColumn, combo);
      m_choices[index] = combo;
      break;
    }

    case RowStatus::Manual: {
      // the link comes from the list: shown as text, opened by the guided window
      const QString url = entry.sources.empty() ? QString() : entry.sources.front().url;
      auto* label       = new QLabel(url);
      label->setTextFormat(Qt::PlainText);
      label->setTextInteractionFlags(Qt::TextSelectableByMouse);
      label->setToolTip(url);
      ui->mods->setItemWidget(item, ActionColumn, label);
      break;
    }

    case RowStatus::Bundled:
      item->setText(ActionColumn, tr("Install"));
      break;

    case RowStatus::New:
      if (!entry.isSeparator) {
        item->setText(ActionColumn, tr("Download"));
      }
      break;
    }

    m_rows.push_back(item);
  }

  ui->mods->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
  for (int c : {OrderColumn, VersionColumn, SizeColumn, StatusColumn, ActionColumn}) {
    ui->mods->resizeColumnToContents(c);
  }
  ui->mods->setUpdatesEnabled(true);
}

void ReviewDialog::applyFilter()
{
  const QString text = ui->filter->text().trimmed();
  const bool hide    = ui->hideInstalled->isChecked();

  for (std::size_t i = 0; i < m_rows.size(); ++i) {
    const bool matches =
        text.isEmpty() || m_plan.mods[i].name.contains(text, Qt::CaseInsensitive);
    const bool installedRow = m_statuses[i] == RowStatus::AlreadyInstalled;
    m_rows[i]->setHidden(!matches || (hide && installedRow));
  }
}

void ReviewDialog::setAllChecked(bool optionalOnly, bool checked)
{
  {
    const QSignalBlocker blocker(ui->mods);
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
      if (!optionalOnly || m_plan.mods[i].optional) {
        m_rows[i]->setCheckState(NameColumn, checked ? Qt::Checked : Qt::Unchecked);
      }
    }
  }
  updateFooter();
}

std::vector<int> ReviewDialog::selectedEntries() const
{
  std::vector<int> out;
  for (std::size_t i = 0; i < m_rows.size(); ++i) {
    if (m_rows[i]->checkState(NameColumn) == Qt::Checked) {
      out.push_back(static_cast<int>(i));
    }
  }
  return out;
}

std::map<int, VersionChoice> ReviewDialog::versionChoices() const
{
  std::map<int, VersionChoice> out;
  for (const auto& [entry, combo] : m_choices) {
    out[entry] = static_cast<VersionChoice>(combo->currentData().toInt());
  }
  return out;
}

bool ReviewDialog::confirmedExecutableContent() const
{
  return !m_hasExecutableContent || ui->reviewedExecutables->isChecked();
}

void ReviewDialog::updateFooter()
{
  const auto selected = selectedEntries();

  int mods = 0, total = 0;
  for (std::size_t i = 0; i < m_plan.mods.size(); ++i) {
    if (!m_plan.mods[i].isSeparator) {
      ++total;
    }
  }
  for (int e : selected) {
    if (!m_plan.mods[e].isSeparator) {
      ++mods;
    }
  }

  const int clicks = nexusClicks(m_plan, m_statuses, m_destination, selected,
                                 versionChoices(), m_reusable);
  ui->selectedLabel->setText(tr("%1 of %2 mods selected").arg(mods).arg(total));
  ui->premiumLabel->setText(downloadsFooter(m_premium, clicks));

  m_install->setEnabled(!selected.empty() && confirmedExecutableContent());
  m_install->setToolTip(confirmedExecutableContent()
                            ? QString()
                            : tr("Check \"I reviewed the executable content\" first."));
}

}  // namespace modlists
