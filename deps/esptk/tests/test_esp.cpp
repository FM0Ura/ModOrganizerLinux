#include <esptk/espexceptions.h>
#include <esptk/espfile.h>

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace
{

std::string data(const char* name)
{
  return std::string(MO2_ESPTK_TEST_DATA) + "/" + name;
}

}  // namespace

TEST(Esp, ReadsHeader)
{
  ESP::File file(data("light.esp"));

  EXPECT_EQ(file.masters(), std::set<std::string>({"Skyrim.esm"}));
  EXPECT_TRUE(file.isLight());
  EXPECT_FALSE(file.isMaster());
  EXPECT_FALSE(file.isMedium());
  EXPECT_TRUE(file.isOverlay());  // 0x200 is the light flag and, for Starfield, overlay
  EXPECT_FALSE(file.isLight(true));  // the alternate light flag is 0x100
  EXPECT_FALSE(file.isBlueprint());
  EXPECT_FALSE(file.isDummy());
  EXPECT_EQ(file.author(), "mo2 test");
  EXPECT_EQ(file.description(), "synthetic light plugin");
  EXPECT_EQ(file.formVersion(), 44);
  EXPECT_FLOAT_EQ(file.headerVersion(), 1.7f);
}

TEST(Esp, ReadsHeaderFromWideFileName)
{
  const std::string narrow = data("light.esp");
  ESP::File file(std::wstring(narrow.begin(), narrow.end()));

  EXPECT_EQ(file.masters(), std::set<std::string>({"Skyrim.esm"}));
  EXPECT_TRUE(file.isLight());
}

TEST(Esp, MasterFlagWithoutMasters)
{
  ESP::File file(data("master.esm"));

  EXPECT_TRUE(file.isMaster());
  EXPECT_FALSE(file.isLight());
  EXPECT_TRUE(file.masters().empty());
  EXPECT_TRUE(file.isDummy());  // zero records
}

TEST(Esp, MissingFileThrows)
{
  EXPECT_THROW(ESP::File(data("missing.esp")), ESP::InvalidFileException);
}

TEST(Esp, InvalidFileTypeThrows)
{
  EXPECT_THROW(ESP::File(data("garbage.esp")), ESP::InvalidFileException);
}
