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

const char* const sampleFiles[] = {"fomod/ModuleConfig.xml", "textures/option_a.dds",
                                   "textures/option_b.dds", "textures/extra.dds"};

// copies the conditional fixture into a directory under QDir::tempPath(), where the
// dialog reads the extracted archive from, and returns the path of the directory
// relative to QDir::tempPath()
QString extractConditionalSample(const QTemporaryDir& tmp)
{
  const QDir sample(FOMOD_SAMPLE2_DIR);
  const QDir out(tmp.path());

  for (const char* file : sampleFiles) {
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

// the archive of the sample, with its files under the directory of the dialog
std::shared_ptr<IFileTree> archiveTree(const QString& fomodPath)
{
  auto tree = MemoryTree::create();
  for (const char* file : sampleFiles) {
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
    m_FomodPath = extractConditionalSample(m_Tmp);
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
