#ifndef MODLISTS_DESTINATIONDIALOG_H
#define MODLISTS_DESTINATIONDIALOG_H

#include "importplan.h"

#include <QDialog>
#include <QStringList>

#include <memory>

namespace Ui
{
class DestinationDialog;
}

namespace modlists
{

// The first window of an import: where the list goes. Nothing is selected when it
// opens and OK stays disabled until a destination with a valid name is chosen (§5.0).
class DestinationDialog : public QDialog
{
  Q_OBJECT

public:
  // profiles: the profiles of this instance; differentGame: why the profiles of this
  // instance cannot be used (the list is for another game), empty when they can;
  // newInstanceProblem: why no new instance can be made, empty when it can
  DestinationDialog(const Header& header, const QStringList& profiles,
                    const QString& differentGame, const QString& newInstanceProblem,
                    QWidget* parent = nullptr);
  ~DestinationDialog() override;

  // valid once accepted
  Destination destination() const;
  // the name of the new instance (sanitized)
  QString instanceName() const;
  // the name of the new profile, or the existing profile
  QString profileName() const;

private:
  std::unique_ptr<Ui::DestinationDialog> ui;
  QStringList m_profiles;

  // enables OK and says what is wrong with the chosen destination
  void validate();
  QString problem() const;
};

}  // namespace modlists

#endif  // MODLISTS_DESTINATIONDIALOG_H
