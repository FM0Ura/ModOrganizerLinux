#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>

#include "modlists/exportreview.h"

using namespace modlists;

namespace
{

ExportFile file(int modId, int fileId, const QString& archive)
{
  ExportFile f;
  f.modId       = modId;
  f.fileId      = fileId;
  f.archivePath = archive;
  return f;
}

}  // namespace

TEST(ExportReview, WarningTextNamesTheMod)
{
  using Kind = ExportWarning::Kind;
  for (const auto kind :
       {Kind::NoFomodChoices, Kind::NoInstallOrder, Kind::NoMd5, Kind::Excluded,
        Kind::ManualWithoutUrl, Kind::SecretEnvDropped, Kind::MixedSources,
        Kind::EmptyBundled, Kind::NexusModNotBundled}) {
    const QString text = warningText({kind, "Some Mod"});
    EXPECT_TRUE(text.contains("Some Mod")) << int(kind);
    EXPECT_GT(text.size(), QString("Some Mod").size() + 10) << int(kind);
  }
}

TEST(ExportReview, CommandLineAndReport)
{
  ExecutableEntry e;
  e.title       = "SKSE";
  e.binary      = "%GAME%/skse64 loader.exe";
  e.arguments   = "-token=abc123";
  e.environment = {"DXVK_HUD=fps"};
  EXPECT_EQ(commandLine(e), "\"%GAME%/skse64 loader.exe\" -token=abc123");

  e.binary = "%GAME%/skse64_loader.exe";
  EXPECT_EQ(commandLine(e), "%GAME%/skse64_loader.exe -token=abc123");

  // every exported executable shows its arguments and environment in full
  const QString report = executablesReport({e});
  EXPECT_TRUE(report.contains("SKSE"));
  EXPECT_TRUE(report.contains("%GAME%/skse64_loader.exe -token=abc123"));
  EXPECT_TRUE(report.contains("DXVK_HUD=fps"));
  EXPECT_FALSE(executablesReport({}).isEmpty());
}

TEST(ExportReview, LeakUserName)
{
  EXPECT_EQ(leakUserName("alice", "/home/bob"), "alice");
  EXPECT_EQ(leakUserName("", "/home/bob"), "bob");
}

TEST(ExportReview, FindsLeaksInInisExecutablesAndReadme)
{
  PackageInput in;
  in.profileFiles["Skyrim.ini"] = "[General]\nsLanguage=ENGLISH\nsPath=/home/alice/x\n";
  in.profileFiles["SkyrimPrefs.ini"] = "[Display]\niSize W=1920\n";
  ExecutableEntry e;
  e.title            = "Tool";
  e.binary           = "%GAME%/tool.exe";
  e.arguments        = "--user alice";
  e.workingDirectory = "%GAME%";
  e.environment      = {"DXVK_HUD=fps", "CACHE=/home/alice/cache"};
  in.plan.executables.push_back(e);
  in.readme = "made by me\n";

  const auto leaks = findPackageLeaks(in, "alice", "/home/alice");
  ASSERT_EQ(leaks.size(), 3u);

  bool ini = false, args = false, env = false;
  for (const auto& l : leaks) {
    if (l.file == "profile/Skyrim.ini") {
      ini = l.line == 3 && l.excerpt.contains("/home/alice");
    } else if (l.file.contains("Tool") && l.file.contains("arguments")) {
      args = l.line == 1;
    } else if (l.file.contains("Tool") && l.file.contains("environment")) {
      env = l.line == 2;
    }
  }
  EXPECT_TRUE(ini);
  EXPECT_TRUE(args);
  EXPECT_TRUE(env);

  in.readme = "ask alice\n";
  EXPECT_EQ(findPackageLeaks(in, "alice", "/home/alice").size(), 4u);
}

TEST(ExportReview, PendingMd5OnlyForArchivesPresent)
{
  ExportMod nexus;
  nexus.name  = "Nexus";
  nexus.files = {file(1, 2, "/d/a.7z"), file(1, 3, "")};

  ExportMod manual;
  manual.name  = "Manual";
  manual.files = {file(0, 0, "/d/m.zip")};

  ExportMod gone;
  gone.name  = "Gone";
  gone.files = {file(0, 0, "")};

  const auto out = withPendingMd5({nexus, manual, gone});
  EXPECT_FALSE(out[0].files[0].md5.isEmpty());
  EXPECT_TRUE(out[0].files[1].md5.isEmpty());
  EXPECT_FALSE(out[1].manualMd5.isEmpty());
  EXPECT_TRUE(out[2].manualMd5.isEmpty());
}

TEST(ExportReview, IsExportedAndNexusFiles)
{
  ExportMod m;
  m.enabled = false;
  ExportOptions o;
  EXPECT_FALSE(isExported(m, o));
  o.includeInactive = true;
  EXPECT_TRUE(isExported(m, o));
  m.isSeparator = true;
  EXPECT_FALSE(isExported(m, o));

  m.files = {file(0, 0, "")};
  EXPECT_FALSE(hasNexusFiles(m));
  m.files.push_back(file(5, 6, ""));
  EXPECT_TRUE(hasNexusFiles(m));
}

TEST(ExportReview, PackageSize)
{
  QTemporaryDir tmp;
  const QString disk = tmp.filePath("f");
  {
    QFile f(disk);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(1000, 'x'));
  }

  PackageInput in;
  const qint64 empty = packageSize(in);
  EXPECT_GT(empty, 0);  // the manifest

  in.profileFiles["Skyrim.ini"] = QByteArray(100, 'a');
  in.readme                     = QString(10, 'r');
  in.bundledFiles.push_back({"x/f", disk});
  EXPECT_EQ(packageSize(in), empty + 1110);
}
