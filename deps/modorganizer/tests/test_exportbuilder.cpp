#include <gtest/gtest.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>

#include "modlists/exportbuilder.h"

using namespace modlists;

namespace
{

PathMarkers markers()
{
  PathMarkers m;
  m.instance = "/home/u/.local/share/ModOrganizer/Skyrim";
  m.game     = "/home/u/Games/Skyrim";
  m.home     = "/home/u";
  return m;
}

Header base()
{
  Header h;
  h.gameName    = "SkyrimSE";
  h.nexusDomain = "skyrimspecialedition";
  return h;
}

ExportOptions options()
{
  ExportOptions o;
  o.listName = "L";
  o.author   = "me";
  o.version  = "1.0";
  return o;
}

ExportFile nexusFile(int modId, int fileId)
{
  ExportFile f;
  f.modId  = modId;
  f.fileId = fileId;
  f.key    = QString("%1_%2").arg(modId).arg(fileId);
  f.md5    = "0123456789abcdef0123456789abcdef";
  f.size   = 100;
  return f;
}

ExportMod nexusMod(const QString& name, int modId, int fileId, bool enabled = true)
{
  ExportMod m;
  m.name    = name;
  m.version = "1.0";
  m.enabled = enabled;
  m.files   = {nexusFile(modId, fileId)};
  return m;
}

ExportResult build(const std::vector<ExportMod>& mods,
                   const ExportOptions& o                    = options(),
                   const std::map<QString, QByteArray>& inis = {},
                   const std::vector<ExecutableEntry>& exes  = {},
                   const QString& overwriteDir               = "/nonexistent")
{
  return buildExport(mods, {}, inis, exes, ProfileConfig{}, markers(), overwriteDir, o,
                     base());
}

bool hasWarning(const ExportResult& r, ExportWarning::Kind kind, const QString& mod)
{
  return std::any_of(r.warnings.begin(), r.warnings.end(), [&](const auto& w) {
    return w.kind == kind && w.mod == mod;
  });
}

void writeFile(const QString& path, const QByteArray& data)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.write(data);
}

}  // namespace

TEST(ExportBuilder, OnlyActiveByDefault)
{
  const std::vector<ExportMod> mods = {nexusMod("Active", 1, 10),
                                       nexusMod("Inactive", 2, 20, false)};

  auto r = build(mods);
  ASSERT_EQ(r.package.plan.mods.size(), 1u);
  EXPECT_EQ(r.package.plan.mods[0].name, "Active");
  EXPECT_EQ(r.nexusCount, 1);

  auto o            = options();
  o.includeInactive = true;
  r                 = build(mods, o);
  ASSERT_EQ(r.package.plan.mods.size(), 2u);
  EXPECT_FALSE(r.package.plan.mods[0].optional);
  EXPECT_TRUE(r.package.plan.mods[0].enabled);
  EXPECT_EQ(r.package.plan.mods[1].name, "Inactive");
  EXPECT_TRUE(r.package.plan.mods[1].optional);
  EXPECT_FALSE(r.package.plan.mods[1].enabled);

  // the header comes from the base and the options
  EXPECT_EQ(r.package.plan.header.name, "L");
  EXPECT_EQ(r.package.plan.header.author, "me");
  EXPECT_EQ(r.package.plan.header.version, "1.0");
  EXPECT_EQ(r.package.plan.header.gameName, "SkyrimSE");
  EXPECT_EQ(r.package.plan.header.nexusDomain, "skyrimspecialedition");
  EXPECT_EQ(r.package.plan.header.origin, "mo2list");
}

TEST(ExportBuilder, SeparatorsAlwaysIncluded)
{
  ExportMod sep;
  sep.name        = "Visuals";
  sep.isSeparator = true;
  sep.enabled     = false;

  const auto r = build({sep, nexusMod("A", 1, 10)});
  ASSERT_EQ(r.package.plan.mods.size(), 2u);
  EXPECT_TRUE(r.package.plan.mods[0].isSeparator);
  EXPECT_EQ(r.package.plan.mods[0].name, "Visuals");
  EXPECT_TRUE(r.package.plan.mods[0].sources.empty());
}

TEST(ExportBuilder, MultiFileOneEntry)
{
  ExportMod m = nexusMod("Multi", 2, 20);
  m.files     = {nexusFile(2, 20), nexusFile(1, 10)};

  auto r = build({m});
  ASSERT_EQ(r.package.plan.mods.size(), 1u);
  const auto& sources = r.package.plan.mods[0].sources;
  ASSERT_EQ(sources.size(), 2u);
  EXPECT_EQ(sources[0].kind, SourceKind::Nexus);
  EXPECT_EQ(sources[0].modId, 2);
  EXPECT_EQ(sources[0].fileId, 20);
  EXPECT_EQ(sources[0].md5, "0123456789abcdef0123456789abcdef");
  EXPECT_EQ(sources[0].size, 100);
  EXPECT_EQ(sources[1].modId, 1);
  EXPECT_EQ(sources[1].fileId, 10);
  EXPECT_FALSE(hasWarning(r, ExportWarning::Kind::NoInstallOrder, "Multi"));

  m.hasInstallOrder = false;
  r                 = build({m});
  EXPECT_TRUE(hasWarning(r, ExportWarning::Kind::NoInstallOrder, "Multi"));
}

TEST(ExportBuilder, NoMd5Warning)
{
  ExportMod m    = nexusMod("A", 1, 10);
  m.files[0].md5 = "";
  const auto r   = build({m});
  EXPECT_TRUE(hasWarning(r, ExportWarning::Kind::NoMd5, "A"));
}

TEST(ExportBuilder, NonNexusChoices)
{
  ExportMod manual;
  manual.name           = "Manual";
  manual.nonNexus       = NonNexusChoice::Manual;
  manual.manualFileName = "manual.7z";
  manual.manualMd5      = "ffffffffffffffffffffffffffffffff";

  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  writeFile(dir.filePath("meta.ini"), "[General]\nmodid=0\n");
  writeFile(dir.filePath("textures/a.dds"), "dds");
  writeFile(dir.filePath("b.esp"), "esp");
  writeFile(dir.filePath("debug.log"), "log");

  ExportMod bundled;
  bundled.name         = "Bundled / Mod";
  bundled.nonNexus     = NonNexusChoice::Bundled;
  bundled.absolutePath = dir.path();

  ExportMod excluded;
  excluded.name     = "Excluded";
  excluded.nonNexus = NonNexusChoice::Exclude;

  const auto r = build({manual, bundled, excluded});

  EXPECT_TRUE(hasWarning(r, ExportWarning::Kind::ManualWithoutUrl, "Manual"));
  EXPECT_TRUE(hasWarning(r, ExportWarning::Kind::Excluded, "Excluded"));
  EXPECT_EQ(r.manualCount, 1);
  EXPECT_EQ(r.bundledCount, 1);
  EXPECT_EQ(r.excludedCount, 1);
  EXPECT_EQ(r.nexusCount, 0);

  ASSERT_EQ(r.package.plan.mods.size(), 2u);
  const auto& m0 = r.package.plan.mods[0];
  ASSERT_EQ(m0.sources.size(), 1u);
  EXPECT_EQ(m0.sources[0].kind, SourceKind::Manual);
  EXPECT_EQ(m0.sources[0].fileName, "manual.7z");
  EXPECT_EQ(m0.sources[0].md5, "ffffffffffffffffffffffffffffffff");

  const auto& m1 = r.package.plan.mods[1];
  EXPECT_EQ(m1.name, "Bundled / Mod");
  ASSERT_EQ(m1.sources.size(), 1u);
  EXPECT_EQ(m1.sources[0].kind, SourceKind::Bundled);
  const QString bdir = bundledDirName("Bundled / Mod");
  EXPECT_EQ(m1.sources[0].bundledDir, bdir);

  std::vector<QString> names;
  for (const auto& [inZip, disk] : r.package.bundledFiles) {
    names.push_back(inZip);
    EXPECT_TRUE(QFile::exists(disk)) << disk.toStdString();
  }
  std::sort(names.begin(), names.end());
  EXPECT_EQ(names, (std::vector<QString>{bdir + "/b.esp", bdir + "/textures/a.dds"}));

  // a manual mod with a url gives no warning
  manual.manualUrl = "https://example.com/mod";
  EXPECT_FALSE(
      hasWarning(build({manual}), ExportWarning::Kind::ManualWithoutUrl, "Manual"));
}

TEST(ExportBuilder, OverwriteUncheckedByDefault)
{
  QTemporaryDir overwrite;
  ASSERT_TRUE(overwrite.isValid());
  writeFile(overwrite.filePath("SKSE/x.ini"), "x");
  writeFile(overwrite.filePath("SKSE/skse64.log"), "log");

  auto r = build({nexusMod("A", 1, 10)}, options(), {}, {}, overwrite.path());
  for (const auto& m : r.package.plan.mods) {
    EXPECT_NE(m.name, "[L] Overwrite");
  }
  EXPECT_TRUE(r.package.bundledFiles.empty());

  auto o           = options();
  o.overwriteFiles = {"SKSE/x.ini", "SKSE/skse64.log"};
  r                = build({nexusMod("A", 1, 10)}, o, {}, {}, overwrite.path());
  ASSERT_EQ(r.package.plan.mods.size(), 2u);
  const auto& last = r.package.plan.mods.back();
  EXPECT_EQ(last.name, "[L] Overwrite");
  ASSERT_EQ(last.sources.size(), 1u);
  EXPECT_EQ(last.sources[0].kind, SourceKind::Bundled);
  EXPECT_EQ(last.sources[0].bundledDir, bundledDirName("[L] Overwrite"));

  ASSERT_EQ(r.package.bundledFiles.size(), 1u);
  EXPECT_EQ(r.package.bundledFiles[0].first,
            bundledDirName("[L] Overwrite") + "/SKSE/x.ini");
  EXPECT_EQ(QFileInfo(r.package.bundledFiles[0].second).canonicalFilePath(),
            QFileInfo(overwrite.filePath("SKSE/x.ini")).canonicalFilePath());
}

TEST(ExportBuilder, Sanitized)
{
  const std::map<QString, QByteArray> inis = {
      {"Skyrim.ini", "[General]\nsPath=/home/u/Games/Skyrim/Data\n"},
      {"SkyrimPrefs.ini", "[Display]\nx=Z:\\home\\u\\Games\\Skyrim\\y\n"},
      {"ModOrganizer.ini", "[General]\napi=secret\n"},
      {"saves/x.ess", "save"},
      {"Saves/a.txt", "save"},
      {"x.skse", "skse"},
  };

  ExecutableEntry exe;
  exe.title            = "SKSE";
  exe.binary           = "/home/u/Games/Skyrim/skse64_loader.exe";
  exe.workingDirectory = "/home/u/Games/Skyrim";
  exe.arguments        = "-x \"/home/u/Documents/a.txt\"";
  exe.environment      = {"A=/home/u/.local/share/ModOrganizer/Skyrim/mods"};

  const auto r = build({}, options(), inis, {exe});

  QStringList keys;
  for (const auto& [name, data] : r.package.profileFiles) {
    keys.append(name);
  }
  EXPECT_EQ(keys, (QStringList{"Skyrim.ini", "SkyrimPrefs.ini"}));
  EXPECT_EQ(r.package.profileFiles.at("Skyrim.ini"),
            QByteArray("[General]\nsPath=%GAME%/Data\n"));
  EXPECT_EQ(r.package.profileFiles.at("SkyrimPrefs.ini"),
            QByteArray("[Display]\nx=Z:%GAME%\\y\n"));
  EXPECT_EQ(r.package.plan.profile.iniFiles,
            (QStringList{"Skyrim.ini", "SkyrimPrefs.ini"}));

  ASSERT_EQ(r.package.plan.executables.size(), 1u);
  const auto& e = r.package.plan.executables[0];
  EXPECT_EQ(e.title, "SKSE");
  EXPECT_EQ(e.binary, "%GAME%/skse64_loader.exe");
  EXPECT_EQ(e.workingDirectory, "%GAME%");
  EXPECT_EQ(e.arguments, "-x \"%HOME%/Documents/a.txt\"");
  EXPECT_EQ(e.environment, (QStringList{"A=%INSTANCE%/mods"}));
}

TEST(ExportBuilder, SanitizedKeepsNonUtf8Bytes)
{
  // an INI in Windows-1252 keeps its bytes; only the paths change
  QByteArray ini("[General]\nsName=Caf\xe9\nsPath=/home/u/Games/Skyrim\n");
  const auto r = build({}, options(), {{"Skyrim.ini", ini}});
  EXPECT_EQ(r.package.profileFiles.at("Skyrim.ini"),
            QByteArray("[General]\nsName=Caf\xe9\nsPath=%GAME%\n"));
}

TEST(ExportBuilder, ExcludedNames)
{
  EXPECT_TRUE(isExcludedFromPackage("ModOrganizer.ini"));
  EXPECT_TRUE(isExcludedFromPackage("sub/modorganizer.INI"));
  EXPECT_TRUE(isExcludedFromPackage("a.ESS"));
  EXPECT_TRUE(isExcludedFromPackage("SKSE/a.skse"));
  EXPECT_TRUE(isExcludedFromPackage("SKSE/skse64.log"));
  EXPECT_TRUE(isExcludedFromPackage("saves/readme.txt"));
  EXPECT_TRUE(isExcludedFromPackage("x\\Saves\\a.txt"));
  EXPECT_FALSE(isExcludedFromPackage("Skyrim.ini"));
  EXPECT_FALSE(isExcludedFromPackage("savesmod/a.txt"));
  EXPECT_FALSE(isExcludedFromPackage("textures/saves.dds"));
}

TEST(ExportBuilder, FomodWarnings)
{
  ExportMod m                    = nexusMod("Fomod", 1, 10);
  m.files                        = {nexusFile(1, 10), nexusFile(1, 11)};
  m.files[0].fomodWithoutChoices = true;
  const QJsonArray choices{QJsonObject{{"name", "Main"}, {"groups", QJsonArray{}}}};
  m.files[1].fomodChoices = choices;

  const auto r = build({m});
  EXPECT_TRUE(hasWarning(r, ExportWarning::Kind::NoFomodChoices, "Fomod"));
  ASSERT_EQ(r.package.plan.mods.size(), 1u);
  const auto& sources = r.package.plan.mods[0].sources;
  ASSERT_EQ(sources.size(), 2u);
  EXPECT_FALSE(sources[0].fomodChoices.has_value());
  ASSERT_TRUE(sources[1].fomodChoices.has_value());
  EXPECT_EQ(*sources[1].fomodChoices, choices);
}

TEST(ExportBuilder, EmptyPathsBundleNothing)
{
  // an empty path must not be read as the current directory
  ExportMod bundled;
  bundled.name     = "B";
  bundled.nonNexus = NonNexusChoice::Bundled;

  auto o           = options();
  o.overwriteFiles = {"CMakeCache.txt"};
  const auto r     = build({bundled}, o, {}, {}, QString());
  EXPECT_TRUE(r.package.bundledFiles.empty());
  EXPECT_TRUE(r.package.plan.mods.empty());
  EXPECT_TRUE(hasWarning(r, ExportWarning::Kind::EmptyBundled, "B"));
}

TEST(ExportBuilder, EmptyBundledIsExcluded)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  writeFile(dir.filePath("meta.ini"), "[General]\n");
  writeFile(dir.filePath("x.log"), "log");

  ExportMod bundled;
  bundled.name         = "B";
  bundled.nonNexus     = NonNexusChoice::Bundled;
  bundled.absolutePath = dir.path();

  const auto r = build({bundled});
  EXPECT_TRUE(r.package.plan.mods.empty());
  EXPECT_TRUE(hasWarning(r, ExportWarning::Kind::EmptyBundled, "B"));
  EXPECT_EQ(r.bundledCount, 0);
  EXPECT_EQ(r.excludedCount, 1);
}

TEST(ExportBuilder, NexusModNotBundled)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  writeFile(dir.filePath("a.esp"), "esp");

  ExportMod bundled;
  bundled.name         = "From Nexus";
  bundled.nexusId      = 1234;
  bundled.nonNexus     = NonNexusChoice::Bundled;
  bundled.absolutePath = dir.path();

  const auto r = build({bundled});
  EXPECT_TRUE(r.package.plan.mods.empty());
  EXPECT_TRUE(r.package.bundledFiles.empty());
  EXPECT_TRUE(hasWarning(r, ExportWarning::Kind::NexusModNotBundled, "From Nexus"));
  EXPECT_EQ(r.bundledCount, 0);
  EXPECT_EQ(r.excludedCount, 1);
}

TEST(ExportBuilder, ManualWithoutMd5)
{
  ExportMod manual;
  manual.name      = "Manual";
  manual.manualUrl = "https://example.com/mod";
  const auto r     = build({manual});
  EXPECT_TRUE(hasWarning(r, ExportWarning::Kind::NoMd5, "Manual"));
}

TEST(ExportBuilder, MixedSources)
{
  ExportMod m = nexusMod("Mixed", 1, 10);
  ExportFile local;
  local.key = "archive_abcdefghijkl";
  m.files.push_back(local);

  const auto r = build({m});
  ASSERT_EQ(r.package.plan.mods.size(), 1u);
  ASSERT_EQ(r.package.plan.mods[0].sources.size(), 1u);
  EXPECT_EQ(r.package.plan.mods[0].sources[0].kind, SourceKind::Nexus);
  EXPECT_TRUE(hasWarning(r, ExportWarning::Kind::MixedSources, "Mixed"));
  EXPECT_FALSE(hasWarning(build({nexusMod("A", 1, 10)}),
                          ExportWarning::Kind::MixedSources, "A"));
}

TEST(ExportBuilder, SecretEnvDropped)
{
  ExecutableEntry exe;
  exe.title       = "Tool";
  exe.binary      = "/home/u/Games/Skyrim/tool.exe";
  exe.environment = {"NEXUS_API_KEY=abc", "my_token=1",    "DB_PASSWORD=x",
                     "OAuthThing=y",      "SteamCookie=z", "SESSIONID=s",
                     "Secret=q",          "DXVK_HUD=fps"};

  const auto r = build({}, options(), {}, {exe});
  ASSERT_EQ(r.package.plan.executables.size(), 1u);
  EXPECT_EQ(r.package.plan.executables[0].environment, (QStringList{"DXVK_HUD=fps"}));
  EXPECT_TRUE(hasWarning(r, ExportWarning::Kind::SecretEnvDropped, "Tool"));

  exe.environment = {"DXVK_HUD=fps"};
  EXPECT_FALSE(hasWarning(build({}, options(), {}, {exe}),
                          ExportWarning::Kind::SecretEnvDropped, "Tool"));
}

TEST(ExportBuilder, OverwriteStaysInside)
{
  QTemporaryDir outside;
  QTemporaryDir overwrite;
  ASSERT_TRUE(outside.isValid() && overwrite.isValid());
  writeFile(outside.filePath("x.ini"), "secret");
  writeFile(overwrite.filePath("SKSE/x.ini"), "x");
  ASSERT_TRUE(QFile::link(outside.path(), overwrite.filePath("linked")));
  ASSERT_TRUE(QFile::link(outside.filePath("x.ini"), overwrite.filePath("file.ini")));

  auto o           = options();
  o.overwriteFiles = {"linked/x.ini",
                      "file.ini",
                      "../" + QFileInfo(outside.path()).fileName() + "/x.ini",
                      "SKSE/../../" + QFileInfo(outside.path()).fileName() + "/x.ini",
                      outside.filePath("x.ini"),
                      "SKSE/x.ini"};
  const auto r     = build({}, o, {}, {}, overwrite.path());

  ASSERT_EQ(r.package.bundledFiles.size(), 1u);
  EXPECT_EQ(r.package.bundledFiles[0].first,
            bundledDirName("[L] Overwrite") + "/SKSE/x.ini");
}

TEST(ExportBuilder, BundledSkipsLinks)
{
  QTemporaryDir outside;
  QTemporaryDir dir;
  ASSERT_TRUE(outside.isValid() && dir.isValid());
  writeFile(outside.filePath("secret.txt"), "secret");
  writeFile(dir.filePath("a.esp"), "esp");
  ASSERT_TRUE(QFile::link(outside.filePath("secret.txt"), dir.filePath("link.txt")));
  ASSERT_TRUE(QFile::link(outside.path(), dir.filePath("linkeddir")));

  ExportMod bundled;
  bundled.name         = "B";
  bundled.nonNexus     = NonNexusChoice::Bundled;
  bundled.absolutePath = dir.path();

  const auto r = build({bundled});
  ASSERT_EQ(r.package.bundledFiles.size(), 1u);
  EXPECT_EQ(r.package.bundledFiles[0].first, bundledDirName("B") + "/a.esp");
}

// the contract between the export and the import: what buildExport() puts in the
// package comes back from Mo2ListPackage, the bundled files included
TEST(ExportBuilder, RoundTripThroughThePackage)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  // a bundled mod whose name has path characters and accents
  const QString modDir = tmp.filePath("mods/Dar");
  writeFile(modDir + "/Dar.esp", "esp bytes");
  writeFile(modDir + "/textures/Câmara.dds", QByteArray("dds\0bytes", 9));
  writeFile(modDir + "/meta.ini", "[General]\n");
  ExportMod bundled;
  bundled.name         = "Dar's Overhaul / Fix: Câmara";
  bundled.version      = "2.1";
  bundled.nonNexus     = NonNexusChoice::Bundled;
  bundled.absolutePath = modDir;

  ExportMod sep;
  sep.name        = "Visuals";
  sep.isSeparator = true;

  const QString overwrite = tmp.filePath("overwrite");
  writeFile(overwrite + "/SKSE/Plugins/x.ini", "[x]\n");
  writeFile(overwrite + "/left.txt", "not checked");

  auto o           = options();
  o.overwriteFiles = {"SKSE/Plugins/x.ini"};
  o.readme         = "Read me";

  const std::map<QString, QByteArray> inis = {
      {"Skyrim.ini", "[General]\nsPath=/home/u/Games/Skyrim/Data\n"}};
  const auto r =
      build({sep, nexusMod("Alpha", 1, 2), bundled}, o, inis, {}, overwrite);

  const QString path = tmp.filePath("out.mo2list");
  writeMo2List(path, r.package);
  const auto package = Mo2ListPackage::open(path);

  // the same plan, the same profile files and README
  EXPECT_EQ(toJson(package.plan()), toJson(r.package.plan));
  EXPECT_EQ(package.readme(), "Read me");
  ASSERT_EQ(r.package.profileFiles.count("Skyrim.ini"), 1u);
  EXPECT_EQ(package.profileFile("Skyrim.ini"), r.package.profileFiles.at("Skyrim.ini"));
  EXPECT_TRUE(package.profileFile("Skyrim.ini").contains("%GAME%/Data"));

  // every bundled source extracts to the files of the mod
  std::map<QString, QByteArray> extracted;
  int bundledSources = 0;
  for (const auto& entry : package.plan().mods) {
    for (const auto& source : entry.sources) {
      if (source.kind != SourceKind::Bundled) {
        continue;
      }
      ++bundledSources;
      EXPECT_EQ(source.bundledDir, bundledDirName(entry.name));
      const QString dest = tmp.filePath("extracted/" + QString::number(bundledSources));
      package.extractBundled(source.bundledDir, dest);
      QDirIterator it(dest, QDir::Files, QDirIterator::Subdirectories);
      while (it.hasNext()) {
        const QString file = it.next();
        QFile f(file);
        ASSERT_TRUE(f.open(QIODevice::ReadOnly));
        extracted[entry.name + ":" + QDir(dest).relativeFilePath(file)] = f.readAll();
      }
    }
  }
  EXPECT_EQ(bundledSources, 2);

  const std::map<QString, QByteArray> expected = {
      {"Dar's Overhaul / Fix: Câmara:Dar.esp", "esp bytes"},
      {"Dar's Overhaul / Fix: Câmara:textures/Câmara.dds", QByteArray("dds\0bytes", 9)},
      {"[L] Overwrite:SKSE/Plugins/x.ini", "[x]\n"}};
  EXPECT_EQ(extracted, expected);
}
