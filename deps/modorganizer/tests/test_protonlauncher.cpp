#include "../src/linux/protonlauncher_linux.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace
{
void write(const QString& path, const QByteArray& content)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.write(content);
}

class ProtonLauncher : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_oldHome = qgetenv("HOME");
    qputenv("HOME", QFile::encodeName(m_tmp.path()));
    m_steam = m_tmp.path() + "/.local/share/Steam";
    m_game  = m_steam + "/steamapps/common/Skyrim Special Edition";
    write(m_steam + "/steamapps/appmanifest_489830.acf",
          "\"AppState\" { \"installdir\" \"Skyrim Special Edition\" }");
    write(m_game + "/SkyrimSE.exe", "MZ");
    write(m_game + "/tools/run me.bat", "@echo off");
    QDir().mkpath(m_steam + "/steamapps/compatdata/489830/pfx");
    write(m_steam + "/steamapps/compatdata/489830/version", "11.0-100");
    write(m_steam + "/steamapps/common/Proton - Experimental/proton",
          "CURRENT_PREFIX_VERSION=\"11.0-100\"");
    write(m_steam + "/compatibilitytools.d/GE-Proton11-7/proton", "x");
    write(m_steam + "/compatibilitytools.d/GE-Proton11-7/compatibilitytool.vdf",
          "\"compatibilitytools\" { \"compat_tools\" { \"GE-Proton11-7\" { "
          "\"install_path\" \".\" \"display_name\" \"GE-Proton11-7\" } } }");
  }

  void TearDown() override { qputenv("HOME", m_oldHome); }

  proton::LaunchParameters exe(const QString& rel, const QString& args = {})
  {
    proton::LaunchParameters p;
    p.binary         = QFileInfo(m_game + "/" + rel);
    p.arguments      = args;
    p.gameSteamAppId = "489830";
    return p;
  }

  QTemporaryDir m_tmp;
  QByteArray m_oldHome;
  QString m_steam, m_game;
};
}  // namespace

TEST_F(ProtonLauncher, WindowsExeArgv)
{
  const auto r = proton::makeLaunch(exe("SkyrimSE.exe"));
  EXPECT_EQ(r.argv, (QStringList{"umu-run", m_game + "/SkyrimSE.exe"}));
  EXPECT_EQ(r.workingDirectory, m_game);
  EXPECT_EQ(r.winePrefix, m_steam + "/steamapps/compatdata/489830");
  EXPECT_EQ(
      r.environment,
      (QStringList{"WINEPREFIX=" + m_steam + "/steamapps/compatdata/489830",
                   "GAMEID=umu-489830", "STORE=steam", "SteamAppId=489830",
                   "SteamGameId=489830",
                   "PROTONPATH=" + m_steam + "/steamapps/common/Proton - Experimental",
                   "STEAM_COMPAT_CLIENT_INSTALL_PATH=" + m_steam}));
}

TEST_F(ProtonLauncher, ArgumentsAreSplitLikeTheCommandLine)
{
  const auto r =
      proton::makeLaunch(exe("SkyrimSE.exe", R"(-forcesteamloader "a b" c)"));
  EXPECT_EQ(r.argv, (QStringList{"umu-run", m_game + "/SkyrimSE.exe",
                                 "-forcesteamloader", "a b", "c"}));
}

TEST_F(ProtonLauncher, ArgumentSplittingKeepsBackslashes)
{
  const auto r = proton::makeLaunch(exe("SkyrimSE.exe", R"(-d C:\Games\x a"b c"d)"));
  EXPECT_EQ(r.argv, (QStringList{"umu-run", m_game + "/SkyrimSE.exe", "-d",
                                 R"(C:\Games\x)", "ab cd"}));
}

TEST_F(ProtonLauncher, ApostrophesAreLiteral)
{
  const auto r = proton::makeLaunch(exe("SkyrimSE.exe", "-title Assassin's Creed"));
  EXPECT_EQ(r.argv, (QStringList{"umu-run", m_game + "/SkyrimSE.exe", "-title",
                                 "Assassin's", "Creed"}));
}

TEST_F(ProtonLauncher, BatchFilesRunThroughCmd)
{
  const auto r = proton::makeLaunch(exe("tools/run me.bat", "x"));
  EXPECT_EQ(r.argv,
            (QStringList{"umu-run", "cmd", "/c", m_game + "/tools/run me.bat", "x"}));
  EXPECT_EQ(r.workingDirectory, m_game + "/tools");
}

TEST_F(ProtonLauncher, ExecutableSettingsWin)
{
  auto p             = exe("SkyrimSE.exe");
  p.proton           = "ge-proton11-7";
  p.environment      = {"DXVK_HUD=fps", "WINEDLLOVERRIDES=dxgi=n,b"};
  p.workingDirectory = m_tmp.path();
  p.output           = "/tmp/x.log";
  const auto r       = proton::makeLaunch(p);
  EXPECT_TRUE(r.environment.contains("PROTONPATH=" + m_steam +
                                     "/compatibilitytools.d/GE-Proton11-7"));
  EXPECT_EQ(r.environment.mid(r.environment.size() - 2),
            (QStringList{"DXVK_HUD=fps", "WINEDLLOVERRIDES=dxgi=n,b"}));
  EXPECT_EQ(r.workingDirectory, m_tmp.path());
  EXPECT_EQ(r.output, "/tmp/x.log");
}

TEST_F(ProtonLauncher, ExecutableAppIdChoosesThePrefix)
{
  write(m_steam + "/steamapps/appmanifest_22330.acf",
        "\"AppState\" { \"installdir\" \"Oblivion\" }");
  QDir().mkpath(m_steam + "/steamapps/common/Oblivion");
  QDir().mkpath(m_steam + "/steamapps/compatdata/22330/pfx");
  auto p       = exe("SkyrimSE.exe");
  p.steamAppId = "22330";
  p.proton     = "GE-Proton11-7";
  const auto r = proton::makeLaunch(p);
  EXPECT_EQ(r.winePrefix, m_steam + "/steamapps/compatdata/22330");
  EXPECT_TRUE(r.environment.contains("SteamAppId=22330"));
}

TEST_F(ProtonLauncher, ConfiguredProtonMissing)
{
  auto p   = exe("SkyrimSE.exe");
  p.proton = "proton_8";
  try {
    proton::makeLaunch(p);
    FAIL();
  } catch (const proton::LaunchError& e) {
    const std::string m = e.what();
    EXPECT_NE(m.find("proton_8"), std::string::npos) << m;
    EXPECT_NE(m.find("GE-Proton11-7"), std::string::npos) << m;
  }
}

TEST_F(ProtonLauncher, SteamMappingProtonMissing)
{
  write(m_steam + "/config/config.vdf",
        "\"InstallConfigStore\" { \"Software\" { \"Valve\" { \"Steam\" { "
        "\"CompatToolMapping\" { \"489830\" { \"name\" \"proton_8\" } } } } } }");
  try {
    proton::makeLaunch(exe("SkyrimSE.exe"));
    FAIL();
  } catch (const proton::LaunchError& e) {
    const std::string m = e.what();
    EXPECT_NE(m.find("proton_8"), std::string::npos) << m;
    EXPECT_NE(m.find("not installed"), std::string::npos) << m;
  }
}

TEST_F(ProtonLauncher, NoPrefix)
{
  QDir(m_steam + "/steamapps/compatdata").removeRecursively();
  try {
    proton::makeLaunch(exe("SkyrimSE.exe"));
    FAIL();
  } catch (const proton::LaunchError& e) {
    const std::string m = e.what();
    EXPECT_NE(m.find("Run the game once from Steam"), std::string::npos) << m;
  }
}

TEST_F(ProtonLauncher, NoGameAppId)
{
  auto p           = exe("SkyrimSE.exe");
  p.gameSteamAppId = "";
  try {
    proton::makeLaunch(p);
    FAIL();
  } catch (const proton::LaunchError& e) {
    const std::string m = e.what();
    EXPECT_NE(m.find("no Steam app ID"), std::string::npos) << m;
  }
}

TEST_F(ProtonLauncher, NativeProgramsRunDirectly)
{
  write(m_tmp.path() + "/tool.sh", "#!/bin/sh\n");
  proton::LaunchParameters p;
  p.binary      = QFileInfo(m_tmp.path() + "/tool.sh");
  p.arguments   = R"(a "b c")";
  p.environment = {"X=1"};
  const auto r  = proton::makeLaunch(p);
  EXPECT_EQ(r.argv, (QStringList{m_tmp.path() + "/tool.sh", "a", "b c"}));
  EXPECT_EQ(r.environment, QStringList{"X=1"});
  EXPECT_EQ(r.workingDirectory, m_tmp.path());
  EXPECT_TRUE(r.winePrefix.isEmpty());
}

TEST_F(ProtonLauncher, InvalidEnvironmentEntriesAreDropped)
{
  EXPECT_EQ(proton::validEnvironment(
                {"FOO", "=x", "1A=b", "", "  ", "OK=1", "_U=", "A_B9=x=y"}),
            (QStringList{"OK=1", "_U=", "A_B9=x=y"}));
}

TEST_F(ProtonLauncher, WindowsProgramTypes)
{
  for (const auto* n : {"a.exe", "a.EXE", "a.bat", "a.Cmd", "a.com"}) {
    EXPECT_TRUE(proton::isWindowsProgram(QFileInfo(n))) << n;
  }
  for (const auto* n : {"a.sh", "a", "a.exe.txt", "a.jar"}) {
    EXPECT_FALSE(proton::isWindowsProgram(QFileInfo(n))) << n;
  }
}
