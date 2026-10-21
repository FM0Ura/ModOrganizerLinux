// GameGamebryo on Linux, the profile INIs: file names found ignoring case (the Proton
// prefix has Skyrim.ini, iniFiles() names skyrim.ini), the INI of the local saves, and
// the repair of a profile when it is opened, the archive lists and the BSA
// invalidation.

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <uibase/iprofile.h>

#include "gamebryobsainvalidation.h"
#include "gamebryodataarchives.h"
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

// the archive lists of a game with one INI, like Skyrim (skyrimdataarchives.cpp):
// fake.ini, in the profile or in the documents of the game
class FakeArchives : public GamebryoDataArchives
{
public:
  using GamebryoDataArchives::GamebryoDataArchives;

  QStringList vanillaArchives() const override { return {}; }

  QStringList archives(const IProfile* profile) const override
  {
    return getArchivesFromKey(iniFile(profile), "SResourceArchiveList");
  }

private:
  QString iniFile(const IProfile* profile) const
  {
    return profile->localSettingsEnabled()
               ? QDir(profile->absolutePath()).absoluteFilePath("fake.ini")
               : localGameDirectory().absoluteFilePath("fake.ini");
  }

  void writeArchiveList(IProfile* profile, const QStringList& before) override
  {
    setArchivesToKey(iniFile(profile), "SResourceArchiveList", before.join(", "));
  }
};

class FakeInvalidation : public GamebryoBSAInvalidation
{
public:
  using GamebryoBSAInvalidation::GamebryoBSAInvalidation;

private:
  QString invalidationBSAName() const override { return "Fake - Invalidation.bsa"; }
  unsigned long bsaVersion() const override { return 0x68; }
};

// the documents of the game and a profile, as for the local saves
class IniCase : public LocalSaves
{};

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

// the game reads sLocalSavePath (__MO_Saves\) as a Windows path: the saves of the
// profile are mapped to the __MO_Saves folder of the documents of the game
TEST_F(LocalSaves, MapTheSavesToTheFolderTheGameUses)
{
  FakeGame game(m_myGames);
  GamebryoLocalSavegames saves(&game, "Skyrimcustom.ini");

  const auto mappings = saves.mappings(QDir(m_profile + "/saves"));

  ASSERT_EQ(mappings.size(), 1u);
  EXPECT_EQ(mappings[0].source, m_profile + "/saves");
  EXPECT_EQ(mappings[0].destination, m_myGames + "/__MO_Saves");
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

// the Proton prefix has Fake.ini, the game reads fake.ini: the archive list is read
// from and written to the file that exists
TEST_F(IniCase, ArchivesUseThePrefixIniWithAnotherCase)
{
  ASSERT_TRUE(write(m_myGames + "/Fake.ini",
                    "[Archive]\r\nSResourceArchiveList=A.bsa, B.bsa\r\n"));
  FakeGame game(m_myGames);
  FakeProfile profile(m_profile, false, false);
  FakeArchives archives(&game);

  EXPECT_EQ(archives.archives(&profile), (QStringList{"A.bsa", "B.bsa"}));

  archives.addArchive(&profile, 0, "C.bsa");

  const QByteArray ini = read(m_myGames + "/Fake.ini");
  EXPECT_TRUE(ini.contains("SResourceArchiveList=C.bsa, A.bsa, B.bsa"))
      << ini.toStdString();
  EXPECT_FALSE(QFile::exists(m_myGames + "/fake.ini"));
}

TEST_F(IniCase, ArchivesUseTheProfileIniWithAnotherCase)
{
  ASSERT_TRUE(
      write(m_profile + "/FAKE.ini", "[Archive]\r\nSResourceArchiveList=A.bsa\r\n"));
  FakeGame game(m_myGames);
  FakeProfile profile(m_profile, true, false);
  FakeArchives archives(&game);

  archives.removeArchive(&profile, "A.bsa");
  archives.addArchive(&profile, 0, "B.bsa");

  const QByteArray ini = read(m_profile + "/FAKE.ini");
  EXPECT_TRUE(ini.contains("SResourceArchiveList=B.bsa")) << ini.toStdString();
  EXPECT_FALSE(ini.contains("A.bsa")) << ini.toStdString();
  EXPECT_FALSE(QFile::exists(m_profile + "/fake.ini"));
}

// no INI in any case (a new profile): created with the name asked for, as before
TEST_F(IniCase, ArchivesCreateTheIniWithTheNameAskedFor)
{
  FakeGame game(m_myGames);
  FakeProfile profile(m_profile, true, false);
  FakeArchives archives(&game);

  archives.addArchive(&profile, 0, "C.bsa");

  const QByteArray ini = read(m_profile + "/fake.ini");
  EXPECT_TRUE(ini.contains("SResourceArchiveList=C.bsa")) << ini.toStdString();
}

// Fallout 4 names Fallout4Custom.ini, the profile has fallout4custom.ini (iniFiles()):
// the invalidation writes to the file of the profile, without a second one
TEST_F(IniCase, BsaInvalidationUsesTheProfileIniWithAnotherCase)
{
  ASSERT_TRUE(write(m_profile + "/fakecustom.ini", "[Display]\r\nbFull Screen=0\r\n"));
  FakeGame game(m_myGames);
  FakeProfile profile(m_profile, true, false);  // invalidation not active
  FakeArchives archives(&game);
  FakeInvalidation invalidation(&archives, "FakeCustom.ini", &game);

  EXPECT_TRUE(invalidation.prepareProfile(&profile));

  const QByteArray ini = read(m_profile + "/fakecustom.ini");
  EXPECT_TRUE(ini.contains("bFull Screen=0")) << ini.toStdString();
  EXPECT_TRUE(ini.contains("bInvalidateOlderFiles=1")) << ini.toStdString();
  EXPECT_TRUE(ini.contains("SInvalidationFile=ArchiveInvalidation.txt"))
      << ini.toStdString();
  EXPECT_FALSE(QFile::exists(m_profile + "/FakeCustom.ini"));
}
