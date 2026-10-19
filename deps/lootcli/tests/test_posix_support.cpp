#include "posix_support.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using lootcli::posix::downloadFile;
using lootcli::posix::lootDataPath;

namespace
{
// a temporary directory, removed on destruction
class TempDir
{
public:
  TempDir()
  {
    std::string tpl = (fs::temp_directory_path() / "lootcli-tests-XXXXXX").string();
    if (!::mkdtemp(tpl.data())) {
      throw std::runtime_error("mkdtemp failed");
    }
    m_path = tpl;
  }
  ~TempDir()
  {
    std::error_code ec;
    fs::remove_all(m_path, ec);
  }
  const fs::path& path() const { return m_path; }

private:
  fs::path m_path;
};

void write(const fs::path& p, const std::string& content)
{
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << content;
}

std::string read(const fs::path& p)
{
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string fileUrl(const fs::path& p)
{
  return "file://" + p.string();
}
}  // namespace

TEST(LootDataPath, FlatpakFolderWins)
{
  TempDir t;
  const auto flatpak = t.path() / ".var/app/io.github.loot.loot/data/LOOT";
  fs::create_directories(flatpak);
  EXPECT_EQ(lootDataPath(t.path().string(), "/xdg/data"), flatpak);
}

TEST(LootDataPath, XdgDataHome)
{
  TempDir t;
  EXPECT_EQ(lootDataPath(t.path().string(), "/xdg/data"), fs::path("/xdg/data/LOOT"));
}

TEST(LootDataPath, DefaultWithoutXdgDataHome)
{
  TempDir t;
  EXPECT_EQ(lootDataPath(t.path().string(), ""), t.path() / ".local/share/LOOT");
  // a relative XDG_DATA_HOME is invalid (XDG Base Directory specification)
  EXPECT_EQ(lootDataPath(t.path().string(), "relative"),
            t.path() / ".local/share/LOOT");
}

TEST(LootDataPath, FlatpakFileIsNotAFolder)
{
  TempDir t;
  write(t.path() / ".var/app/io.github.loot.loot/data/LOOT", "not a folder");
  EXPECT_EQ(lootDataPath(t.path().string(), ""), t.path() / ".local/share/LOOT");
}

TEST(LootDataPath, NoHome)
{
  EXPECT_EQ(lootDataPath("", ""), fs::path());
}

TEST(DownloadFile, ReplacesTheFile)
{
  TempDir t;
  write(t.path() / "source.yaml", "plugins: []\n");
  write(t.path() / "games/masterlist.yaml", "old\n");

  const auto error = downloadFile(fileUrl(t.path() / "source.yaml"),
                                  t.path() / "games/masterlist.yaml");

  EXPECT_FALSE(error) << *error;
  EXPECT_EQ(read(t.path() / "games/masterlist.yaml"), "plugins: []\n");
  EXPECT_FALSE(fs::exists(t.path() / "games/masterlist.yaml.download"));
}

TEST(DownloadFile, DownloadFailureKeepsTheOldFile)
{
  TempDir t;
  write(t.path() / "masterlist.yaml", "old\n");

  const auto error =
      downloadFile(fileUrl(t.path() / "missing.yaml"), t.path() / "masterlist.yaml");

  ASSERT_TRUE(error);
  EXPECT_FALSE(error->empty());
  EXPECT_EQ(read(t.path() / "masterlist.yaml"), "old\n");
  EXPECT_FALSE(fs::exists(t.path() / "masterlist.yaml.download"));
}

TEST(DownloadFile, ConnectionErrorIsAnError)
{
  // nothing listens on port 9 of localhost: a refused connection, without the network
  TempDir t;
  write(t.path() / "masterlist.yaml", "old\n");

  const auto error =
      downloadFile("http://127.0.0.1:9/masterlist.yaml", t.path() / "masterlist.yaml");

  ASSERT_TRUE(error);
  EXPECT_EQ(read(t.path() / "masterlist.yaml"), "old\n");
}

TEST(DownloadFile, UnwritableDestinationIsAnError)
{
  TempDir t;
  write(t.path() / "source.yaml", "x\n");

  const auto error = downloadFile(fileUrl(t.path() / "source.yaml"),
                                  t.path() / "no-such-dir/masterlist.yaml");

  ASSERT_TRUE(error);
  EXPECT_NE(error->find("no-such-dir"), std::string::npos) << *error;
}
