#include "../src/linux/selfupdate_linux.h"
#include "tempdir.h"

#include <gtest/gtest.h>

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
QJsonObject fixtureRelease()
{
  QFile f(QStringLiteral(MO2_TESTS_FIXTURES "/selfupdate/release.json"));
  if (!f.open(QIODevice::ReadOnly)) {
    ADD_FAILURE() << "can't open the fixture " << f.fileName().toStdString();
    return {};
  }
  return QJsonDocument::fromJson(f.readAll()).object();
}

void writeFile(const fs::path& p, const std::string& contents)
{
  std::ofstream out(p, std::ios::binary);
  out << contents;
}

std::string readFile(const fs::path& p)
{
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

QByteArray sha256(const std::string& contents)
{
  return QCryptographicHash::hash(QByteArray::fromStdString(contents),
                                  QCryptographicHash::Sha256)
      .toHex();
}

QString q(const fs::path& p)
{
  return QString::fromStdString(p.string());
}

fs::perms permsOf(const fs::path& p)
{
  return fs::status(p).permissions();
}
}  // namespace

TEST(SelfUpdateLinux, PicksAppImageAsset)
{
  const auto a = selfupdate::appImageAsset(fixtureRelease());

  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->name, "ModOrganizer-2.5.2.2-x86_64.AppImage");
  EXPECT_EQ(a->url, QUrl("https://github.com/FM0Ura/ModOrganizerLinux/releases/download/"
                         "v2.5.2.2/ModOrganizer-2.5.2.2-x86_64.AppImage"));
}

TEST(SelfUpdateLinux, IgnoresZsyncAndDebug)
{
  // only the .zsync and the debug symbols left: no AppImage
  QJsonObject release = fixtureRelease();
  QJsonArray assets;
  for (const auto& v : release["assets"].toArray()) {
    const auto name = v.toObject()["name"].toString();
    if (name.endsWith(".zsync") || name.endsWith(".debug.tar.zst")) {
      assets.append(v);
    }
  }
  ASSERT_EQ(assets.size(), 2);
  release["assets"] = assets;

  EXPECT_FALSE(selfupdate::appImageAsset(release).has_value());
}

TEST(SelfUpdateLinux, PicksChecksumAsset)
{
  const auto a = selfupdate::checksumAsset(fixtureRelease());

  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->name, "SHA256SUMS");
  EXPECT_EQ(a->url, QUrl("https://github.com/FM0Ura/ModOrganizerLinux/releases/download/"
                         "v2.5.2.2/SHA256SUMS"));
}

TEST(SelfUpdateLinux, MissingChecksumRefusesUpdate)
{
  QJsonObject release = fixtureRelease();
  QJsonArray assets;
  for (const auto& v : release["assets"].toArray()) {
    if (v.toObject()["name"].toString() != "SHA256SUMS") {
      assets.append(v);
    }
  }
  release["assets"] = assets;

  EXPECT_FALSE(selfupdate::checksumAsset(release).has_value());
  EXPECT_TRUE(selfupdate::appImageAsset(release).has_value());
}

TEST(SelfUpdateLinux, ExpectedShaFromSums)
{
  const QByteArray a(64, 'a'), b(64, 'b');
  const QByteArray c = "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef";
  const QByteArray sums =
      a + "  ModOrganizer-2.5.2.2-x86_64.AppImage.zsync\n" + b +
      "  ModOrganizer-2.5.2.2-x86_64.AppImage\n" + c +
      " *ModOrganizer-2.5.2.2-x86_64.debug.tar.zst\n";

  EXPECT_EQ(selfupdate::expectedSha256(sums, "ModOrganizer-2.5.2.2-x86_64.AppImage"),
            std::optional<QByteArray>(b));
  EXPECT_EQ(
      selfupdate::expectedSha256(sums, "ModOrganizer-2.5.2.2-x86_64.debug.tar.zst"),
      std::optional<QByteArray>(c.toLower()));
  EXPECT_EQ(selfupdate::expectedSha256(sums, "ModOrganizer-2.5.2.3-x86_64.AppImage"),
            std::nullopt);
  EXPECT_EQ(selfupdate::expectedSha256("not a sum  ModOrganizer.AppImage\n",
                                       "ModOrganizer.AppImage"),
            std::nullopt);
}

TEST(SelfUpdateLinux, DownloadPathIsNextToTarget)
{
  EXPECT_EQ(selfupdate::downloadPathFor("/home/u/Apps/MO2.AppImage"),
            QString("/home/u/Apps/.MO2.AppImage.update-%1").arg(::getpid()));
}

TEST(ReplaceAppImage, ReplacesAtomicallyKeepingMode)
{
  TempDir dir;
  const auto target = dir.path() / "ModOrganizer.AppImage";
  writeFile(target, "old");
  fs::permissions(target, fs::perms(0755));
  const auto downloaded = fs::path(selfupdate::downloadPathFor(q(target)).toStdString());
  writeFile(downloaded, "new version");
  fs::permissions(downloaded, fs::perms(0600));

  EXPECT_EQ(selfupdate::replaceAppImage(q(downloaded), q(target), sha256("new version")),
            selfupdate::ReplaceError::None);

  EXPECT_EQ(readFile(target), "new version");
  EXPECT_EQ(permsOf(target), fs::perms(0755));
  EXPECT_FALSE(fs::exists(downloaded));
}

TEST(ReplaceAppImage, ChecksumMismatchKeepsOld)
{
  TempDir dir;
  const auto target = dir.path() / "ModOrganizer.AppImage";
  writeFile(target, "old");
  fs::permissions(target, fs::perms(0755));
  const auto downloaded = fs::path(selfupdate::downloadPathFor(q(target)).toStdString());
  writeFile(downloaded, "half a downlo");

  EXPECT_EQ(selfupdate::replaceAppImage(q(downloaded), q(target), sha256("new version")),
            selfupdate::ReplaceError::ChecksumMismatch);

  EXPECT_EQ(readFile(target), "old");
  EXPECT_EQ(permsOf(target), fs::perms(0755));
  EXPECT_FALSE(fs::exists(downloaded));
}

TEST(ReplaceAppImage, ReadOnlyDirectoryKeepsOld)
{
  if (::geteuid() == 0) {
    GTEST_SKIP() << "root can write to a read-only directory";
  }

  TempDir dir;
  const auto apps   = dir.path() / "apps";
  const auto target = apps / "ModOrganizer.AppImage";
  fs::create_directories(apps);
  writeFile(target, "old");
  fs::permissions(target, fs::perms(0755));
  const auto downloaded = fs::path(selfupdate::downloadPathFor(q(target)).toStdString());
  writeFile(downloaded, "new version");
  fs::permissions(apps, fs::perms(0555));

  EXPECT_EQ(selfupdate::replaceAppImage(q(downloaded), q(target), sha256("new version")),
            selfupdate::ReplaceError::NotWritable);

  fs::permissions(apps, fs::perms(0755));
  EXPECT_EQ(readFile(target), "old");
  EXPECT_EQ(permsOf(target), fs::perms(0755));
}

TEST(ReplaceAppImage, DownloadOutsideTheDirectoryIsRefused)
{
  TempDir dir;
  fs::create_directories(dir.path() / "a");
  fs::create_directories(dir.path() / "b");
  const auto target     = dir.path() / "a" / "ModOrganizer.AppImage";
  const auto downloaded = dir.path() / "b" / ".ModOrganizer.AppImage.update";
  writeFile(target, "old");
  writeFile(downloaded, "new version");

  EXPECT_EQ(selfupdate::replaceAppImage(q(downloaded), q(target), sha256("new version")),
            selfupdate::ReplaceError::IoError);

  EXPECT_EQ(readFile(target), "old");
  EXPECT_FALSE(fs::exists(downloaded));
}

TEST(ReplaceAppImage, PathWithSpacesAndAccents)
{
  TempDir dir;
  const auto apps = dir.path() / "Meus Programas";
  fs::create_directories(apps);
  const QString target = q(apps) + QString::fromUtf8("/Mod Organizer ç.AppImage");
  const auto targetPath = fs::path(target.toStdString());
  writeFile(targetPath, "old");
  fs::permissions(targetPath, fs::perms(0750));
  const QString downloaded = selfupdate::downloadPathFor(target);
  EXPECT_TRUE(downloaded.endsWith(QString::fromUtf8("/.Mod Organizer ç.AppImage.update-") +
                                  QString::number(::getpid())));
  writeFile(fs::path(downloaded.toStdString()), "nova versão");

  EXPECT_EQ(selfupdate::replaceAppImage(downloaded, target, sha256("nova versão")),
            selfupdate::ReplaceError::None);

  EXPECT_EQ(readFile(targetPath), "nova versão");
  EXPECT_EQ(permsOf(targetPath), fs::perms(0750));
  EXPECT_FALSE(fs::exists(fs::path(downloaded.toStdString())));
}

TEST(Reexec, NothingScheduled)
{
  EXPECT_FALSE(selfupdate::runPendingReexec());
}

TEST(Reexec, ExecutesTheScheduledProgramWithItsArguments)
{
  TempDir dir;
  const auto script = dir.path() / "New Organizer.AppImage";
  const auto out    = dir.path() / "args.txt";
  writeFile(script, "#!/bin/sh\nprintf '%s\\n' \"$0\" \"$@\" > \"" + out.string() +
                        "\"\n");
  fs::permissions(script, fs::perms(0755));

  const pid_t pid = ::fork();
  ASSERT_NE(pid, -1);
  if (pid == 0) {
    selfupdate::scheduleReexec(q(script),
                               {"-i", "My Instance", QString::fromUtf8("çã"), "$HOME"});
    selfupdate::runPendingReexec();
    ::_exit(42);
  }

  int status = 0;
  ASSERT_EQ(::waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  EXPECT_EQ(readFile(out),
            script.string() + "\n-i\nMy Instance\n" + std::string("çã") + "\n$HOME\n");
}

TEST(Reexec, ReturnsWhenExecFails)
{
  TempDir dir;

  const pid_t pid = ::fork();
  ASSERT_NE(pid, -1);
  if (pid == 0) {
    selfupdate::scheduleReexec(q(dir.path() / "missing.AppImage"), {});
    ::_exit(selfupdate::runPendingReexec() ? 3 : 4);
  }

  int status = 0;
  ASSERT_EQ(::waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 3);
}

TEST(UpdateMode, NoCheckOutsideAnAppImage)
{
  EXPECT_EQ(selfupdate::updateMode(""), selfupdate::UpdateMode::None);
}

TEST(UpdateMode, InstallsInAWritableFolder)
{
  TempDir dir;
  const auto target = dir.path() / "ModOrganizer.AppImage";
  writeFile(target, "old");

  EXPECT_EQ(selfupdate::updateMode(q(target)), selfupdate::UpdateMode::Install);
}

TEST(UpdateMode, OnlyNotifiesInAReadOnlyFolder)
{
  if (::geteuid() == 0) {
    GTEST_SKIP() << "root can write to a read-only directory";
  }

  TempDir dir;
  const auto apps   = dir.path() / "apps";
  const auto target = apps / "ModOrganizer.AppImage";
  fs::create_directories(apps);
  writeFile(target, "old");
  fs::permissions(apps, fs::perms(0555));

  EXPECT_EQ(selfupdate::updateMode(q(target)), selfupdate::UpdateMode::NotifyOnly);

  fs::permissions(apps, fs::perms(0755));
}
