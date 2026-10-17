#include <gtest/gtest.h>

#include <uibase/exceptions.h>

#include <QMap>

#include "filemappingbuilder.h"
#include "tempdir.h"

namespace
{

QString q(const std::filesystem::path& p)
{
  return QString::fromStdString(p.string());
}

struct Fixture
{
  TempDir tmp;

  ActiveModDir mod(const QString& name, bool regular = true)
  {
    return {name, q(tmp.path() / "mods" / name.toStdString()), regular};
  }

  QString overwrite() { return q(tmp.path() / "overwrite"); }
};

QMap<QString, QStringList> dataOnly()
{
  return {{"", {"Data"}}};
}

}  // namespace

TEST(FileMapping, ModsInPriorityOrderThenOverwrite)
{
  Fixture f;
  f.tmp.createFile("mods/A/a.txt", 1);
  f.tmp.createFile("mods/B/b.txt", 1);
  f.tmp.createFile("overwrite/o.txt", 1);

  const auto m = buildModMappings({f.mod("A"), f.mod("B"), {"overwrite", "", true}},
                                  dataOnly(), f.overwrite(), "");

  ASSERT_EQ(m.size(), 3u);
  EXPECT_EQ(m[0].source, f.mod("A").path);
  EXPECT_EQ(m[0].destination, "Data");
  EXPECT_TRUE(m[0].isDirectory);
  EXPECT_FALSE(m[0].createTarget);
  EXPECT_EQ(m[1].source, f.mod("B").path);
  EXPECT_FALSE(m[1].createTarget);
  EXPECT_EQ(m[2].source, f.overwrite());
  EXPECT_TRUE(m[2].createTarget);
}

TEST(FileMapping, SkipsOverwriteEntryAndNonRegularMods)
{
  Fixture f;
  f.tmp.createFile("mods/A/a.txt", 1);
  f.tmp.createFile("mods/Sep/x.txt", 1);
  f.tmp.createFile("mods/Overwrite/x.txt", 1);

  const auto m = buildModMappings({f.mod("A"), f.mod("Sep", false), f.mod("Overwrite")},
                                  dataOnly(), f.overwrite(), "");

  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].source, f.mod("A").path);
}

TEST(FileMapping, DataMapSubdirectoriesAndSecondaryDirs)
{
  Fixture f;
  f.tmp.createFile("mods/A/a.txt", 1);
  f.tmp.createFile("mods/A/Root/r.txt", 1);

  const QMap<QString, QStringList> maps{{"", {"Data", "Data2"}}, {"Root", {"Game"}}};
  const auto m = buildModMappings({f.mod("A")}, maps, f.overwrite(), "");

  ASSERT_EQ(m.size(), 3u);
  int data = 0, data2 = 0, game = 0;
  for (const auto& e : m) {
    if (e.destination == "Data") {
      ++data;
      EXPECT_EQ(e.source, f.mod("A").path);
    } else if (e.destination == "Data2") {
      ++data2;
      EXPECT_EQ(e.source, f.mod("A").path);
    } else if (e.destination == "Game") {
      ++game;
      EXPECT_EQ(e.source, f.mod("A").path + "/Root");
    }
  }
  EXPECT_EQ(data, 1);
  EXPECT_EQ(data2, 1);
  EXPECT_EQ(game, 1);
}

TEST(FileMapping, CustomOverwriteIsCreateTarget)
{
  Fixture f;
  f.tmp.createFile("mods/A/a.txt", 1);
  f.tmp.createFile("mods/B/b.txt", 1);
  f.tmp.createFile("overwrite/o.txt", 1);

  const auto m =
      buildModMappings({f.mod("A"), f.mod("B")}, dataOnly(), f.overwrite(), "B");

  ASSERT_EQ(m.size(), 3u);
  EXPECT_FALSE(m[0].createTarget);
  EXPECT_EQ(m[1].source, f.mod("B").path);
  EXPECT_TRUE(m[1].createTarget);
  EXPECT_EQ(m[2].source, f.overwrite());
  EXPECT_FALSE(m[2].createTarget);
}

TEST(FileMapping, CustomOverwriteNotActiveThrows)
{
  Fixture f;
  f.tmp.createFile("mods/A/a.txt", 1);

  EXPECT_THROW(buildModMappings({f.mod("A")}, dataOnly(), f.overwrite(), "Nope"),
               MOBase::MyException);
}

TEST(FileMapping, MissingModSubdirIsNotMapped)
{
  Fixture f;
  f.tmp.createFile("mods/A/a.txt", 1);

  const QMap<QString, QStringList> maps{{"", {"Data"}}, {"Root", {"Game"}}};
  const auto m = buildModMappings({f.mod("A")}, maps, f.overwrite(), "");

  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].destination, "Data");
}
