#include <gtest/gtest.h>

#include "modlists/importreview.h"

using namespace modlists;

namespace
{

Source nexus(int modId, int fileId, qint64 size = -1)
{
  Source s;
  s.kind   = SourceKind::Nexus;
  s.modId  = modId;
  s.fileId = fileId;
  s.size   = size;
  return s;
}

ModEntry entry(const QString& name, std::vector<Source> sources, bool optional = false)
{
  ModEntry e;
  e.name     = name;
  e.sources  = std::move(sources);
  e.optional = optional;
  return e;
}

ModEntry separator(const QString& name)
{
  ModEntry e;
  e.name        = name;
  e.isSeparator = true;
  return e;
}

Plan samplePlan()
{
  Plan p;
  p.header.name = "List";
  Source manual;
  manual.kind = SourceKind::Manual;
  manual.url  = "https://example.com/file";
  Source bundled;
  bundled.kind       = SourceKind::Bundled;
  bundled.bundledDir = "Mine-0000";
  p.mods             = {
      separator("Graphics"),                          // 0
      entry("Alpha", {nexus(10, 100), nexus(10, 101)}),  // 1: two files
      entry("Beta", {nexus(20, 200)}),                  // 2: installed (same file)
      entry("Gamma", {nexus(30, 300)}),                 // 3: other version
      entry("Delta", {nexus(40, 400)}, true),           // 4: optional
      entry("Manual", {manual}),                        // 5
      entry("Bundled", {bundled}),                      // 6
  };
  return p;
}

std::vector<InstalledMod> sampleInstalled()
{
  return {{"Beta", 20, {200}, "1.0"}, {"Gamma mine", 30, {299}, "0.9"}};
}

}  // namespace

TEST(ImportReview, DefaultSelectionLeavesOptionalOut)
{
  EXPECT_EQ(defaultSelection(samplePlan()), (std::vector<int>{0, 1, 2, 3, 5, 6}));
}

TEST(ImportReview, ReusableByMetaIds)
{
  const Plan p = samplePlan();
  std::vector<DownloadCandidate> c(2);
  c[0].path   = "/d/a.7z";
  c[0].modId  = 10;
  c[0].fileId = 101;
  c[1].path   = "/d/other.7z";
  c[1].modId  = 10;
  c[1].fileId = 999;
  EXPECT_EQ(reusableSources(p, c), (std::set<SourceRef>{{1, 1}}));
}

TEST(ImportReview, ClicksExcludeReusableAndKept)
{
  const Plan p      = samplePlan();
  const auto status = classifyAll(p, sampleInstalled(), Destination::ExistingProfile);
  ASSERT_EQ(status[2], RowStatus::AlreadyInstalled);
  ASSERT_EQ(status[3], RowStatus::DifferentVersion);

  const auto selected = defaultSelection(p);
  // Alpha (2 files) + Gamma (list's version); Beta is installed, manual and bundled
  // are not on the Nexus, Delta is not selected
  EXPECT_EQ(nexusClicks(p, status, Destination::ExistingProfile, selected, {}, {}), 3);
  // one file of Alpha is in the downloads folder
  EXPECT_EQ(
      nexusClicks(p, status, Destination::ExistingProfile, selected, {}, {{1, 1}}), 2);
  // the user keeps their Gamma
  EXPECT_EQ(nexusClicks(p, status, Destination::ExistingProfile, selected,
                        {{3, VersionChoice::KeepMine}}, {{1, 1}}),
            1);
  // the optional one, selected twice, counts once
  EXPECT_EQ(nexusClicks(p, status, Destination::ExistingProfile, {4, 4}, {}, {}), 1);
}

TEST(ImportReview, ClicksInNewInstanceCountInstalledMods)
{
  // nothing is installed in a new instance: Beta is downloaded unless reusable
  const Plan p      = samplePlan();
  const auto status = classifyAll(p, sampleInstalled(), Destination::NewInstance);
  const auto sel    = defaultSelection(p);
  EXPECT_EQ(nexusClicks(p, status, Destination::NewInstance, sel, {}, {}), 4);
  EXPECT_EQ(nexusClicks(p, status, Destination::NewInstance, sel, {}, {{2, 0}}), 3);
}

TEST(ImportReview, Footer)
{
  EXPECT_EQ(downloadsFooter(true, 12), QString("Premium account ✔"));
  EXPECT_EQ(downloadsFooter(false, 12),
            QString("No Premium: you will click 12 downloads on Nexus"));
}

TEST(ImportReview, StatusTexts)
{
  const Plan p   = samplePlan();
  const auto ins = sampleInstalled();
  EXPECT_EQ(statusText(p.mods[0], RowStatus::New, {}), QString("Separator"));
  EXPECT_EQ(statusText(p.mods[2], RowStatus::AlreadyInstalled, {}),
            QString("✔ Already installed"));
  EXPECT_EQ(userVersion(p.mods[3], ins), QString("0.9"));
  EXPECT_EQ(statusText(p.mods[3], RowStatus::DifferentVersion, "0.9"),
            QString("⚠ You have 0.9"));
  EXPECT_EQ(statusText(p.mods[5], RowStatus::Manual, {}), QString("Download manually"));
  EXPECT_EQ(statusText(p.mods[6], RowStatus::Bundled, {}),
            QString("Included in the list"));

  ModEntry fomod = p.mods[1];
  fomod.sources[0].fomodChoices = QJsonArray();
  EXPECT_EQ(statusText(fomod, RowStatus::New, {}),
            QString("New (FOMOD with the author's choices)"));
  EXPECT_EQ(statusText(p.mods[1], RowStatus::New, {}), QString("New"));
}

TEST(ImportReview, EntrySize)
{
  EXPECT_EQ(entrySize(entry("x", {nexus(1, 1, 100), nexus(1, 2, 50)})), 150);
  EXPECT_EQ(entrySize(entry("x", {nexus(1, 1), nexus(1, 2, 50)})), 50);
  EXPECT_EQ(entrySize(entry("x", {nexus(1, 1)})), -1);
}

TEST(ImportReview, EnvironmentSplit)
{
  const auto split = splitImportedEnvironment(
      {"DXVK_HUD=1", "LD_PRELOAD=/tmp/evil.so", "WINEDLLOVERRIDES=d3d11=n,b",
       "DXVK_CONFIG_FILE=x", "PROTON_LOG=1\nLD_PRELOAD=y", "=nothing"});
  EXPECT_EQ(split.kept, (QStringList{"DXVK_HUD=1", "WINEDLLOVERRIDES=d3d11=n,b"}));
  EXPECT_EQ(split.dropped, (QStringList{"LD_PRELOAD", "DXVK_CONFIG_FILE", "PROTON_LOG"}));
}

TEST(ImportReview, ExecutableContentListsEverything)
{
  ExecutableEntry exe;
  exe.title            = "xEdit";
  exe.binary           = "%INSTANCE%/mods/xEdit/SSEEdit.exe";
  exe.arguments        = "-quickautoclean";
  exe.workingDirectory = "%GAME%";
  exe.environment      = {"DXVK_HUD=1", "LD_PRELOAD=/x.so"};

  const std::vector<ExecFinding> findings = {
      {ExecFinding::Kind::NativeFile, "bundled/Mine-0000/a.dll", ".dll"}};

  EXPECT_FALSE(hasExecutableContent({}, {}));
  EXPECT_TRUE(hasExecutableContent(findings, {}));
  EXPECT_TRUE(hasExecutableContent({}, {exe}));

  const QString text = executableContentText(findings, {exe});
  EXPECT_TRUE(text.contains("bundled/Mine-0000/a.dll")) << text.toStdString();
  EXPECT_TRUE(text.contains("Binary: %INSTANCE%/mods/xEdit/SSEEdit.exe"));
  EXPECT_TRUE(text.contains("Arguments: -quickautoclean"));
  EXPECT_TRUE(text.contains("Working directory: %GAME%"));
  EXPECT_TRUE(text.contains("Environment kept: DXVK_HUD=1"));
  EXPECT_TRUE(text.contains("Environment left out: LD_PRELOAD"));
}

TEST(ImportReview, SameGameAndPath)
{
  Header h;
  h.gameName = "SkyrimSE";
  EXPECT_TRUE(sameGame(h, "skyrimse"));
  EXPECT_FALSE(sameGame(h, "Skyrim"));

  EXPECT_TRUE(isMo2ListPath("/home/u/My List.MO2LIST"));
  EXPECT_TRUE(isMo2ListPath("a.mo2list"));
  EXPECT_FALSE(isMo2ListPath(".mo2list"));
  EXPECT_FALSE(isMo2ListPath("/home/u/list.mo2list.zip"));
  EXPECT_FALSE(isMo2ListPath("nxm://skyrimspecialedition/mods/1/files/2"));
}
