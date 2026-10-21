// The Steam games that xEdit supports, on Linux: each plugin (loaded like Mod Organizer
// loads it, with QPluginLoader) finds its game in a Steam library of a temporary HOME
// and puts My Games (the INIs xEdit reads) and AppData/Local (plugins.txt) in the Proton
// prefix of that game's app id (spec of Phase 4c.3, §3).

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QTemporaryDir>

#include <uibase/iplugingame.h>

#include <memory>
#include <ostream>

using namespace MOBase;

namespace
{

struct SteamGame
{
  const char* name;        // test name
  const char* plugin;      // the plugin file
  const char* appId;       // Steam app id
  const char* installDir;  // steamapps/common/<installDir>
  const char* myGames;     // Documents/My Games/<myGames>, as on Windows
  const char* localApp;    // AppData/Local/<localApp>, the folder the plugin reads
};

void PrintTo(const SteamGame& g, std::ostream* os)
{
  *os << g.name;
}

const SteamGame steamGames[] = {
    {"Skyrim", GAME_PLUGIN_SKYRIM, "72850", "Skyrim", "Skyrim", "Skyrim"},
    {"SkyrimVR", GAME_PLUGIN_SKYRIMVR, "611670", "SkyrimVR", "Skyrim VR", "Skyrim VR"},
    {"Enderal", GAME_PLUGIN_ENDERAL, "933480", "Enderal", "Enderal", "enderal"},
    {"EnderalSE", GAME_PLUGIN_ENDERALSE, "976620", "Enderal Special Edition",
     "Enderal Special Edition", "Enderal Special Edition"},
    {"Oblivion", GAME_PLUGIN_OBLIVION, "22330", "Oblivion", "Oblivion", "Oblivion"},
    {"Fallout3", GAME_PLUGIN_FALLOUT3, "22300", "Fallout 3", "Fallout3", "Fallout3"},
    {"FalloutNV", GAME_PLUGIN_FALLOUTNV, "22380", "Fallout New Vegas", "FalloutNV",
     "FalloutNV"},
    {"Fallout4", GAME_PLUGIN_FALLOUT4, "377160", "Fallout 4", "Fallout4", "Fallout4"},
    {"Fallout4VR", GAME_PLUGIN_FALLOUT4VR, "611660", "Fallout 4 VR", "Fallout4VR",
     "Fallout4VR"},
    {"Starfield", GAME_PLUGIN_STARFIELD, "1716740", "Starfield", "Starfield", "Starfield"},
};

bool write(const QString& path, const QByteArray& content)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

QByteArray read(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// a HOME with the native Steam client, whose library has the game and its Proton
// prefix (compatdata/<app id>/pfx), and a second prefix of another app id that must
// never be used
class SteamGames : public testing::TestWithParam<SteamGame>
{
protected:
  void SetUp() override
  {
    const SteamGame& g = GetParam();
    ASSERT_TRUE(m_home.isValid());
    m_oldHome = qgetenv("HOME");
    qputenv("HOME", QFile::encodeName(m_home.path()));
    m_oldConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_home.path() + "/.config"));

    const QString steam = m_home.path() + "/.local/share/Steam";
    m_game              = steam + "/steamapps/common/" + g.installDir;
    m_user = steam + "/steamapps/compatdata/" + g.appId + "/pfx/drive_c/users/steamuser";
    m_profile = m_home.path() + "/profile";

    ASSERT_TRUE(write(steam + "/steamapps/libraryfolders.vdf",
                      "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" +
                          QFile::encodeName(steam) +
                          "\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"" + QByteArray(g.appId) +
                          "\"\t\t\"0\"\n\t\t}\n\t}\n}\n"));
    ASSERT_TRUE(write(steam + "/steamapps/appmanifest_" + g.appId + ".acf",
                      "\"AppState\"\n{\n\t\"appid\"\t\t\"" + QByteArray(g.appId) +
                          "\"\n\t\"installdir\"\t\t\"" + QByteArray(g.installDir) +
                          "\"\n}\n"));
    ASSERT_TRUE(QDir().mkpath(m_user));
    ASSERT_TRUE(QDir().mkpath(
        steam + "/steamapps/compatdata/489830/pfx/drive_c/users/steamuser"));
    ASSERT_TRUE(QDir().mkpath(m_game + "/Data"));
    ASSERT_TRUE(QDir().mkpath(m_profile));

    m_loader          = std::make_unique<QPluginLoader>(g.plugin);
    QObject* instance = m_loader->instance();
    ASSERT_NE(instance, nullptr) << m_loader->errorString().toStdString();
    m_plugin = qobject_cast<IPluginGame*>(instance);
    ASSERT_NE(m_plugin, nullptr);

    // the plugin only finds a game whose binary is there
    ASSERT_TRUE(write(m_game + "/" + m_plugin->binaryName(), ""));
  }

  void TearDown() override
  {
    qputenv("HOME", m_oldHome);
    if (m_oldConfigHome.isNull()) {
      qunsetenv("XDG_CONFIG_HOME");
    } else {
      qputenv("XDG_CONFIG_HOME", m_oldConfigHome);
    }
  }

  QTemporaryDir m_home;
  QByteArray m_oldHome;
  QByteArray m_oldConfigHome;
  QString m_game;
  QString m_user;
  QString m_profile;
  std::unique_ptr<QPluginLoader> m_loader;
  IPluginGame* m_plugin = nullptr;
};

}  // namespace

TEST_P(SteamGames, DetectsTheGame)
{
  m_plugin->detectGame();

  EXPECT_TRUE(m_plugin->isInstalled());
  EXPECT_EQ(m_plugin->gameDirectory().absolutePath().toStdString(),
            QDir(m_game).absolutePath().toStdString());
}

TEST_P(SteamGames, MyGamesInThePrefixOfTheGame)
{
  m_plugin->detectGame();

  const QString documents = m_user + "/Documents/My Games/" + GetParam().myGames;
  EXPECT_EQ(m_plugin->documentsDirectory().absolutePath().toStdString(),
            documents.toStdString());
  EXPECT_TRUE(m_plugin->savesDirectory().absolutePath().startsWith(documents + "/"))
      << m_plugin->savesDirectory().absolutePath().toStdString();
}

TEST_P(SteamGames, PluginsTxtFromThePrefixOfTheGame)
{
  ASSERT_TRUE(write(m_user + "/AppData/Local/" + GetParam().localApp + "/plugins.txt",
                    "*FromThePrefix.esp\r\n"));
  m_plugin->detectGame();

  m_plugin->initializeProfile(QDir(m_profile), IPluginGame::MODS);

  EXPECT_EQ(read(m_profile + "/plugins.txt"), QByteArray("*FromThePrefix.esp\r\n"));
}

INSTANTIATE_TEST_SUITE_P(Linux, SteamGames, testing::ValuesIn(steamGames),
                         [](const testing::TestParamInfo<SteamGame>& info) {
                           return std::string(info.param.name);
                         });
