#include <uibase/linux/steam.h>

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace MOBase;

namespace
{
void write(const QString& path, const QByteArray& content)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.write(content);
}

// HOME with a native Steam (~/.local/share/Steam) whose second library is under
// <tmp>/disk2; Skyrim SE (489830) is installed in disk2, its prefix in the main library
class LinuxSteam : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_oldHome = qgetenv("HOME");
    qputenv("HOME", QFile::encodeName(home()));
    m_steam = home() + "/.local/share/Steam";
    m_disk2 = m_tmp.path() + "/disk2";
    write(m_steam + "/steamapps/libraryfolders.vdf",
          ("\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + m_steam +
           "\"\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + m_disk2 +
           "\"\n\t\t\"apps\" { \"489830\" \"1\" }\n\t}\n}\n")
              .toUtf8());
    write(m_disk2 + "/steamapps/appmanifest_489830.acf",
          "\"AppState\"\n{\n\t\"appid\"\t\t\"489830\"\n\t\"installdir\"\t\t\"Skyrim "
          "Special Edition\"\n}\n");
    QDir().mkpath(m_disk2 + "/steamapps/common/Skyrim Special Edition");
    QDir().mkpath(m_steam + "/steamapps/compatdata/489830/pfx");
    write(m_steam + "/steamapps/compatdata/489830/version", "11.0-100\n");
    write(m_steam + "/steamapps/common/Proton - Experimental/proton",
          "#!/usr/bin/env python3\nCURRENT_PREFIX_VERSION=\"11.0-100\"\n");
    write(m_steam + "/steamapps/common/Proton 9.0/proton",
          "CURRENT_PREFIX_VERSION=\"9.0-200\"\n");
    // a runtime without a proton script is not a Proton
    QDir().mkpath(m_steam + "/steamapps/common/Proton EasyAntiCheat Runtime");
    write(m_steam + "/compatibilitytools.d/GE-Proton11-7-x86_64/proton",
          "CURRENT_PREFIX_VERSION=\"GE-Proton11-7\"\n");
    write(m_steam + "/compatibilitytools.d/GE-Proton11-7-x86_64/compatibilitytool.vdf",
          "\"compatibilitytools\"\n{\n  \"compat_tools\"\n  {\n    "
          "\"GE-Proton11-7-x86_64\" // Internal name\n    {\n      \"install_path\" "
          "\".\"\n      \"display_name\" \"GE-Proton11-7\"\n    }\n  }\n}\n");
  }

  void TearDown() override { qputenv("HOME", m_oldHome); }

  QString home() const { return m_tmp.path() + "/home"; }

  void mapping(const QByteArray& entries)
  {
    write(m_steam + "/config/config.vdf",
          "\"InstallConfigStore\"\n{\n\"Software\"\n{\n\"Valve\"\n{\n\"Steam\"\n{\n"
          "\"CompatToolMapping\"\n{\n" +
              entries + "}\n}\n}\n}\n}\n");
  }

  QTemporaryDir m_tmp;
  QByteArray m_oldHome;
  QString m_steam, m_disk2;
};
}  // namespace

TEST_F(LinuxSteam, RootsAreUniqueAndExisting)
{
  // ~/.steam/steam is a link to the native installation, as Steam makes it
  QDir().mkpath(home() + "/.steam");
  ASSERT_TRUE(QFile::link(m_steam, home() + "/.steam/steam"));
  EXPECT_EQ(steam::roots(), QStringList{m_steam});

  QDir().mkpath(home() + "/.var/app/com.valvesoftware.Steam/.local/share/Steam");
  EXPECT_EQ(
      steam::roots(),
      (QStringList{m_steam,
                   home() + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"}));
}

TEST_F(LinuxSteam, Libraries)
{
  QDir().mkpath(m_disk2);
  EXPECT_EQ(steam::libraries(m_steam), (QStringList{m_steam, m_disk2}));
}

TEST_F(LinuxSteam, FindAppInOtherDiskWithPrefixInMainLibrary)
{
  const auto app = steam::findApp("489830");
  ASSERT_TRUE(app);
  EXPECT_EQ(app->root, m_steam);
  EXPECT_EQ(app->library, m_disk2);
  EXPECT_EQ(app->gamePath, m_disk2 + "/steamapps/common/Skyrim Special Edition");
  EXPECT_EQ(app->compatData, m_steam + "/steamapps/compatdata/489830");
}

TEST_F(LinuxSteam, PrefixInTheGameLibraryWins)
{
  QDir().mkpath(m_disk2 + "/steamapps/compatdata/489830/pfx");
  EXPECT_EQ(steam::findApp("489830")->compatData,
            m_disk2 + "/steamapps/compatdata/489830");
}

TEST_F(LinuxSteam, NoPrefixAndBadIds)
{
  QDir(m_steam + "/steamapps/compatdata/489830").removeRecursively();
  EXPECT_EQ(steam::findApp("489830")->compatData, "");
  EXPECT_FALSE(steam::findApp("1"));
  EXPECT_FALSE(steam::findApp("../489830"));
  EXPECT_FALSE(steam::findApp(""));
}

TEST_F(LinuxSteam, FlatpakSteam)
{
  const auto flatpak = home() + "/.var/app/com.valvesoftware.Steam/.local/share/Steam";
  QDir(m_steam).removeRecursively();
  write(flatpak + "/steamapps/appmanifest_489830.acf",
        "\"AppState\" { \"installdir\" \"Skyrim Special Edition\" }");
  QDir().mkpath(flatpak + "/steamapps/common/Skyrim Special Edition");
  QDir().mkpath(flatpak + "/steamapps/compatdata/489830/pfx");
  const auto app = steam::findApp("489830");
  ASSERT_TRUE(app);
  EXPECT_EQ(app->root, flatpak);
  EXPECT_EQ(app->compatData, flatpak + "/steamapps/compatdata/489830");
}

TEST_F(LinuxSteam, InstalledProtons)
{
  const auto protons = steam::installedProtons();
  ASSERT_EQ(protons.size(), 3u);
  EXPECT_EQ(protons[0].displayName, "Proton - Experimental");
  EXPECT_EQ(protons[0].internalName, "proton_experimental");
  EXPECT_EQ(protons[0].path, m_steam + "/steamapps/common/Proton - Experimental");
  EXPECT_EQ(protons[1].displayName, "Proton 9.0");
  EXPECT_EQ(protons[1].internalName, "proton_9");
  EXPECT_EQ(protons[2].displayName, "GE-Proton11-7");
  EXPECT_EQ(protons[2].internalName, "GE-Proton11-7-x86_64");
  EXPECT_EQ(protons[2].path, m_steam + "/compatibilitytools.d/GE-Proton11-7-x86_64");
  EXPECT_EQ(steam::findProton(protons, "ge-proton11-7")->internalName,
            "GE-Proton11-7-x86_64");
  EXPECT_FALSE(steam::findProton(protons, "proton_8"));
}

TEST_F(LinuxSteam, CompatToolOfTheAppThenTheDefault)
{
  mapping("\"0\" { \"name\" \"proton_9\" }\n"
          "\"489830\" { \"name\" \"GE-Proton11-7-x86_64\" \"config\" \"\" }\n");
  EXPECT_EQ(steam::compatToolName("489830"), "GE-Proton11-7-x86_64");
  EXPECT_EQ(steam::compatToolName("22330"), "proton_9");
  EXPECT_EQ(steam::protonForApp(*steam::findApp("489830"))->displayName,
            "GE-Proton11-7");
}

TEST_F(LinuxSteam, MappedProtonNotInstalled)
{
  mapping("\"489830\" { \"name\" \"proton_8\" }\n");
  EXPECT_FALSE(steam::protonForApp(*steam::findApp("489830")));
}

TEST_F(LinuxSteam, NoMappingUsesThePrefixVersion)
{
  EXPECT_EQ(steam::compatToolName("489830"), "");
  EXPECT_EQ(steam::protonForApp(*steam::findApp("489830"))->internalName,
            "proton_experimental");

  write(m_steam + "/steamapps/compatdata/489830/version", "1.0-1\n");
  EXPECT_FALSE(steam::protonForApp(*steam::findApp("489830")));
}
