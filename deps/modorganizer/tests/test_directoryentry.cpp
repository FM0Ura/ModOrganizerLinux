#include <gtest/gtest.h>

#include "shared/directoryentry.h"
#include "shared/fileentry.h"
#include "shared/filesorigin.h"
#include "shared/util.h"
#include "tempdir.h"

using namespace MOShared;

TEST(DirectoryEntry, CaseInsensitiveMerge)
{
  TempDir tmp;
  tmp.createFile("A/Textures/a.dds", 1);
  tmp.createFile("B/textures/A.DDS", 2);

  DirectoryEntry root(L"data", nullptr, 0);
  DirectoryStats stats;

  // origin B has the higher priority
  root.addFromOrigin(L"A", (tmp.path() / "A").wstring(), 1, stats);
  root.addFromOrigin(L"B", (tmp.path() / "B").wstring(), 2, stats);

  const OriginID a = root.getOriginByName(L"A").getID();
  const OriginID b = root.getOriginByName(L"B").getID();
  ASSERT_NE(a, b);

  // one directory, one file
  ASSERT_EQ(root.getSubDirectories().size(), 1u);
  const DirectoryEntry* textures = root.findSubDirectory(L"TEXTURES");
  ASSERT_NE(textures, nullptr);
  EXPECT_EQ(textures->getFiles().size(), 1u);

  const auto file = root.searchFile(L"textures\\a.dds");
  ASSERT_TRUE(file);
  EXPECT_EQ(file, textures->findFile(L"A.dds"));
  EXPECT_EQ(file, root.searchFile(L"TEXTURES/A.DDS"));

  // B wins, A is the alternative (the conflict)
  EXPECT_EQ(file->getOrigin(), b);
  ASSERT_EQ(file->getAlternatives().size(), 1u);
  EXPECT_EQ(file->getAlternatives()[0].originID(), a);
}

TEST(DirectoryEntry, CaseInsensitiveHelpers)
{
  EXPECT_TRUE(caseInsensitiveEqual(L"Textures", L"tEXTURES"));
  EXPECT_TRUE(caseInsensitiveEqual(L"ÜNÏCÖDÉ", L"ünïcödé"));
  EXPECT_FALSE(caseInsensitiveEqual(L"Textures", L"Texture"));
  EXPECT_FALSE(caseInsensitiveEqual(L"a.dds", L"b.dds"));

  EXPECT_EQ(caseInsensitiveCompare(L"abc", L"ABC"), 0);
  EXPECT_LT(caseInsensitiveCompare(L"abc", L"ABD"), 0);
  EXPECT_GT(caseInsensitiveCompare(L"Textures", L"meshes"), 0);
  EXPECT_LT(caseInsensitiveCompare(L"ab", L"ABC"), 0);

  EXPECT_EQ(ToLowerCopy(std::wstring(L"AbÉЖΣ")), L"abéжσ");
}
