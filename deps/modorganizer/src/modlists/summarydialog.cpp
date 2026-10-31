#include "summarydialog.h"
#include "ui_summarydialog.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHeaderView>
#include <QPushButton>
#include <QTreeWidgetItem>

namespace modlists
{

namespace
{

  QString outcomeText(Outcome outcome)
  {
    switch (outcome) {
    case Outcome::Installed:
      return SummaryDialog::tr("Installed");
    case Outcome::Reused:
      return SummaryDialog::tr("Reused");
    case Outcome::Failed:
      return SummaryDialog::tr("Failed");
    case Outcome::Skipped:
      return SummaryDialog::tr("Skipped");
    }
    return {};
  }

}  // namespace

SummaryDialog::SummaryDialog(const Summary& summary, QWidget* parent)
    : QDialog(parent), ui(new Ui::SummaryDialog)
{
  ui->setupUi(this);

  connect(ui->copy, &QPushButton::clicked, this, [this] {
    QGuiApplication::clipboard()->setText(summaryText(m_summary));
  });
  connect(ui->retry, &QPushButton::clicked, this, [this] {
    emit retryRequested();
  });
  connect(ui->loot, &QPushButton::clicked, this, [this] {
    emit sortWithLootRequested();
  });

  setSummary(summary);
}

SummaryDialog::~SummaryDialog() = default;

void SummaryDialog::setSummary(const Summary& summary)
{
  m_summary = summary;

  int counts[4] = {0, 0, 0, 0};
  ui->rows->clear();
  for (const auto& row : summary.rows) {
    ++counts[static_cast<int>(row.outcome)];

    auto* item = new QTreeWidgetItem(ui->rows);
    item->setText(0, row.mod);
    item->setText(1, outcomeText(row.outcome));
    item->setText(2, row.detail);
    item->setToolTip(2, row.detail);

    if (row.outcome == Outcome::Failed && row.canInstallManually) {
      auto* button = new QPushButton(tr("Install manually"));
      button->setToolTip(tr("Opens the usual installation of this mod, with its "
                            "dialogs"));
      const QString mod = row.mod;
      connect(button, &QPushButton::clicked, this, [this, mod] {
        emit installManuallyRequested(mod);
      });
      ui->rows->setItemWidget(item, 3, button);
    }
  }
  ui->rows->header()->setSectionResizeMode(2, QHeaderView::Stretch);
  ui->rows->resizeColumnToContents(0);
  ui->rows->resizeColumnToContents(1);
  ui->rows->resizeColumnToContents(3);

  ui->countsLabel->setText(
      tr("%1 installed, %2 reused, %3 failed, %4 skipped")
          .arg(counts[static_cast<int>(Outcome::Installed)])
          .arg(counts[static_cast<int>(Outcome::Reused)])
          .arg(counts[static_cast<int>(Outcome::Failed)])
          .arg(counts[static_cast<int>(Outcome::Skipped)]));

  // the sections after the mods
  Summary rest = summary;
  rest.rows.clear();
  const QString details = summaryText(rest).trimmed();
  ui->details->setPlainText(details);
  ui->details->setVisible(!details.isEmpty());

  updateButtons();
}

void SummaryDialog::setCanRetry(bool canRetry)
{
  m_canRetry = canRetry;
  updateButtons();
}

void SummaryDialog::setBusy(bool busy)
{
  m_busy = busy;
  updateButtons();
}

void SummaryDialog::reject()
{
  if (!m_busy) {
    QDialog::reject();
  }
}

void SummaryDialog::updateButtons()
{
  ui->retry->setEnabled(m_canRetry && !m_busy);
  ui->loot->setVisible(m_summary.offerLoot);
  ui->loot->setEnabled(!m_busy);
  ui->close->setEnabled(!m_busy);
  for (int i = 0; i < ui->rows->topLevelItemCount(); ++i) {
    if (auto* w = ui->rows->itemWidget(ui->rows->topLevelItem(i), 3)) {
      w->setEnabled(!m_busy);
    }
  }
}

}  // namespace modlists
