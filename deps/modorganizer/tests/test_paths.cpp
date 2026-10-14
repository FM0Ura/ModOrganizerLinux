#include <gtest/gtest.h>

#include <QFile>
#include <QString>

#include "shared/directoryentry.h"
#include "shared/fileentry.h"
#include "shared/filesorigin.h"
#include "shared/util.h"
#include "tempdir.h"

using namespace MOShared;

TEST(Paths, NormalizesUserSeparators)
{
  EXPECT_EQ(normalizeSeparators("textures\\armor\\a.dds"), "textures/armor/a.dds");
  EXPECT_EQ(normalizeSeparators("textures/armor\\a.dds"), "textures/armor/a.dds");
  EXPECT_EQ(normalizeSeparators("/abs/path"), "/abs/path");
  EXPECT_EQ(normalizeSeparators(""), "");
}

TEST(Paths, DataTreeFullPathIsADiskPath)
{
  TempDir tmp;
  tmp.createFile("mods/A/textures/armor/a.dds", 3);

  const auto origin = tmp.path() / "mods" / "A";

  DirectoryEntry root(L"data", nullptr, 0);
  DirectoryStats stats;
  root.addFromOrigin(L"A", origin.wstring(), 1, stats);

  const auto file = root.searchFile(L"textures\\armor\\a.dds");
  ASSERT_TRUE(file);

  const auto expected = (origin / "textures" / "armor" / "a.dds").wstring();
  EXPECT_EQ(file->getFullPath(), expected);
  EXPECT_EQ(file->getFullPath(root.getOriginByName(L"A").getID()), expected);

  // relative to the origin, also with the separator of the system
  EXPECT_EQ(file->getRelativePath(), L"/textures/armor/a.dds");

  // the path can be opened
  QFile f(QString::fromStdWString(file->getFullPath()));
  ASSERT_TRUE(f.open(QIODevice::ReadOnly));
  EXPECT_EQ(f.readAll(), QByteArray("xxx"));

  // a file at the root of the origin
  tmp.createFile("mods/A/plugin.esp", 1);
  DirectoryEntry root2(L"data", nullptr, 0);
  DirectoryStats stats2;
  root2.addFromOrigin(L"A", origin.wstring(), 1, stats2);

  const auto esp = root2.searchFile(L"plugin.esp");
  ASSERT_TRUE(esp);
  EXPECT_EQ(esp->getFullPath(), (origin / "plugin.esp").wstring());
  EXPECT_TRUE(QFile::exists(QString::fromStdWString(esp->getFullPath())));
}
