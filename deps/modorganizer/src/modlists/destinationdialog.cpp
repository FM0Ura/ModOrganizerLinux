#include "destinationdialog.h"
#include "ui_destinationdialog.h"

#include "instancemanager.h"

#include <uibase/filesystemutilities.h>

#include <QPushButton>

namespace modlists
{

DestinationDialog::DestinationDialog(const Header& header, const QStringList& profiles,
                                     const QString& differentGame,
                                     const QString& newInstanceProblem,
                                     QWidget* parent)
    : QDialog(parent), ui(new Ui::DestinationDialog), m_profiles(profiles)
{
  ui->setupUi(this);

  QString title = header.name;
  if (!header.version.isEmpty()) {
    title += " " + header.version;
  }
  ui->headerLabel->setText(header.author.isEmpty()
                               ? tr("Import \"%1\"").arg(title)
                               : tr("Import \"%1\" by %2").arg(title, header.author));

  // the suggestions are the name of the list
  const QString suggested = header.name.trimmed();
  ui->instanceName->setText(InstanceManager::singleton().makeUniqueName(suggested));
  QString profile = suggested;
  for (int n = 2; profiles.contains(profile, Qt::CaseInsensitive); ++n) {
    profile = QString("%1 (%2)").arg(suggested).arg(n);
  }
  ui->profileName->setText(profile);
  ui->profiles->addItems(profiles);

  QStringList reasons;
  if (!differentGame.isEmpty()) {
    reasons << differentGame;
    ui->newProfile->setEnabled(false);
    ui->profileName->setEnabled(false);
    ui->existingProfile->setEnabled(false);
    ui->profiles->setEnabled(false);
  } else if (profiles.isEmpty()) {
    ui->existingProfile->setEnabled(false);
    ui->profiles->setEnabled(false);
  }
  if (!newInstanceProblem.isEmpty()) {
    reasons << newInstanceProblem;
    ui->newInstance->setEnabled(false);
    ui->instanceName->setEnabled(false);
  }
  ui->reasonLabel->setText(reasons.join("\n"));
  ui->reasonLabel->setVisible(!reasons.isEmpty());

  // nothing is checked: the user picks (§5.0)
  for (auto* radio : {ui->newInstance, ui->newProfile, ui->existingProfile}) {
    radio->setChecked(false);
    connect(radio, &QRadioButton::toggled, this, [this] {
      validate();
    });
  }

  // typing a name or picking a profile selects its destination
  connect(ui->instanceName, &QLineEdit::textEdited, this, [this] {
    ui->newInstance->setChecked(ui->newInstance->isEnabled());
    validate();
  });
  connect(ui->profileName, &QLineEdit::textEdited, this, [this] {
    ui->newProfile->setChecked(ui->newProfile->isEnabled());
    validate();
  });
  connect(ui->profiles, qOverload<int>(&QComboBox::activated), this, [this] {
    ui->existingProfile->setChecked(ui->existingProfile->isEnabled());
    validate();
  });

  connect(ui->buttons, &QDialogButtonBox::accepted, this, [this] {
    if (problem().isEmpty()) {
      accept();
    }
  });

  validate();
}

DestinationDialog::~DestinationDialog() = default;

Destination DestinationDialog::destination() const
{
  if (ui->newProfile->isChecked()) {
    return Destination::NewProfile;
  }
  if (ui->existingProfile->isChecked()) {
    return Destination::ExistingProfile;
  }
  return Destination::NewInstance;
}

QString DestinationDialog::instanceName() const
{
  return MOBase::sanitizeFileName(ui->instanceName->text().trimmed());
}

QString DestinationDialog::profileName() const
{
  if (ui->existingProfile->isChecked()) {
    return ui->profiles->currentText();
  }
  QString name = ui->profileName->text().trimmed();
  MOBase::fixDirectoryName(name);
  return name;
}

QString DestinationDialog::problem() const
{
  if (ui->newInstance->isChecked()) {
    const QString name = instanceName();
    if (name.isEmpty()) {
      return tr("Enter a name for the new instance.");
    }
    if (InstanceManager::singleton().instanceExists(name)) {
      return tr("An instance named \"%1\" already exists.").arg(name);
    }
    return {};
  }

  if (ui->newProfile->isChecked()) {
    QString name = ui->profileName->text().trimmed();
    if (name.isEmpty() || !MOBase::fixDirectoryName(name)) {
      return tr("Enter a valid name for the new profile.");
    }
    if (m_profiles.contains(name, Qt::CaseInsensitive)) {
      return tr("A profile named \"%1\" already exists.").arg(name);
    }
    return {};
  }

  if (ui->existingProfile->isChecked()) {
    return ui->profiles->currentText().isEmpty() ? tr("Choose a profile.") : QString();
  }

  // nothing chosen yet, not an error to show
  return QStringLiteral(" ");
}

void DestinationDialog::validate()
{
  const QString p = problem();
  ui->problemLabel->setText(p.trimmed());
  ui->buttons->button(QDialogButtonBox::Ok)->setEnabled(p.isEmpty());
}

}  // namespace modlists
