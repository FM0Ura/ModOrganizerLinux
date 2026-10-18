#ifndef PROTONSETTINGS_LINUX_H
#define PROTONSETTINGS_LINUX_H

#include <QGroupBox>

class Executable;
class QComboBox;
class QPlainTextEdit;

// the "Proton" group of the executables dialog on Linux: the Proton version
// ("Automatic" is Steam's choice) and the environment variables, one NAME=value per
// line
class ProtonSettingsWidget : public QGroupBox
{
  Q_OBJECT

public:
  explicit ProtonSettingsWidget(QWidget* parent = nullptr);

  void setFrom(const Executable& e);
  void saveTo(Executable& e) const;

  // nothing selected in the list
  void clearAndDisable();

signals:
  void changed();

private:
  QComboBox* m_proton;
  QPlainTextEdit* m_environment;

  void fillProtons(const QString& current);
};

#endif
