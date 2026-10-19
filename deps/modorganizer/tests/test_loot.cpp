#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <filesystem>
#include <fstream>

#include "linux/loot_linux.h"
#include "tempdir.h"

namespace fs = std::filesystem;

namespace
{
QString q(const fs::path& p)
{
  return QString::fromStdString(p.string());
}

QString canonicalOf(const fs::path& p)
{
  return QFileInfo(q(p)).canonicalFilePath();
}

void append(const fs::path& p, const std::string& s)
{
  std::ofstream(p, std::ios::binary | std::ios::app) << s;
}

// the tests run with MO2_VFS_BRIDGE=direct: hostCanonicalPath() is the canonical path
class LootLinuxTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_oldBridge = qgetenv("MO2_VFS_BRIDGE");
    qputenv("MO2_VFS_BRIDGE", "direct");
  }
  void TearDown() override
  {
    if (m_oldBridge.isNull()) {
      qunsetenv("MO2_VFS_BRIDGE");
    } else {
      qputenv("MO2_VFS_BRIDGE", m_oldBridge);
    }
  }

  LootcliRun run(const fs::path& root) const
  {
    LootcliRun r;
    r.lootcli              = q(root / "bin/loot/lootcli");
    r.game                 = "SkyrimSE";
    r.gamePath             = q(root / "Módulos e Jogos/Skyrim Special Edition");
    r.pluginListPath       = q(root / "instance/profiles/Default/loadorder.txt");
    r.logLevel             = "info";
    r.reportPath           = q(root / "instance/logs/lootreport.json");
    r.sortedPluginListPath = q(root / "instance/logs/loot-loadorder.txt");
    r.outputPath           = q(root / "instance/logs/lootcli.log");
    r.language             = "pt_BR";
    return r;
  }

  void makeTree(TempDir& t) const
  {
    t.createFile("bin/loot/lootcli");
    t.createFile("instance/profiles/Default/loadorder.txt");
    fs::create_directories(t.path() / "Módulos e Jogos/Skyrim Special Edition/Data");
    fs::create_directories(t.path() / "instance/logs");
  }

private:
  QByteArray m_oldBridge;
};
}  // namespace

TEST_F(LootLinuxTest, LootFilesDirectoryIsTheLogsOfTheInstance)
{
  TempDir t;
  const auto dir = lootFilesDirectory(q(t.path()));
  EXPECT_EQ(dir, q(t.path() / "logs"));
  EXPECT_TRUE(QFileInfo(dir).isDir());
}

TEST_F(LootLinuxTest, LootFilesDirectoryWithoutDataPathIsTemp)
{
  EXPECT_EQ(lootFilesDirectory(QString()), QDir::tempPath());
}

TEST_F(LootLinuxTest, LootcliSpawnRequestArguments)
{
  TempDir t;
  makeTree(t);
  const auto root = fs::path(canonicalOf(t.path()).toStdString());

  const auto r = lootcliSpawnRequest(run(root));

  const QStringList expected = {q(root / "bin/loot/lootcli"),
                                "--game",
                                "SkyrimSE",
                                "--gamePath",
                                q(root / "Módulos e Jogos/Skyrim Special Edition"),
                                "--pluginListPath",
                                q(root / "instance/profiles/Default/loadorder.txt"),
                                "--logLevel",
                                "info",
                                "--out",
                                q(root / "instance/logs/lootreport.json"),
                                "--pluginListOutputPath",
                                q(root / "instance/logs/loot-loadorder.txt"),
                                "--language",
                                "pt_BR"};
  EXPECT_EQ(r.argv, expected);
  EXPECT_EQ(r.workingDirectory, q(root / "bin/loot"));
  EXPECT_EQ(r.output, q(root / "instance/logs/lootcli.log"));
  EXPECT_TRUE(r.winePrefix.isEmpty());
  EXPECT_TRUE(r.environment.isEmpty());
}

TEST_F(LootLinuxTest, LootcliSpawnRequestSkipsTheMasterlistUpdate)
{
  TempDir t;
  makeTree(t);
  auto r                 = run(t.path());
  r.skipUpdateMasterlist = true;

  EXPECT_EQ(lootcliSpawnRequest(r).argv.last(), "--skipUpdateMasterlist");
}

TEST_F(LootLinuxTest, LootcliSpawnRequestResolvesTheDirectoryOfNewFiles)
{
  // the logs are reached through a link; the output files don't exist yet
  TempDir t;
  makeTree(t);
  fs::create_symlink(t.path() / "instance/logs", t.path() / "logs-link");
  auto r       = run(t.path());
  r.outputPath = q(t.path() / "logs-link/lootcli.log");
  r.reportPath = q(t.path() / "logs-link/lootreport.json");

  const auto request = lootcliSpawnRequest(r);

  const auto logs = canonicalOf(t.path() / "instance/logs");
  EXPECT_EQ(request.output, logs + "/lootcli.log");
  EXPECT_EQ(request.argv[request.argv.indexOf("--out") + 1], logs + "/lootreport.json");
}

TEST_F(LootLinuxTest, AsyncPipeReadsWhatIsAppended)
{
  TempDir t;
  const auto file = t.path() / "lootcli.log";
  append(file, "old output\n");

  AsyncPipe pipe(q(file));
  ASSERT_TRUE(pipe.create());
  EXPECT_EQ(fs::file_size(file), 0u);  // emptied
  EXPECT_EQ(pipe.read(), "");

  append(file, "[progress] 1\n[info] a");
  EXPECT_EQ(pipe.read(), "[progress] 1\n[info] a");
  append(file, "bc\n");
  EXPECT_EQ(pipe.read(), "bc\n");
  EXPECT_EQ(pipe.read(), "");
}

TEST_F(LootLinuxTest, AsyncPipeInMissingDirectoryFails)
{
  TempDir t;
  AsyncPipe pipe(q(t.path() / "missing/lootcli.log"));
  EXPECT_FALSE(pipe.create());
  EXPECT_EQ(pipe.read(), "");
}

TEST_F(LootLinuxTest, LootcliOtherOutputKeepsTheLoaderError)
{
  TempDir t;
  const auto file = t.path() / "lootcli.log";
  append(file, "[progress] 1\n[info] reading plugins\n"
               "lootcli: error while loading shared libraries: libfoo.so.1: cannot "
               "open shared object file: No such file or directory\n"
               "\n[error] sorting failed\nError: last line without newline");

  const auto lines = lootcliOtherOutput(q(file));

  ASSERT_EQ(lines.size(), 2);
  EXPECT_TRUE(lines[0].startsWith("lootcli: error while loading shared libraries"));
  EXPECT_EQ(lines[1], "Error: last line without newline");
}

TEST_F(LootLinuxTest, LootcliOtherOutputKeepsTheLastLines)
{
  TempDir t;
  const auto file = t.path() / "lootcli.log";
  for (int i = 0; i < 30; ++i) {
    append(file, "line " + std::to_string(i) + "\n");
  }

  const auto lines = lootcliOtherOutput(q(file), 20);

  ASSERT_EQ(lines.size(), 20);
  EXPECT_EQ(lines.front(), "line 10");
  EXPECT_EQ(lines.back(), "line 29");
  EXPECT_TRUE(lootcliOtherOutput(q(t.path() / "missing.log")).isEmpty());
}

TEST_F(LootLinuxTest, LootUnavailableReason)
{
  TempDir t;
  EXPECT_EQ(lootUnavailableReason(q(t.path())),
            QString("lootcli was not found in %1.").arg(q(t.path() / "loot")));

  t.createFile("loot/lootcli");
  EXPECT_FALSE(lootUnavailableReason(q(t.path())).isEmpty());  // not executable

  fs::permissions(t.path() / "loot/lootcli", fs::perms::owner_all);
  EXPECT_TRUE(lootUnavailableReason(q(t.path())).isEmpty());
}
