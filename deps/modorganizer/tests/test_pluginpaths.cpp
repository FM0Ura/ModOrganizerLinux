#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

#include "pluginpaths.h"
#include "tempdir.h"

namespace
{

QString qpath(const std::filesystem::path& p)
{
  return QString::fromStdString(p.string());
}

}  // namespace

TEST(PluginPaths, UserFolderComesFirst)
{
  const QStringList paths = pluginsPaths();

  ASSERT_EQ(paths.size(), 2);
  EXPECT_EQ(paths[0], userPluginsPath());
  EXPECT_EQ(paths[1], builtInPluginsPath());
  EXPECT_TRUE(paths[0].endsWith("/plugins"));
  EXPECT_TRUE(paths[1].endsWith("/plugins"));
  EXPECT_NE(paths[0], paths[1]);
}

TEST(PluginPaths, MergeKeepsTheEntriesOfEveryFolder)
{
  const auto merged = mergePluginEntries(
      {{"/user/plugins/new.py", "/user/plugins/libtool.so"},
       {"/bin/plugins/libgame_skyrimse.so", "/bin/plugins/basic_games"}});

  EXPECT_EQ(merged, QStringList({"/user/plugins/new.py", "/user/plugins/libtool.so",
                                 "/bin/plugins/libgame_skyrimse.so",
                                 "/bin/plugins/basic_games"}));
}

TEST(PluginPaths, UserEntryHidesTheShippedOneWithTheSameName)
{
  const auto merged =
      mergePluginEntries({{"/user/plugins/libgame_skyrimse.so", "/user/plugins/basic_games"},
                          {"/bin/plugins/libgame_skyrimse.so", "/bin/plugins/basic_games",
                           "/bin/plugins/libinstaller_fomod.so"}});

  EXPECT_EQ(merged, QStringList({"/user/plugins/libgame_skyrimse.so",
                                 "/user/plugins/basic_games",
                                 "/bin/plugins/libinstaller_fomod.so"}));
}

TEST(PluginPaths, MergeOfNoEntries)
{
  EXPECT_TRUE(mergePluginEntries({}).isEmpty());
  EXPECT_TRUE(mergePluginEntries({{}, {}}).isEmpty());
}

TEST(PluginPaths, FilePathIsInTheFirstFolderThatHasIt)
{
  TempDir tmp;
  tmp.createFile("user/mine.py");
  tmp.createFile("user/both.py");
  tmp.createFile("bin/both.py");
  tmp.createFile("bin/shipped.py");

  const QString user = qpath(tmp.path() / "user");
  const QString bin  = qpath(tmp.path() / "bin");

  EXPECT_EQ(pluginFilePath({user, bin}, "mine.py"), user + "/mine.py");
  EXPECT_EQ(pluginFilePath({user, bin}, "both.py"), user + "/both.py");
  EXPECT_EQ(pluginFilePath({user, bin}, "shipped.py"), bin + "/shipped.py");
}

TEST(PluginPaths, FilePathOfAMissingPluginIsInTheFirstFolder)
{
  TempDir tmp;
  const QString user = qpath(tmp.path() / "user");
  const QString bin  = qpath(tmp.path() / "bin");

  EXPECT_EQ(pluginFilePath({user, bin}, "gone.py"), user + "/gone.py");
}
