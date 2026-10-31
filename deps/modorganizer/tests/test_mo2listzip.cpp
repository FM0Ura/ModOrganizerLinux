#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <QtCore/private/qzipwriter_p.h>
#include <QDataStream>

#include "modlists/mo2listzip.h"

using namespace modlists;

namespace
{

Plan makePlan(const QString& modName = "Cool Mod")
{
  Plan p;
  p.header.name     = "My list";
  p.header.gameName = "SkyrimSE";
  ModEntry m;
  m.name = modName;
  Source b;
  b.kind       = SourceKind::Bundled;
  b.bundledDir = bundledDirName(modName);
  m.sources    = {b};
  p.mods.push_back(m);
  return p;
}

void writeFile(const QString& path, const QByteArray& data)
{
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.write(data);
}

QByteArray manifest(int schema = 1)
{
  auto json                = toJson(makePlan());
  json["schemaVersion"]    = schema;
  return QJsonDocument(json).toJson();
}

// bundled/<dir>/<file> from a plan with one executable and one INI
QString makePackage(const QTemporaryDir& tmp, const std::map<QString, QByteArray>& files,
                    const Plan& plan, const std::map<QString, QByteArray>& profile = {})
{
  PackageInput in;
  in.plan         = plan;
  in.profileFiles = profile;
  int i           = 0;
  for (const auto& [name, data] : files) {
    const QString disk = tmp.filePath(QString("src%1").arg(i++));
    writeFile(disk, data);
    in.bundledFiles.push_back({name, disk});
  }
  const QString out = tmp.filePath("out.mo2list");
  writeMo2List(out, in);
  return out;
}


// Hostile archives are assembled by hand (stored entries, local headers + central
// directory), because QZipWriter normalizes names. `declared` overrides the size in the
// headers (without the data), `madeBy`/`external` set the host system and attributes
// (a Unix symbolic link).
struct RawEntry
{
  QByteArray name, data;
  qint64 declared = -1;
};

QByteArray rawZip(const std::vector<RawEntry>& entries)
{
  auto crc32 = [](const QByteArray& d) {
    quint32 c = 0xffffffffu;
    for (const char ch : d) {
      c ^= static_cast<quint8>(ch);
      for (int k = 0; k < 8; ++k) {
        c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
      }
    }
    return ~c;
  };
  QByteArray out, central;
  QDataStream o(&out, QIODevice::WriteOnly), c(&central, QIODevice::WriteOnly);
  o.setByteOrder(QDataStream::LittleEndian);
  c.setByteOrder(QDataStream::LittleEndian);
  for (const auto& e : entries) {
    const quint32 offset = static_cast<quint32>(out.size());
    const quint32 crc    = crc32(e.data);
    const quint32 size   = static_cast<quint32>(e.declared >= 0 ? e.declared : e.data.size());
    o << quint32(0x04034b50) << quint16(20) << quint16(0) << quint16(0) << quint16(0)
      << quint16(0x21) << crc << quint32(e.data.size()) << quint32(e.data.size())
      << quint16(e.name.size()) << quint16(0);
    o.writeRawData(e.name.constData(), e.name.size());
    o.writeRawData(e.data.constData(), e.data.size());
    c << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0) << quint16(0)
      << quint16(0) << quint16(0x21) << crc << quint32(e.data.size()) << size
      << quint16(e.name.size()) << quint16(0) << quint16(0) << quint16(0) << quint16(0)
      << quint32(0) << offset;
    c.writeRawData(e.name.constData(), e.name.size());
  }
  const quint32 cdOffset = static_cast<quint32>(out.size());
  o.writeRawData(central.constData(), central.size());
  o << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(entries.size())
    << quint16(entries.size()) << quint32(central.size()) << cdOffset << quint16(0);
  return out;
}

QString rawPackage(const QTemporaryDir& tmp, std::vector<RawEntry> entries,
                   const QString& file = "raw.mo2list")
{
  entries.insert(entries.begin(), RawEntry{"manifest.json", manifest(), -1});
  const QString path = tmp.filePath(file);
  writeFile(path, rawZip(entries));
  return path;
}

}  // namespace

TEST(Mo2ListZip, WriteThenOpen)
{
  QTemporaryDir tmp;
  PackageInput in;
  in.plan                           = makePlan();
  in.profileFiles["Skyrim.ini"]     = "[General]\nsLanguage=ENGLISH\n";
  in.readme                         = "# Hello\n";
  const QString path                = tmp.filePath("a.mo2list");
  writeMo2List(path, in);

  EXPECT_FALSE(QFile::exists(path + ".part"));
  const auto pkg = Mo2ListPackage::open(path);
  EXPECT_EQ(QJsonDocument(toJson(pkg.plan())), QJsonDocument(toJson(in.plan)));
  EXPECT_EQ(pkg.profileFile("Skyrim.ini"), in.profileFiles["Skyrim.ini"]);
  EXPECT_EQ(pkg.readme(), in.readme);
  EXPECT_TRUE(pkg.profileFile("missing.ini").isEmpty());
}

TEST(Mo2ListZip, BundledNameRoundTrip)
{
  const QString name = "Dar's Overhaul / Fix";
  const QString dir  = bundledDirName(name);
  EXPECT_FALSE(dir.contains('/'));
  EXPECT_NE(bundledDirName("a/b"), bundledDirName("a:b"));

  QTemporaryDir tmp;
  const auto path = makePackage(tmp, {{dir + "/sub/a.txt", "hi"}}, makePlan(name));
  const auto pkg  = Mo2ListPackage::open(path);
  EXPECT_EQ(pkg.plan().mods[0].name, name);
  EXPECT_EQ(pkg.plan().mods[0].sources[0].bundledDir, dir);
  EXPECT_EQ(pkg.bundledEntries(dir), QStringList{"sub/a.txt"});

  const QString dest = tmp.filePath("dest");
  pkg.extractBundled(dir, dest);
  QFile f(dest + "/sub/a.txt");
  ASSERT_TRUE(f.open(QIODevice::ReadOnly));
  EXPECT_EQ(f.readAll(), QByteArray("hi"));
}

TEST(Mo2ListZip, RejectsTraversal)
{
  QTemporaryDir tmp;
  // names QZipReader keeps verbatim
  const QStringList bad = {"bundled/m/sub/../../x", "a\\..\\b", "\\abs", "C:/x", "C:\\x"};
  for (const auto& entry : bad) {
    const auto path = rawPackage(tmp, {{entry.toUtf8(), "x", -1}});
    try {
      Mo2ListPackage::open(path);
      ADD_FAILURE() << "accepted " << entry.toStdString();
    } catch (const PlanError& e) {
      EXPECT_EQ(e.kind(), PlanError::Kind::Invalid) << entry.toStdString();
      EXPECT_TRUE(QString(e.what()).contains("Unsafe entry") &&
                  QString(e.what()).contains(entry))
          << e.what();
    }
  }
}

TEST(Mo2ListZip, StrippedNamesStayInside)
{
  // QZipReader strips leading '.' and '/': "../evil" -> "evil", "/abs" -> "abs"
  QTemporaryDir tmp;
  const auto path = rawPackage(tmp, {{"../evil", "1", -1}, {"/abs", "2", -1}});
  const auto pkg  = Mo2ListPackage::open(path);
  EXPECT_TRUE(pkg.entries().contains("evil"));
  EXPECT_TRUE(pkg.entries().contains("abs"));
  EXPECT_FALSE(pkg.entries().contains("../evil"));

  const QString dest = tmp.filePath("sub/dest");
  pkg.extractBundled("m", dest);  // nothing under bundled/m/
  EXPECT_TRUE(QDir(dest).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
  EXPECT_FALSE(QFile::exists(tmp.filePath("sub/evil")));
  EXPECT_FALSE(QFile::exists(tmp.filePath("evil")));
  EXPECT_FALSE(QFile::exists("/abs"));
}

TEST(Mo2ListZip, RejectsSymlinkDuplicateAndLimits)
{
  QTemporaryDir tmp;

  const QString link = tmp.filePath("link.mo2list");
  {
    QZipWriter zip(link);
    zip.addFile("manifest.json", manifest());
    zip.addSymLink("bundled/x/l", "/etc/passwd");
  }
  try {
    Mo2ListPackage::open(link);
    ADD_FAILURE() << "accepted a symlink";
  } catch (const PlanError& e) {
    EXPECT_EQ(e.kind(), PlanError::Kind::Invalid);
    EXPECT_TRUE(QString(e.what()).contains("bundled/x/l"));
  }

  const auto none = tmp.filePath("none.mo2list");
  {
    QZipWriter zip(none);
    zip.addFile("README.md", "x");
  }
  EXPECT_THROW(Mo2ListPackage::open(none), PlanError);

  // a second manifest.json and a repeated name
  for (const QByteArray& name : {QByteArray("manifest.json"), QByteArray("bundled/m/a")}) {
    const auto path = rawPackage(tmp, {{"bundled/m/a", "1", -1}, {name, "2", -1}});
    try {
      Mo2ListPackage::open(path);
      ADD_FAILURE() << "accepted a duplicate " << name.toStdString();
    } catch (const PlanError& e) {
      EXPECT_EQ(e.kind(), PlanError::Kind::Invalid);
      EXPECT_TRUE(QString(e.what()).contains("Duplicate")) << e.what();
    }
  }

  // declared manifest.json size over 32 MiB
  const auto big = tmp.filePath("big.mo2list");
  writeFile(big, rawZip({{"manifest.json", manifest(), MaxManifestSize + 1}}));
  try {
    Mo2ListPackage::open(big);
    ADD_FAILURE() << "accepted a huge manifest";
  } catch (const PlanError& e) {
    EXPECT_EQ(e.kind(), PlanError::Kind::Invalid);
    EXPECT_TRUE(QString(e.what()).contains("32 MiB")) << e.what();
  }
}

TEST(Mo2ListZip, RejectsNewerSchema)
{
  QTemporaryDir tmp;
  const QString path = tmp.filePath("new.mo2list");
  {
    QZipWriter zip(path);
    zip.addFile("manifest.json", manifest(2));
  }
  try {
    Mo2ListPackage::open(path);
    FAIL() << "accepted schema 2";
  } catch (const PlanError& e) {
    EXPECT_EQ(e.kind(), PlanError::Kind::NewerSchema);
  }
}

TEST(Mo2ListZip, FindsExecutableContent)
{
  QTemporaryDir tmp;
  Plan plan = makePlan();
  ExecutableEntry e;
  e.title       = "SKSE";
  e.binary      = "%GAME%/skse64_loader.exe";
  e.arguments   = "-foo";
  e.environment = {"A=1"};
  plan.executables.push_back(e);

  const auto path = makePackage(
      tmp,
      {{"m/x.dll", "data"},
       {"m/noext", QByteArray("\x7f"
                              "ELF....")},
       {"m/a.esp", "TES4"},
       {"m/t.dds", "DDS "}},
      plan, {{"Skyrim.ini", "[Plugins]\nsDLL=foo.dll\nsOk=1\n"}});
  const auto found = findExecutableContent(Mo2ListPackage::open(path));

  int native = 0, exec = 0, ini = 0;
  for (const auto& f : found) {
    switch (f.kind) {
    case ExecFinding::Kind::NativeFile:
      ++native;
      break;
    case ExecFinding::Kind::Executable:
      ++exec;
      EXPECT_TRUE(f.detail.contains("skse64_loader.exe"));
      EXPECT_TRUE(f.detail.contains("-foo"));
      EXPECT_TRUE(f.detail.contains("A=1"));
      break;
    case ExecFinding::Kind::IniLoadsCode:
      ++ini;
      break;
    }
  }
  EXPECT_EQ(native, 2);
  EXPECT_EQ(exec, 1);
  EXPECT_EQ(ini, 1);

  QTemporaryDir tmp2;
  const auto clean = makePackage(tmp2, {{"m/a.esp", "TES4"}, {"m/t.dds", "DDS "}},
                                 makePlan());
  EXPECT_TRUE(findExecutableContent(Mo2ListPackage::open(clean)).empty());
}

TEST(Mo2ListZip, FindsMoreExecutableContent)
{
  QTemporaryDir tmp;
  const auto path = rawPackage(
      tmp,
      {{"bundled/m/a.asi", "x", -1},
       {"bundled/m/mz", "MZ..", -1},
       {"bundled/m/script", "#!/bin/sh\n", -1},
       {"bundled/m/huge.bin", "abcd", MaxScanSize + 1},
       {"bundled/m/huge.dds", "abcd", MaxScanSize + 1},
       {"bundled/m/plain.txt", "DATA", -1},
       {"bundled/m/disguised.dds", "MZ..", -1},
       {"bundled/m/real.dds", "DDS ", -1},
       {"profile/Skyrim.ini",
        "[A]\nk=%GAME%/x\nrun=tool.exe\nok=1\nsShader=foo.shader\nx=run.sh\ny=\"a.dll\" \n", -1}});
  const auto found = findExecutableContent(Mo2ListPackage::open(path));

  auto has = [&](ExecFinding::Kind kind, const QString& what, const QString& detail = {}) {
    for (const auto& f : found) {
      if (f.kind == kind && f.what == what && (detail.isEmpty() || f.detail == detail)) {
        return true;
      }
    }
    return false;
  };
  EXPECT_TRUE(has(ExecFinding::Kind::NativeFile, "bundled/m/a.asi"));
  EXPECT_TRUE(has(ExecFinding::Kind::NativeFile, "bundled/m/mz", "executable header"));
  EXPECT_TRUE(has(ExecFinding::Kind::NativeFile, "bundled/m/script", "executable header"));
  EXPECT_TRUE(has(ExecFinding::Kind::NativeFile, "bundled/m/huge.bin", "too large to scan"));
  EXPECT_FALSE(has(ExecFinding::Kind::NativeFile, "bundled/m/huge.dds"));
  EXPECT_FALSE(has(ExecFinding::Kind::NativeFile, "bundled/m/plain.txt"));
  EXPECT_TRUE(has(ExecFinding::Kind::NativeFile, "bundled/m/disguised.dds",
                  "executable header"));
  EXPECT_FALSE(has(ExecFinding::Kind::NativeFile, "bundled/m/real.dds"));
  EXPECT_FALSE(has(ExecFinding::Kind::IniLoadsCode, "profile/Skyrim.ini",
                   "sShader=foo.shader"));
  EXPECT_TRUE(has(ExecFinding::Kind::IniLoadsCode, "profile/Skyrim.ini", "x=run.sh"));
  EXPECT_TRUE(has(ExecFinding::Kind::IniLoadsCode, "profile/Skyrim.ini", "y=\"a.dll\""));
  EXPECT_TRUE(has(ExecFinding::Kind::IniLoadsCode, "profile/Skyrim.ini", "k=%GAME%/x"));
  EXPECT_TRUE(has(ExecFinding::Kind::IniLoadsCode, "profile/Skyrim.ini", "run=tool.exe"));
  EXPECT_FALSE(has(ExecFinding::Kind::IniLoadsCode, "profile/Skyrim.ini", "ok=1"));
}

TEST(Mo2ListZip, ExtractRefusesShortData)
{
  // declared size larger than the stored data: fileData cannot deliver it
  QTemporaryDir tmp;
  const auto path = rawPackage(tmp, {{"bundled/m/a.txt", "abc", 10}});
  const auto pkg  = Mo2ListPackage::open(path);
  const QString dest = tmp.filePath("dest");
  EXPECT_THROW(pkg.extractBundled("m", dest), std::runtime_error);
  EXPECT_FALSE(QFile::exists(dest + "/a.txt"));
  EXPECT_FALSE(QFile::exists(dest + "/a.txt.part"));
}

TEST(Mo2ListZip, WriteKeepsOldFileOnFailure)
{
  QTemporaryDir tmp;
  const QString path = tmp.filePath("a.mo2list");
  PackageInput in;
  in.plan = makePlan();
  writeMo2List(path, in);
  const auto before = [&] {
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
  }();

  in.bundledFiles.push_back({"m/x", tmp.filePath("missing")});
  EXPECT_THROW(writeMo2List(path, in), std::runtime_error);
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::ReadOnly));
  EXPECT_EQ(f.readAll(), before);
  EXPECT_FALSE(QFile::exists(path + ".part"));
}

TEST(Mo2ListZip, ExtractKeepsBothAAndAPart)
{
  QTemporaryDir tmp;
  const auto path = rawPackage(tmp, {{"bundled/m/a", "one", -1}, {"bundled/m/a.part", "two", -1}});
  const auto pkg  = Mo2ListPackage::open(path);
  const QString dest = tmp.filePath("dest");
  pkg.extractBundled("m", dest);
  auto read = [&](const QString& n) {
    QFile f(dest + "/" + n);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
  };
  EXPECT_EQ(read("a"), QByteArray("one"));
  EXPECT_EQ(read("a.part"), QByteArray("two"));
  EXPECT_EQ(QDir(dest).entryList(QDir::Files | QDir::Hidden).size(), 2);
}

namespace
{

// tests/fixtures/modlists/small.mo2list, see tests/fixtures/modlists/README.md
const QString FixturePath = QStringLiteral(MO2_TESTS_FIXTURES "/modlists/small.mo2list");

Source nexusSource(int modId, int fileId)
{
  Source s;
  s.kind   = SourceKind::Nexus;
  s.modId  = modId;
  s.fileId = fileId;
  // md5 and size unknown: the importer does not verify them
  return s;
}

ModEntry nexusMod(const QString& name, const QString& version, int modId, int fileId)
{
  ModEntry m;
  m.name    = name;
  m.version = version;
  m.sources = {nexusSource(modId, fileId)};
  return m;
}

}  // namespace

// Writes the fixture; run it by hand after changing it:
// organizer_tests --gtest_also_run_disabled_tests
//   --gtest_filter=Mo2ListZip.DISABLED_WriteFixture
TEST(Mo2ListZip, DISABLED_WriteFixture)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  Plan p;
  p.header.name        = "Small fixture";
  p.header.author      = "Mod Organizer Linux tests";
  p.header.version     = "1.0";
  p.header.gameName    = "SkyrimSE";
  p.header.nexusDomain = "skyrimspecialedition";

  ModEntry separator;
  separator.name        = "Interface";
  separator.isSeparator = true;
  p.mods.push_back(separator);

  p.mods.push_back(nexusMod("SkyUI", "5.2SE", 12604, 35407));
  p.mods.push_back(nexusMod("Better MessageBox Controls", "1.2", 1428, 1429));
  p.mods.push_back(nexusMod("Better Dialogue Controls", "1.2", 1429, 1430));

  const QString notes = "Fixture Notes";
  ModEntry bundled;
  bundled.name = notes;
  Source b;
  b.kind       = SourceKind::Bundled;
  b.bundledDir = bundledDirName(notes);
  bundled.sources = {b};
  p.mods.push_back(bundled);

  for (const auto* name : {"Skyrim.esm", "Update.esm", "Dawnguard.esm",
                           "HearthFires.esm", "Dragonborn.esm", "SkyUI_SE.esp"}) {
    p.plugins.push_back({name, true});
  }
  p.profile.localInis  = true;
  p.profile.localSaves = true;
  p.profile.iniFiles   = {"Skyrim.ini"};

  PackageInput in;
  in.plan                       = p;
  in.profileFiles["Skyrim.ini"] = "[General]\r\nsLanguage=ENGLISH\r\n";
  in.readme = "# Small fixture\n\nA tiny list for the tests of Mod Organizer.\n";

  const QString readme = tmp.filePath("readme.txt");
  writeFile(readme, "Notes bundled with the small fixture list.\n");
  in.bundledFiles.push_back({b.bundledDir + "/readme.txt", readme});

  ASSERT_TRUE(QDir().mkpath(QFileInfo(FixturePath).absolutePath()));
  writeMo2List(FixturePath, in);
  EXPECT_TRUE(QFile::exists(FixturePath));
}

TEST(Mo2ListZip, FixtureOpens)
{
  ASSERT_TRUE(QFile::exists(FixturePath)) << FixturePath.toStdString();
  const auto pkg = Mo2ListPackage::open(FixturePath);
  const Plan& p  = pkg.plan();

  EXPECT_EQ(p.header.schemaVersion, SchemaVersion);
  EXPECT_EQ(p.header.nexusDomain, "skyrimspecialedition");
  ASSERT_EQ(p.mods.size(), 5u);

  int separators = 0, nexus = 0, bundled = 0;
  QString bundledDir;
  for (const auto& m : p.mods) {
    if (m.isSeparator) {
      ++separators;
      continue;
    }
    ASSERT_EQ(m.sources.size(), 1u) << m.name.toStdString();
    const auto& s = m.sources.front();
    if (s.kind == SourceKind::Nexus) {
      ++nexus;
      EXPECT_GT(s.modId, 0);
      EXPECT_GT(s.fileId, 0);
      EXPECT_TRUE(s.md5.isEmpty());
      EXPECT_EQ(s.size, -1);
    } else if (s.kind == SourceKind::Bundled) {
      ++bundled;
      bundledDir = s.bundledDir;
    }
  }
  EXPECT_EQ(separators, 1);
  EXPECT_EQ(nexus, 3);
  EXPECT_EQ(bundled, 1);

  EXPECT_EQ(pkg.bundledEntries(bundledDir), QStringList{"readme.txt"});
  EXPECT_EQ(p.profile.iniFiles, QStringList{"Skyrim.ini"});
  EXPECT_FALSE(pkg.profileFile("Skyrim.ini").isEmpty());
  EXPECT_FALSE(pkg.readme().isEmpty());
  EXPECT_EQ(p.plugins.size(), 6u);
  EXPECT_TRUE(findExecutableContent(pkg).empty());
}
