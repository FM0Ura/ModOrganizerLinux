// GameGamebryo on Linux, the profile INIs: file names found ignoring case (the Proton
// prefix has Skyrim.ini, iniFiles() names skyrim.ini), the INI of the local saves, and
// the repair of a profile when it is opened.

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <uibase/iprofile.h>

#include "gamebryolocalsavegames.h"
#include "gamegamebryo.h"

#include <memory>
#include <ostream>
#include <utility>
#include <vector>

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

bool write(const QString& path, const QByteArray& content)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

QByteArray read(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<missing>");
}

// a game with its documents (myGamesPath()) in the given folder; records the calls of
// initializeProfile()
class FakeGame : public GameGamebryo
{
public:
  explicit FakeGame(const QString& myGames) { m_MyGamesPath = myGames; }

  using GameGamebryo::repairProfileInis;

  std::vector<std::pair<QString, ProfileSettings>> initialized;

  void initializeProfile(const QDir& directory, ProfileSettings settings) const override
  {
    const_cast<FakeGame*>(this)->initialized.emplace_back(directory.absolutePath(),
                                                          settings);
  }

  QString name() const override { return "Fake"; }
  QString author() const override { return {}; }
  QString description() const override { return {}; }
  VersionInfo version() const override { return {}; }
  QList<PluginSetting> settings() const override { return {}; }
  QString gameName() const override { return "Fake"; }
  QList<ExecutableInfo> executables() const override { return {}; }
  QList<ExecutableForcedLoadSetting> executableForcedLoads() const override
  {
    return {};
  }
  QString gameShortName() const override { return "Fake"; }
  QStringList iniFiles() const override { return {"fake.ini"}; }
  int nexusGameID() const override { return 0; }

protected:
  QString savegameExtension() const override { return "ess"; }
  QString savegameSEExtension() const override { return "skse"; }
  std::shared_ptr<const GamebryoSaveGame> makeSaveGame(QString) const override
  {
    return nullptr;
  }
};

class FakeProfile : public IProfile
{
public:
  FakeProfile(QString path, bool localSettings, bool localSaves)
      : m_path(std::move(path)), m_localSettings(localSettings),
        m_localSaves(localSaves)
  {}

  QString name() const override { return "Default"; }
  QString absolutePath() const override { return m_path; }
  bool localSavesEnabled() const override { return m_localSaves; }
  bool localSettingsEnabled() const override { return m_localSettings; }
  bool invalidationActive(bool* supported) const override
  {
    if (supported != nullptr) {
      *supported = false;
    }
    return false;
  }
  QString absoluteIniFilePath(QString iniFile) const override
  {
    return m_path + "/" + iniFile;
  }

private:
  QString m_path;
  bool m_localSettings;
  bool m_localSaves;
};

// GamebryoLocalSavegames::prepareProfile() is public in LocalSavegames
class LocalSaves : public testing::Test
{
protected:
  void SetUp() override
  {
    ASSERT_TRUE(m_tmp.isValid());
    m_myGames = m_tmp.filePath("My Games/Skyrim Special Edition");
    m_profile = m_tmp.filePath("profiles/Default");
    ASSERT_TRUE(QDir().mkpath(m_myGames));
    ASSERT_TRUE(QDir().mkpath(m_profile));
  }

  QTemporaryDir m_tmp;
  QString m_myGames;
  QString m_profile;
};

}  // namespace

TEST(ResolveIgnoringCase, FindsTheFileWithAnotherCase)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ASSERT_TRUE(write(tmp.filePath("Skyrim.ini"), "x"));

  EXPECT_EQ(GameGamebryo::resolveIgnoringCase(tmp.path() + "/skyrim.ini"),
            tmp.path() + "/Skyrim.ini");
  EXPECT_EQ(GameGamebryo::resolveIgnoringCase(tmp.path() + "/SKYRIM.INI"),
            tmp.path() + "/Skyrim.ini");
}

TEST(ResolveIgnoringCase, PrefersTheExactName)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ASSERT_TRUE(write(tmp.filePath("Skyrim.ini"), "x"));
  ASSERT_TRUE(write(tmp.filePath("skyrim.ini"), "y"));

  EXPECT_EQ(GameGamebryo::resolveIgnoringCase(tmp.path() + "/skyrim.ini"),
            tmp.path() + "/skyrim.ini");
  EXPECT_EQ(GameGamebryo::resolveIgnoringCase(tmp.path() + "/Skyrim.ini"),
            tmp.path() + "/Skyrim.ini");
}

TEST(ResolveIgnoringCase, KeepsTheNameWhenThereIsNoFile)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  EXPECT_EQ(GameGamebryo::resolveIgnoringCase(tmp.path() + "/skyrim.ini"),
            tmp.path() + "/skyrim.ini");
  EXPECT_EQ(GameGamebryo::resolveIgnoringCase(tmp.path() + "/missing/skyrim.ini"),
            tmp.path() + "/missing/skyrim.ini");
}

// Skyrim SE registers the local saves with "Skyrimcustom.ini", the profile has
// skyrimcustom.ini (iniFiles(), the file that the INI Bakery maps)
TEST_F(LocalSaves, UseTheProfileIniWithAnotherCase)
{
  ASSERT_TRUE(
      write(m_profile + "/skyrimcustom.ini", "[Display]\r\nbFull Screen=0\r\n"));
  FakeGame game(m_myGames);
  FakeProfile profile(m_profile, true, true);
  GamebryoLocalSavegames saves(&game, "Skyrimcustom.ini");

  EXPECT_TRUE(saves.prepareProfile(&profile));

  const QByteArray ini = read(m_profile + "/skyrimcustom.ini");
  EXPECT_TRUE(ini.contains("bFull Screen=0")) << ini.toStdString();
  EXPECT_TRUE(ini.contains("sLocalSavePath=__MO_Saves\\")) << ini.toStdString();
  EXPECT_FALSE(QFile::exists(m_profile + "/Skyrimcustom.ini"));
}

TEST_F(LocalSaves, UseThePrefixIniWithAnotherCase)
{
  ASSERT_TRUE(
      write(m_myGames + "/SkyrimCustom.ini", "[Display]\r\nbFull Screen=0\r\n"));
  FakeGame game(m_myGames);
  FakeProfile profile(m_profile, false, true);
  GamebryoLocalSavegames saves(&game, "Skyrimcustom.ini");

  EXPECT_TRUE(saves.prepareProfile(&profile));

  const QByteArray ini = read(m_myGames + "/SkyrimCustom.ini");
  EXPECT_TRUE(ini.contains("sLocalSavePath=__MO_Saves\\")) << ini.toStdString();
  EXPECT_FALSE(QFile::exists(m_myGames + "/Skyrimcustom.ini"));
}

// no INI with that name in any case: the name of the feature, as before
TEST_F(LocalSaves, CreateTheIniWhenThereIsNone)
{
  FakeGame game(m_myGames);
  FakeProfile profile(m_profile, true, true);
  GamebryoLocalSavegames saves(&game, "Skyrimcustom.ini");

  EXPECT_TRUE(saves.prepareProfile(&profile));

  const QByteArray ini = read(m_profile + "/Skyrimcustom.ini");
  EXPECT_TRUE(ini.contains("sLocalSavePath=__MO_Saves\\")) << ini.toStdString();
}

// when a profile is opened (and before a run), the configuration of a profile with
// profile-specific INIs is initialized again, which restores its empty INIs
TEST(RepairProfileInis, InitializesTheConfigurationOfAProfileWithLocalInis)
{
  FakeGame game("/nowhere");
  FakeProfile profile("/profiles/Default", true, false);

  game.repairProfileInis(&profile);

  ASSERT_EQ(game.initialized.size(), 1u);
  EXPECT_EQ(game.initialized[0].first, "/profiles/Default");
  EXPECT_EQ(game.initialized[0].second,
            IPluginGame::ProfileSettings(IPluginGame::CONFIGURATION));
}

TEST(RepairProfileInis, IgnoresAProfileWithoutLocalInis)
{
  FakeGame game("/nowhere");
  FakeProfile profile("/profiles/Default", false, false);

  game.repairProfileInis(&profile);
  game.repairProfileInis(nullptr);

  EXPECT_TRUE(game.initialized.empty());
}
