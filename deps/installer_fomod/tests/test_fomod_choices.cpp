// Records and replays the choices of the conditional FOMOD sample of the
// ModOrganizerLinux tests (FOMOD_SAMPLE2_DIR): page 1 has a SelectExactlyOne group
// with the options "A" and "B" ("A" sets the flag a=on), and page 2 is only visible
// when the flag a is "on".
//
// The choices use the format of the Vortex collections:
//   [{"name": <step>, "groups": [{"name": <group>, "choices": [{"name", "idx"}]}]}]
//
// The dialog runs with the offscreen platform (main() of test_fomod_parse.cpp) and is
// never shown.

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <memory>
#include <stdexcept>
#include <string>

#include <uibase/guessedvalue.h>
#include <uibase/ifiletree.h>

#include "fomodinstallerdialog.h"

using namespace MOBase;

namespace
{

const QStringList sampleFiles{"fomod/ModuleConfig.xml", "textures/option_a.dds",
                              "textures/option_b.dds", "textures/extra.dds"};

// FOMOD_SAMPLE3_DIR: the first step is never visible (flag "never") and installs
// textures/hidden.dds from a SelectAll group; the second step has a SelectExactlyOne
// group with "X", "Y" and "Z" (not usable while Missing.esp is missing)
const QStringList hiddenFirstFiles{"fomod/ModuleConfig.xml", "textures/hidden.dds",
                                   "textures/x.dds", "textures/y.dds",
                                   "textures/z.dds"};

// copies the files of a fixture into a directory under QDir::tempPath(), where the
// dialog reads the extracted archive from, and returns the path of the directory
// relative to QDir::tempPath()
QString extractSample(const QTemporaryDir& tmp, const QString& sampleDir,
                      const QStringList& files)
{
  const QDir sample(sampleDir);
  const QDir out(tmp.path());

  for (const QString& file : files) {
    const QString target = out.absoluteFilePath(file);
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!QFile::copy(sample.absoluteFilePath(file), target)) {
      return {};
    }
  }

  return QDir(QDir::tempPath()).relativeFilePath(tmp.path());
}

// a file tree that only holds what is added to it, standing for the archive
class MemoryTree : public IFileTree
{
public:
  static std::shared_ptr<IFileTree> create()
  {
    return std::shared_ptr<MemoryTree>(new MemoryTree(nullptr, ""));
  }

protected:
  MemoryTree(std::shared_ptr<const IFileTree> parent, QString name)
      : FileTreeEntry(parent, name), IFileTree()
  {}

  std::shared_ptr<IFileTree> makeDirectory(std::shared_ptr<const IFileTree> parent,
                                           QString name) const override
  {
    return std::shared_ptr<MemoryTree>(new MemoryTree(parent, name));
  }

  bool doPopulate(std::shared_ptr<const IFileTree>,
                  std::vector<std::shared_ptr<FileTreeEntry>>&) const override
  {
    return true;
  }

  std::shared_ptr<IFileTree> doClone() const override
  {
    return std::shared_ptr<MemoryTree>(new MemoryTree(nullptr, name()));
  }
};

// the archive of a sample, with its files under the directory of the dialog
std::shared_ptr<IFileTree> archiveTree(const QString& fomodPath,
                                       const QStringList& files = sampleFiles)
{
  auto tree = MemoryTree::create();
  for (const QString& file : files) {
    tree->addFile(fomodPath + "/" + file);
  }
  return tree;
}

QStringList filePaths(const std::shared_ptr<const IFileTree>& tree)
{
  QStringList paths;
  for (const auto& entry : *tree) {
    if (entry->isDir()) {
      for (const QString& sub : filePaths(entry->astree())) {
        paths.append(entry->name() + "/" + sub);
      }
    } else {
      paths.append(entry->name());
    }
  }
  paths.sort();
  return paths;
}

QJsonObject choice(const QString& name, int idx)
{
  return QJsonObject{{"name", name}, {"idx", idx}};
}

QJsonObject group(const QString& name, const QJsonArray& choices)
{
  return QJsonObject{{"name", name}, {"choices", choices}};
}

QJsonObject step(const QString& name, const QJsonArray& groups)
{
  return QJsonObject{{"name", name}, {"groups", groups}};
}

std::string compact(const QJsonArray& array)
{
  return QJsonDocument(array).toJson(QJsonDocument::Compact).toStdString();
}

// the first page with the given option of the "Options" group
QJsonObject firstPage(const QString& option, int idx)
{
  return step("Choose an option", {group("Options", {choice(option, idx)})});
}

// the message of the std::runtime_error thrown by applyChoices(), or "" when it does
// not throw
std::string replayError(FomodInstallerDialog& dialog, const QJsonArray& choices)
{
  try {
    dialog.applyChoices(choices);
  } catch (const std::runtime_error& e) {
    return e.what();
  }
  return {};
}

class FomodChoices : public testing::Test
{
protected:
  void SetUp() override
  {
    ASSERT_TRUE(m_Tmp.isValid());
    m_FomodPath = extractSample(m_Tmp, FOMOD_SAMPLE2_DIR, sampleFiles);
    ASSERT_FALSE(m_FomodPath.isEmpty());

    m_Dialog = std::make_unique<FomodInstallerDialog>(
        nullptr, GuessedValue<QString>("FOMOD Conditional Sample"), m_FomodPath,
        [](const QString&) {
          return IPluginList::STATE_MISSING;
        });
    m_Dialog->setInteractive(false);
    m_Dialog->initData(nullptr);
  }

  void TearDown() override { EXPECT_FALSE(m_Dialog && m_Dialog->isVisible()); }

  QTemporaryDir m_Tmp;
  QString m_FomodPath;
  std::unique_ptr<FomodInstallerDialog> m_Dialog;
};

}  // namespace

TEST_F(FomodChoices, RecordDefault)
{
  // nothing clicked: page 1 with the default of the XML (the first optional option of
  // a SelectExactlyOne group); page 2 was not shown yet
  const QJsonArray expected{firstPage("A", 0)};
  EXPECT_EQ(compact(m_Dialog->selectedChoices()), compact(expected));
}

TEST_F(FomodChoices, ReplaySelectsOption)
{
  const QJsonArray choices{firstPage("B", 1)};
  ASSERT_EQ(replayError(*m_Dialog, choices), "");

  EXPECT_EQ(compact(m_Dialog->selectedChoices()), compact(choices));

  auto tree = archiveTree(m_FomodPath);
  ASSERT_EQ(m_Dialog->updateTree(tree), IPluginInstaller::RESULT_SUCCESS);
  EXPECT_EQ(filePaths(tree), QStringList({"textures/option_b.dds"}));
}

TEST_F(FomodChoices, ReplayShowsConditionalPage)
{
  // with A, page 2 is shown and recorded
  const QJsonArray choices{
      firstPage("A", 0), step("Extras for A", {group("Extras", {choice("Extra", 0)})})};
  ASSERT_EQ(replayError(*m_Dialog, choices), "");

  EXPECT_EQ(compact(m_Dialog->selectedChoices()), compact(choices));

  auto tree = archiveTree(m_FomodPath);
  ASSERT_EQ(m_Dialog->updateTree(tree), IPluginInstaller::RESULT_SUCCESS);
  EXPECT_EQ(filePaths(tree),
            QStringList({"textures/extra.dds", "textures/option_a.dds"}));
}

TEST_F(FomodChoices, ReplayKeepsDefaultOfUnrecordedPage)
{
  // page 2 is visible with A but has no record: its default (nothing) is accepted
  const QJsonArray choices{firstPage("A", 0)};
  ASSERT_EQ(replayError(*m_Dialog, choices), "");

  const QJsonArray expected{firstPage("A", 0),
                            step("Extras for A", {group("Extras", {})})};
  EXPECT_EQ(compact(m_Dialog->selectedChoices()), compact(expected));
}

TEST_F(FomodChoices, HiddenPageNotRequired)
{
  // with B, page 2 stays hidden and is not in the record
  ASSERT_EQ(replayError(*m_Dialog, {firstPage("B", 1)}), "");
  const QJsonArray recorded = m_Dialog->selectedChoices();
  ASSERT_EQ(recorded.size(), 1);
  EXPECT_EQ(recorded[0].toObject()["name"].toString(), "Choose an option");

  // a record with page 2 and option B: page 2 is not shown with these choices
  FomodInstallerDialog other(nullptr, GuessedValue<QString>("FOMOD Conditional Sample"),
                             m_FomodPath, [](const QString&) {
                               return IPluginList::STATE_MISSING;
                             });
  other.setInteractive(false);
  other.initData(nullptr);
  const std::string error = replayError(
      other, {firstPage("B", 1),
              step("Extras for A", {group("Extras", {choice("Extra", 0)})})});
  EXPECT_NE(error.find("FOMOD step 'Extras for A' is not shown"), std::string::npos)
      << error;
}

TEST_F(FomodChoices, MissingOptionFails)
{
  const std::string error = replayError(*m_Dialog, {firstPage("C", 2)});
  EXPECT_NE(error.find("FOMOD option 'C' not found in group 'Options'"),
            std::string::npos)
      << error;
}

TEST_F(FomodChoices, MissingGroupFails)
{
  const std::string error = replayError(
      *m_Dialog, {step("Choose an option", {group("Other", {choice("A", 0)})})});
  EXPECT_NE(error.find("FOMOD group 'Other' not found in step 'Choose an option'"),
            std::string::npos)
      << error;
}

TEST_F(FomodChoices, MissingStepFails)
{
  const std::string error =
      replayError(*m_Dialog, {firstPage("B", 1), step("Nowhere", {})});
  EXPECT_NE(error.find("FOMOD step 'Nowhere' not found"), std::string::npos) << error;
}

TEST_F(FomodChoices, MissingFileFailsWithoutDialog)
{
  // without the user, a file missing from the archive fails instead of asking
  ASSERT_EQ(replayError(*m_Dialog, {firstPage("B", 1)}), "");

  auto tree = MemoryTree::create();
  tree->addFile(m_FomodPath + "/fomod/ModuleConfig.xml");
  try {
    m_Dialog->updateTree(tree);
    ADD_FAILURE() << "updateTree() did not throw";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("FOMOD files missing from the archive"),
              std::string::npos)
        << e.what();
  }
}

namespace
{

// a dialog on the hidden-first-step sample, read without the user
std::unique_ptr<FomodInstallerDialog> hiddenFirstDialog(const QString& fomodPath)
{
  auto dialog = std::make_unique<FomodInstallerDialog>(
      nullptr, GuessedValue<QString>("FOMOD Hidden First Sample"), fomodPath,
      [](const QString&) {
        return IPluginList::STATE_MISSING;
      });
  dialog->setInteractive(false);
  dialog->initData(nullptr);
  return dialog;
}

QJsonObject visibleStep(const QJsonArray& choices)
{
  return step("Visible step", {group("Visible group", choices)});
}

class FomodHiddenFirst : public testing::Test
{
protected:
  void SetUp() override
  {
    ASSERT_TRUE(m_Tmp.isValid());
    m_FomodPath = extractSample(m_Tmp, FOMOD_SAMPLE3_DIR, hiddenFirstFiles);
    ASSERT_FALSE(m_FomodPath.isEmpty());
  }

  QTemporaryDir m_Tmp;
  QString m_FomodPath;
};

}  // namespace

TEST_F(FomodHiddenFirst, RecordSkipsHiddenFirstStep)
{
  auto dialog = hiddenFirstDialog(m_FomodPath);
  const QJsonArray expected{visibleStep({choice("X", 0)})};
  EXPECT_EQ(compact(dialog->selectedChoices()), compact(expected));
}

TEST_F(FomodHiddenFirst, RecordReplayRoundTrip)
{
  // what one install records, another replays
  auto first = hiddenFirstDialog(m_FomodPath);
  ASSERT_EQ(replayError(*first, {visibleStep({choice("Y", 1)})}), "");
  const QJsonArray recorded = first->selectedChoices();
  EXPECT_EQ(compact(recorded), compact(QJsonArray{visibleStep({choice("Y", 1)})}));

  auto second = hiddenFirstDialog(m_FomodPath);
  ASSERT_EQ(replayError(*second, recorded), "");
  EXPECT_EQ(compact(second->selectedChoices()), compact(recorded));
}

TEST_F(FomodHiddenFirst, UpdateTreeSkipsHiddenFirstStep)
{
  // the SelectAll group of the hidden step is checked, but its files stay out
  auto dialog = hiddenFirstDialog(m_FomodPath);
  ASSERT_EQ(replayError(*dialog, {visibleStep({choice("Y", 1)})}), "");

  auto tree = archiveTree(m_FomodPath, hiddenFirstFiles);
  ASSERT_EQ(dialog->updateTree(tree), IPluginInstaller::RESULT_SUCCESS);
  EXPECT_EQ(filePaths(tree), QStringList({"textures/y.dds"}));
}

TEST_F(FomodHiddenFirst, DisabledOptionFails)
{
  // "Z" exists but is not usable: a stale record fails instead of being ignored
  auto dialog             = hiddenFirstDialog(m_FomodPath);
  const std::string error = replayError(*dialog, {visibleStep({choice("Z", 2)})});
  EXPECT_NE(error.find("FOMOD option 'Z' cannot be selected in group 'Visible group'"),
            std::string::npos)
      << error;
}

TEST_F(FomodHiddenFirst, EmptyRecordOfRequiredGroupFails)
{
  // a SelectExactlyOne group recorded without a choice
  auto dialog             = hiddenFirstDialog(m_FomodPath);
  const std::string error = replayError(*dialog, {visibleStep({})});
  EXPECT_NE(
      error.find("FOMOD group 'Visible group' needs a choice in step 'Visible step'"),
      std::string::npos)
      << error;
}

TEST(FomodReplayDetail, PrefixesFailures)
{
  EXPECT_EQ(InstallerFomod::replayFailureDetail("FOMOD option 'C' not found"),
            "FOMOD option 'C' not found");
  EXPECT_EQ(InstallerFomod::replayFailureDetail("unsupported group type Foo"),
            "FOMOD unsupported group type Foo");
}

TEST(FomodReplayDetail, UnmetModuleDependency)
{
  // an unmet moduleDependencies throws a message without the "FOMOD " prefix; the
  // detail of the replay gets it, so the importer offers "Install manually"
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  QDir().mkpath(tmp.filePath("fomod"));
  QFile xml(tmp.filePath("fomod/ModuleConfig.xml"));
  ASSERT_TRUE(xml.open(QIODevice::WriteOnly));
  xml.write(R"(<?xml version="1.0" encoding="UTF-8"?>
<config>
  <moduleName>FOMOD Dependency Sample</moduleName>
  <moduleDependencies operator="And">
    <fileDependency file="Required.esp" state="Active"/>
  </moduleDependencies>
</config>
)");
  xml.close();

  FomodInstallerDialog dialog(nullptr, GuessedValue<QString>("FOMOD Dependency Sample"),
                              QDir(QDir::tempPath()).relativeFilePath(tmp.path()),
                              [](const QString&) {
                                return IPluginList::STATE_MISSING;
                              });
  dialog.setInteractive(false);
  try {
    dialog.initData(nullptr);
    FAIL() << "initData() did not throw";
  } catch (const std::exception& e) {
    const QString detail = InstallerFomod::replayFailureDetail(e.what());
    EXPECT_TRUE(detail.startsWith("FOMOD ")) << detail.toStdString();
    EXPECT_TRUE(detail.contains("Required.esp")) << detail.toStdString();
  }
}
