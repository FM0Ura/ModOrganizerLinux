#include <gtest/gtest.h>

#include <QString>

#include "shared/util.h"
#include "vfsconnector.h"

TEST(VfsConnector, NullStoresMapping)
{
  const auto vfs = createVfsConnector();
  ASSERT_TRUE(vfs);
  EXPECT_EQ(vfs->name(), "none");

  auto* null = dynamic_cast<NullVfsConnector*>(vfs.get());
  ASSERT_NE(null, nullptr);
  EXPECT_TRUE(null->lastMapping().empty());

  vfs->updateMapping({Mapping{"/a", "/b", true, false}});

  ASSERT_EQ(null->lastMapping().size(), 1u);
  EXPECT_EQ(null->lastMapping()[0].source, "/a");
  EXPECT_EQ(null->lastMapping()[0].destination, "/b");
  EXPECT_TRUE(null->lastMapping()[0].isDirectory);
  EXPECT_FALSE(null->lastMapping()[0].createTarget);

  // replaced, not appended
  vfs->updateMapping(
      {Mapping{"/c", "/d", false, true}, Mapping{"/e", "/f", true, true}});
  ASSERT_EQ(null->lastMapping().size(), 2u);
  EXPECT_EQ(null->lastMapping()[0].source, "/c");
  EXPECT_EQ(null->lastMapping()[1].destination, "/f");
}

// the mappings are disk paths: the ones built by OrganizerCore::fileMapping() and the
// ones given by plugins (local saves, file mappers) may contain backslashes, which are
// separators on Windows only
TEST(VfsConnector, NullMappingPathsAreDiskPaths)
{
  const auto vfs = createVfsConnector();
  auto* null     = dynamic_cast<NullVfsConnector*>(vfs.get());
  ASSERT_NE(null, nullptr);

  vfs->updateMapping({
      // a mod directory mapped to the data directory (fileMapping())
      Mapping{"/mo2/mods/A", "/games/Skyrim/Data", true, false},
      // a file of the data tree, relative path joined with '\'
      Mapping{"/mo2/mods/A\\textures\\a.dds", "/games/Skyrim/Data\\textures\\a.dds",
              false, false},
      // the local saves of a game plugin
      Mapping{"/mo2/profiles/Default/saves", "/games/My Games\\Skyrim\\Saves", true,
              true},
  });

  ASSERT_EQ(null->lastMapping().size(), 3u);

  EXPECT_EQ(null->lastMapping()[0].source, "/mo2/mods/A");
  EXPECT_EQ(null->lastMapping()[0].destination, "/games/Skyrim/Data");

  EXPECT_EQ(null->lastMapping()[1].source, "/mo2/mods/A/textures/a.dds");
  EXPECT_EQ(null->lastMapping()[1].destination, "/games/Skyrim/Data/textures/a.dds");

  EXPECT_EQ(null->lastMapping()[2].source, "/mo2/profiles/Default/saves");
  EXPECT_EQ(null->lastMapping()[2].destination, "/games/My Games/Skyrim/Saves");
  EXPECT_TRUE(null->lastMapping()[2].createTarget);
}

TEST(VfsConnector, NullAcceptsParamsAndForcedLibraries)
{
  const auto vfs = createVfsConnector();

  // nothing to configure without a virtual file system, but OrganizerCore calls them
  vfs->updateParams(MOBase::log::Debug, env::CoreDumpTypes::Mini, "/tmp/dumps",
                    std::chrono::seconds(0), "a.exe;b.exe", {".txt"}, {"dir"});
  vfs->updateForcedLibraries({});

  EXPECT_EQ(vfs->name(), "none");
}

// shown in the about dialog and logged on startup
TEST(VfsConnector, NoUsvfsOnLinux)
{
  EXPECT_EQ(MOShared::getUsvfsVersionString(), "none");
}
