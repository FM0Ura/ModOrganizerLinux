// Skyrim Special Edition on Linux: the plugin (loaded like Mod Organizer loads it, with
// QPluginLoader) finds the game in a Steam library of a temporary HOME and reads the
// information fields of its saves, and creates the profile INIs.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QTimeZone>

#include <uibase/iplugingame.h>
#include <uibase/isavegame.h>
#include <uibase/log.h>

#include <memory>
#include <ostream>

using namespace MOBase;

// readable QString values in the failure messages
QT_BEGIN_NAMESPACE
inline void PrintTo(const QString& s, std::ostream* os)
{
  *os << '"' << s.toStdString() << '"';
}
QT_END_NAMESPACE

namespace
{

bool touch(const QString& path)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly);
}

bool write(const QString& path, const QByteArray& content)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

// a HOME with the native Steam client and a second Steam library, listed in
// libraryfolders.vdf, that has Skyrim Special Edition
class SkyrimSE : public testing::Test
{
protected:
  void SetUp() override
  {
    ASSERT_TRUE(m_home.isValid());
    m_oldHome = qgetenv("HOME");
    qputenv("HOME", QFile::encodeName(m_home.path()));
    // the XDG folders (Documents/) of the temporary HOME, not those of the user
    m_oldConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_home.path() + "/.config"));

    const QDir home(m_home.path());
    const QString library = home.absoluteFilePath("Games/SteamLibrary");
    m_steam               = home.absoluteFilePath(".local/share/Steam");
    m_library             = library;
    m_game                = library + "/steamapps/common/Skyrim Special Edition";

    ASSERT_TRUE(
        write(home.absoluteFilePath(".local/share/Steam/steamapps/libraryfolders.vdf"),
              "\"libraryfolders\"\n{\n"
              "\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" +
                  QFile::encodeName(home.absoluteFilePath(".local/share/Steam")) +
                  "\"\n\t}\n"
                  "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" +
                  QFile::encodeName(library) +
                  "\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"489830\"\t\t\"0\"\n\t\t}\n"
                  "\t}\n}\n"));
    ASSERT_TRUE(write(library + "/steamapps/appmanifest_489830.acf",
                      "\"AppState\"\n{\n\t\"appid\"\t\t\"489830\"\n"
                      "\t\"installdir\"\t\t\"Skyrim Special Edition\"\n}\n"));
    ASSERT_TRUE(touch(m_game + "/SkyrimSE.exe"));
    ASSERT_TRUE(QDir().mkpath(m_game + "/Data"));

    m_loader          = std::make_unique<QPluginLoader>(SKYRIMSE_PLUGIN);
    QObject* instance = m_loader->instance();
    ASSERT_NE(instance, nullptr) << m_loader->errorString().toStdString();
    m_plugin = qobject_cast<IPluginGame*>(instance);
    ASSERT_NE(m_plugin, nullptr);
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
  QString m_steam;
  QString m_library;
  QString m_game;
  std::unique_ptr<QPluginLoader> m_loader;
  IPluginGame* m_plugin = nullptr;
};

}  // namespace

TEST_F(SkyrimSE, DetectsSteamInstall)
{
  m_plugin->detectGame();

  EXPECT_TRUE(m_plugin->isInstalled());
  EXPECT_EQ(m_plugin->gameDirectory().absolutePath(), QDir(m_game).absolutePath());
}

TEST_F(SkyrimSE, DataDirectory)
{
  m_plugin->detectGame();

  EXPECT_EQ(m_plugin->dataDirectory().absolutePath(),
            QDir(m_game + "/Data").absolutePath());
}

// the documents of the game are in the Proton prefix of the game, in the library that
// has the game or in the one of the Steam client
TEST_F(SkyrimSE, DocumentsInTheProtonPrefix)
{
  const QString user =
      m_library + "/steamapps/compatdata/489830/pfx/drive_c/users/steamuser";
  ASSERT_TRUE(QDir().mkpath(user));

  m_plugin->detectGame();

  const QString documents = user + "/Documents/My Games/Skyrim Special Edition";
  EXPECT_EQ(m_plugin->documentsDirectory().absolutePath(), documents);
  EXPECT_EQ(m_plugin->savesDirectory().absolutePath(), documents + "/Saves");
}

TEST_F(SkyrimSE, DocumentsInTheProtonPrefixOfTheSteamClient)
{
  const QString user =
      m_steam + "/steamapps/compatdata/489830/pfx/drive_c/users/steamuser";
  ASSERT_TRUE(QDir().mkpath(user));

  m_plugin->detectGame();

  EXPECT_EQ(m_plugin->documentsDirectory().absolutePath(),
            user + "/Documents/My Games/Skyrim Special Edition");
}

// a compatdata without pfx/ is a game that was never run with Proton
TEST_F(SkyrimSE, DocumentsWithoutPrefixAreTheXdgOnes)
{
  ASSERT_TRUE(QDir().mkpath(m_library + "/steamapps/compatdata/489830"));
  const QString xdg = m_home.path() + "/Documents/My Games/Skyrim Special Edition";
  ASSERT_TRUE(QDir().mkpath(xdg));

  m_plugin->detectGame();

  EXPECT_EQ(m_plugin->documentsDirectory().absolutePath(), xdg);
}

// the information fields at the start of a Skyrim SE save (.ess), as the plugin reads
// them: 32-bit integers (the long of Windows), strings with a 16-bit length
QByteArray skyrimSESave(const QDateTime& creationTime)
{
  QByteArray data;
  QDataStream s(&data, QIODevice::WriteOnly);
  s.setByteOrder(QDataStream::LittleEndian);
  s.setFloatingPointPrecision(QDataStream::SinglePrecision);

  auto string = [&](const QByteArray& str) {
    s << static_cast<quint16>(str.size());
    s.writeRawData(str.constData(), static_cast<int>(str.size()));
  };

  s.writeRawData("TESV_SAVEGAME", 13);
  s << quint32(0);   // header size
  s << quint32(12);  // version
  s << quint32(7);   // save number
  string("Dovahkiin");
  s << quint32(42);  // level
  string("Whiterun");
  string("12.30.00");  // time of day
  string("NordRace");
  s << quint16(0);                    // gender
  s << float(10.5f) << float(20.0f);  // experience

  // FILETIME, 100 ns since 1601-01-01 UTC; the plugin subtracts 6 hours from it
  const QDateTime epoch(QDate(1601, 1, 1), QTime(0, 0), QTimeZone::UTC);
  const quint64 ticks =
      static_cast<quint64>(epoch.msecsTo(creationTime.addSecs(6 * 3600))) * 10000;
  s << ticks;

  return data;
}

TEST_F(SkyrimSE, ReadsSaveInformation)
{
  const QDateTime created(QDate(2024, 1, 2), QTime(3, 4, 5), QTimeZone::UTC);
  const QString saves = m_home.path() + "/Saves";
  ASSERT_TRUE(write(saves + "/Save7_Dovahkiin.ess", skyrimSESave(created)));

  const auto list = m_plugin->listSaves(QDir(saves));
  ASSERT_EQ(list.size(), 1u);

  EXPECT_EQ(list[0]->getName(), "Dovahkiin, #7, Level 42, Whiterun");
  EXPECT_EQ(list[0]->getSaveGroupIdentifier(), "Dovahkiin");
  EXPECT_EQ(list[0]->getCreationTime(), created);
}

// the profile INIs on Linux: the INIs of the Proton prefix (Skyrim.ini,
// SkyrimPrefs.ini) and the default INI of the game (Skyrim_Default.ini) do not have the
// lowercase names of iniFiles(); a profile INI that an earlier version created empty
// because of that is restored from them
class SkyrimSEProfileInis : public SkyrimSE
{
protected:
  void SetUp() override
  {
    SkyrimSE::SetUp();
    if (HasFatalFailure()) {
      return;
    }

    m_myGames = m_library + "/steamapps/compatdata/489830/pfx/drive_c/users/steamuser"
                            "/Documents/My Games/Skyrim Special Edition";
    ASSERT_TRUE(QDir().mkpath(m_myGames));
    m_profile = m_home.path() + "/profiles/Default";
    ASSERT_TRUE(QDir().mkpath(m_profile));

    m_plugin->detectGame();
    ASSERT_EQ(m_plugin->documentsDirectory().absolutePath(), m_myGames);
  }

  QByteArray read(const QString& name) const
  {
    QFile file(m_profile + "/" + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<missing>");
  }

  void initializeConfiguration()
  {
    m_plugin->initializeProfile(QDir(m_profile), IPluginGame::CONFIGURATION);
  }

  const QByteArray m_userIni    = "[General]\r\nsLanguage=ENGLISH\r\n";
  const QByteArray m_userPrefs  = "[Display]\r\niSize W=1920\r\niSize H=1080\r\n";
  const QByteArray m_defaultIni = "[General]\r\nsLanguage=DEFAULT\r\n";
  const QByteArray m_stub       = "[Launcher]\r\nbEnableFileSelection=1\r\n";

  QString m_myGames;
  QString m_profile;
};

TEST_F(SkyrimSEProfileInis, CopiesThePrefixInisIgnoringCase)
{
  ASSERT_TRUE(write(m_myGames + "/Skyrim.ini", m_userIni));
  ASSERT_TRUE(write(m_myGames + "/SkyrimPrefs.ini", m_userPrefs));
  // the user INI is chosen over the default one
  ASSERT_TRUE(write(m_game + "/Skyrim_Default.ini", m_defaultIni));

  initializeConfiguration();

  EXPECT_EQ(read("skyrim.ini"), m_userIni);
  EXPECT_EQ(read("skyrimprefs.ini"), m_userPrefs);
  // no SkyrimCustom.ini in the prefix: created empty, as before
  EXPECT_EQ(read("skyrimcustom.ini"), QByteArray());
}

TEST_F(SkyrimSEProfileInis, CopiesTheDefaultIniIgnoringCase)
{
  ASSERT_TRUE(write(m_game + "/Skyrim_Default.ini", m_defaultIni));

  initializeConfiguration();

  EXPECT_EQ(read("skyrim.ini"), m_defaultIni);
}

TEST_F(SkyrimSEProfileInis, RestoresAnEmptyProfileIni)
{
  ASSERT_TRUE(write(m_myGames + "/Skyrim.ini", m_userIni));
  ASSERT_TRUE(write(m_myGames + "/SkyrimPrefs.ini", m_userPrefs));
  ASSERT_TRUE(touch(m_profile + "/skyrim.ini"));
  ASSERT_TRUE(touch(m_profile + "/skyrimprefs.ini"));
  ASSERT_TRUE(touch(m_profile + "/skyrimcustom.ini"));

  initializeConfiguration();

  EXPECT_EQ(read("skyrim.ini"), m_userIni);
  EXPECT_EQ(read("skyrimprefs.ini"), m_userPrefs);
  // nothing to restore it from
  EXPECT_EQ(read("skyrimcustom.ini"), QByteArray());
}

// the profile INI that only has what prepareIni() writes before a run
TEST_F(SkyrimSEProfileInis, RestoresTheLauncherStubAndKeepsFileSelection)
{
  ASSERT_TRUE(write(m_myGames + "/Skyrim.ini", m_userIni));
  ASSERT_TRUE(write(m_profile + "/skyrim.ini", m_stub));

  initializeConfiguration();

  const QByteArray ini = read("skyrim.ini");
  EXPECT_TRUE(ini.startsWith(m_userIni)) << ini.toStdString();
  EXPECT_TRUE(ini.contains("[Launcher]")) << ini.toStdString();
  EXPECT_TRUE(ini.contains("bEnableFileSelection=1")) << ini.toStdString();
}

TEST_F(SkyrimSEProfileInis, RestoresTheLauncherStubWrittenDifferently)
{
  ASSERT_TRUE(write(m_myGames + "/SkyrimPrefs.ini", m_userPrefs));
  ASSERT_TRUE(write(m_profile + "/skyrimprefs.ini",
                    "\n  [Launcher] \n bEnableFileSelection = 1\n\n"));

  initializeConfiguration();

  const QByteArray ini = read("skyrimprefs.ini");
  EXPECT_TRUE(ini.startsWith(m_userPrefs)) << ini.toStdString();
  EXPECT_TRUE(ini.contains("bEnableFileSelection=1")) << ini.toStdString();
}

TEST_F(SkyrimSEProfileInis, KeepsAProfileIniWithContent)
{
  ASSERT_TRUE(write(m_myGames + "/Skyrim.ini", m_userIni));
  ASSERT_TRUE(write(m_myGames + "/SkyrimPrefs.ini", m_userPrefs));
  const QByteArray own         = "[Display]\r\nbFull Screen=0\r\n";
  const QByteArray stubAndMore = m_stub + "[General]\r\nsLanguage=FRENCH\r\n";
  ASSERT_TRUE(write(m_profile + "/skyrimprefs.ini", own));
  ASSERT_TRUE(write(m_profile + "/skyrim.ini", stubAndMore));

  initializeConfiguration();

  EXPECT_EQ(read("skyrimprefs.ini"), own);
  EXPECT_EQ(read("skyrim.ini"), stubAndMore);
}

TEST_F(SkyrimSEProfileInis, KeepsAnEmptyProfileIniWhenTheSourceIsEmpty)
{
  ASSERT_TRUE(touch(m_myGames + "/SkyrimPrefs.ini"));
  ASSERT_TRUE(write(m_profile + "/skyrimprefs.ini", m_stub));

  initializeConfiguration();

  EXPECT_EQ(read("skyrimprefs.ini"), m_stub);
}

// only INIs: an empty plugins.txt is a profile without plugins
TEST_F(SkyrimSEProfileInis, KeepsAnEmptyPluginsTxt)
{
  const QString local = m_library + "/steamapps/compatdata/489830/pfx/drive_c/users/"
                                    "steamuser/AppData/Local/Skyrim Special Edition";
  ASSERT_TRUE(write(local + "/plugins.txt", "*Unofficial Patch.esp\r\n"));
  ASSERT_TRUE(touch(m_profile + "/plugins.txt"));

  m_plugin->initializeProfile(QDir(m_profile), IPluginGame::MODS);

  EXPECT_EQ(read("plugins.txt"), QByteArray());
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);

  // the plugin logs warnings and errors (unreadable saves, ...)
  log::LoggerConfiguration conf;
  conf.name     = "game_bethesda_tests";
  conf.maxLevel = log::Debug;
  conf.pattern  = "%^[%Y-%m-%d %H:%M:%S.%e %L] %v%$";
  log::createDefault(conf);

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
