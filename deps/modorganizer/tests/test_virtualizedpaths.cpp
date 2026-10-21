#include <gtest/gtest.h>

#include <QString>

#include <ostream>

#include "virtualizedpaths.h"

// readable QString values in the failure messages
QT_BEGIN_NAMESPACE
inline void PrintTo(const QString& s, std::ostream* os)
{
  *os << '"' << s.toStdString() << '"';
}
QT_END_NAMESPACE

namespace
{
const QString mods = "/home/u/.local/share/ModOrganizer/SSE/mods";
const QString data = "/home/u/Steam/steamapps/common/Skyrim Special Edition/Data";
}  // namespace

TEST(VirtualizedPaths, BinaryAndDirectoryInAModGoToTheDataDirectory)
{
  const auto p = virtualizedPaths(mods, data, mods + "/FNIS/tools/GenerateFNIS.exe",
                                  mods + "/FNIS/tools");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->binary, data + "/tools/GenerateFNIS.exe");
  EXPECT_EQ(p->workingDirectory, data + "/tools");
  EXPECT_EQ(p->mod, "FNIS");
}

TEST(VirtualizedPaths, KeepsSpacesOfTheModAndOfItsFolders)
{
  const QString mod = "Nemesis Unlimited Behavior Engine";
  const auto p      = virtualizedPaths(
      mods, data,
      mods + "/" + mod + "/Nemesis_Engine/Nemesis Unlimited Behavior Engine.exe",
      mods + "/" + mod + "/Nemesis_Engine");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->binary, data + "/Nemesis_Engine/Nemesis Unlimited Behavior Engine.exe");
  EXPECT_EQ(p->workingDirectory, data + "/Nemesis_Engine");
  EXPECT_EQ(p->mod, mod);
}

TEST(VirtualizedPaths, OnlyTheBinaryInAMod)
{
  const auto p = virtualizedPaths(mods, data, mods + "/Tool/tool.exe", "/home/u/work");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->binary, data + "/tool.exe");
  EXPECT_EQ(p->workingDirectory, "/home/u/work");
  EXPECT_EQ(p->mod, "Tool");
}

TEST(VirtualizedPaths, OnlyTheDirectoryInAMod)
{
  const auto p =
      virtualizedPaths(mods, data, "/usr/bin/python3", mods + "/Scripts/run");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->binary, "/usr/bin/python3");
  EXPECT_EQ(p->workingDirectory, data + "/run");
  EXPECT_TRUE(p->mod.isEmpty());
}

TEST(VirtualizedPaths, TheFolderOfAModIsTheDataDirectory)
{
  const auto p = virtualizedPaths(mods, data, mods + "/Tool/tool.exe", mods + "/Tool");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->workingDirectory, data);
}

TEST(VirtualizedPaths, NothingInTheModsDirectory)
{
  EXPECT_FALSE(
      virtualizedPaths(mods, data, "/home/u/SSEEdit/SSEEdit.exe", "/home/u/SSEEdit")
          .has_value());
  EXPECT_FALSE(
      virtualizedPaths(mods, data, "/home/u/SSEEdit/SSEEdit.exe", "").has_value());
}

TEST(VirtualizedPaths, ModsDirectoryWithATrailingSlash)
{
  const auto p = virtualizedPaths(mods + "/", data, mods + "/Tool/tool.exe", "");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->binary, data + "/tool.exe");
  EXPECT_TRUE(p->workingDirectory.isEmpty());
}

// like the comparison of Windows, where it comes from
TEST(VirtualizedPaths, ModsDirectoryIgnoringCase)
{
  QString other = mods;
  other.replace("/mods", "/MODS");
  const auto p = virtualizedPaths(mods, data, other + "/Tool/tool.exe", "");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->binary, data + "/tool.exe");
}

TEST(VirtualizedPaths, AnotherDirectoryStartingLikeMods)
{
  EXPECT_FALSE(virtualizedPaths(mods, data, mods + "2/Tool/tool.exe", mods + "2/Tool")
                   .has_value());
  EXPECT_FALSE(virtualizedPaths(mods, data, mods, "").has_value());
}
