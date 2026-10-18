// The Proton settings of an executable in the executables dialog (Linux).

#include "protonsettings_linux.h"
#include "../executableslist.h"

#include <QComboBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QSignalBlocker>

#include <uibase/linux/steam.h>

ProtonSettingsWidget::ProtonSettingsWidget(QWidget* parent)
    : QGroupBox(tr("Proton"), parent), m_proton(new QComboBox(this)),
      m_environment(new QPlainTextEdit(this))
{
  auto* form = new QFormLayout(this);
  form->addRow(tr("Version"), m_proton);
  form->addRow(tr("Environment"), m_environment);
  m_environment->setPlaceholderText(tr("NAME=value, one per line"));
  m_environment->setMaximumHeight(80);
  setToolTip(tr("Used for Windows programs, which run with Proton in the prefix of "
                "their Steam game."));

  connect(m_proton, &QComboBox::currentIndexChanged, this,
          &ProtonSettingsWidget::changed);
  connect(m_environment, &QPlainTextEdit::textChanged, this,
          &ProtonSettingsWidget::changed);
}

void ProtonSettingsWidget::fillProtons(const QString& current)
{
  const QSignalBlocker block(m_proton);
  m_proton->clear();
  m_proton->addItem(tr("Automatic (same as Steam)"), QString());

  for (const auto& p : MOBase::steam::installedProtons()) {
    m_proton->addItem(p.displayName, p.internalName);
  }

  if (current.isEmpty()) {
    m_proton->setCurrentIndex(0);
    return;
  }

  int index = m_proton->findData(current, Qt::UserRole, Qt::MatchFixedString);
  if (index < 0) {
    index = m_proton->findText(current, Qt::MatchFixedString);
  }
  if (index < 0) {
    m_proton->addItem(tr("%1 (not installed)").arg(current), current);
    index = m_proton->count() - 1;
  }
  m_proton->setCurrentIndex(index);
}

void ProtonSettingsWidget::setFrom(const Executable& e)
{
  setEnabled(true);
  fillProtons(e.proton());
  const QSignalBlocker block(m_environment);
  m_environment->setPlainText(e.environment().join('\n'));
}

void ProtonSettingsWidget::saveTo(Executable& e) const
{
  e.proton(m_proton->currentData().toString());

  QStringList env;
  for (const auto& line : m_environment->toPlainText().split('\n')) {
    if (!line.trimmed().isEmpty()) {
      env.append(line.trimmed());
    }
  }
  e.environment(env);
}

void ProtonSettingsWidget::clearAndDisable()
{
  const QSignalBlocker b1(m_proton), b2(m_environment);
  m_proton->clear();
  m_environment->clear();
  setEnabled(false);
}
