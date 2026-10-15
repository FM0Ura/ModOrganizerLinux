// Parses the FOMOD sample of the ModOrganizerLinux tests (FOMOD_SAMPLE_DIR): one
// install step with a SelectExactlyOne group of two options, each installing one
// texture.
//
// The ModuleConfig.xml parser of the plugin builds the pages of the installer dialog as
// it reads the file, so the test runs it through FomodInstallerDialog::initData() (with
// the offscreen platform) and checks the pages and choices it created.

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTemporaryDir>

#include <ostream>

#include <uibase/guessedvalue.h>
#include <uibase/log.h>

#include "fomodinstallerdialog.h"

using namespace MOBase;

// readable values in the failure messages
QT_BEGIN_NAMESPACE
inline void PrintTo(const QString& s, std::ostream* os)
{
  *os << '"' << s.toStdString() << '"';
}

inline void PrintTo(const QStringList& list, std::ostream* os)
{
  *os << '{' << list.join(", ").toStdString() << '}';
}
QT_END_NAMESPACE

namespace
{

// copies the fixture into a directory under QDir::tempPath(), where the dialog reads
// the extracted archive from
QString extractSample(const QTemporaryDir& tmp)
{
  const QDir sample(FOMOD_SAMPLE_DIR);
  const QDir out(tmp.path());

  for (const char* file :
       {"fomod/ModuleConfig.xml", "textures/option_a.dds", "textures/option_b.dds"}) {
    const QString target = out.absoluteFilePath(file);
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!QFile::copy(sample.absoluteFilePath(file), target)) {
      return {};
    }
  }

  // the dialog takes the path relative to the temporary directory
  return QDir(QDir::tempPath()).relativeFilePath(tmp.path());
}

}  // namespace

TEST(Fomod, ParsesSample)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString fomodPath = extractSample(tmp);
  ASSERT_FALSE(fomodPath.isEmpty());

  FomodInstallerDialog dialog(nullptr, GuessedValue<QString>("FOMOD Sample"), fomodPath,
                              [](const QString&) {
                                return IPluginList::STATE_MISSING;
                              });
  dialog.initData(nullptr);

  auto* steps = dialog.findChild<QStackedWidget*>("stepsStack");
  ASSERT_NE(steps, nullptr);
  ASSERT_EQ(steps->count(), 1);
  EXPECT_TRUE(dialog.hasOptions());

  auto* step = qobject_cast<QGroupBox*>(steps->widget(0));
  ASSERT_NE(step, nullptr);
  EXPECT_EQ(step->title(), "Choose an option");

  const auto choices = step->findChildren<QAbstractButton*>("choice");
  ASSERT_EQ(choices.size(), 2);

  QStringList names;
  for (auto* choice : choices) {
    names.append(choice->text());

    // SelectExactlyOne: exclusive options
    EXPECT_NE(qobject_cast<QRadioButton*>(choice), nullptr)
        << choice->text().toStdString();

    // each option installs its texture
    const auto files = choice->property("files").toList();
    ASSERT_EQ(files.size(), 1);
    const auto* file = files[0].value<FileDescriptor*>();
    ASSERT_NE(file, nullptr);
    const QString texture = choice->text() == "Option A" ? "textures/option_a.dds"
                                                         : "textures/option_b.dds";
    EXPECT_EQ(file->m_Source, texture);
    EXPECT_EQ(file->m_Destination, texture);
  }
  names.sort();
  EXPECT_EQ(names, QStringList({"Option A", "Option B"}));
}

int main(int argc, char** argv)
{
  // the dialog needs a QApplication, without a display
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }

  QApplication app(argc, argv);

  // the plugin logs warnings and errors (unreadable saves, ...)
  log::LoggerConfiguration conf;
  conf.name     = "installer_fomod_tests";
  conf.maxLevel = log::Debug;
  conf.pattern  = "%^[%Y-%m-%d %H:%M:%S.%e %L] %v%$";
  log::createDefault(conf);

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
