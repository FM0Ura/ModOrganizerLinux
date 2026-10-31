#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
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


// QZipWriter normalizes entry names ("../x" and "/x" never reach the file), so the
// hostile archives are assembled by hand: stored entries, local headers + central
// directory.
QByteArray rawZip(const std::vector<std::pair<QByteArray, QByteArray>>& entries)
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
  for (const auto& [name, data] : entries) {
    const quint32 offset = static_cast<quint32>(out.size());
    const quint32 crc    = crc32(data);
    o << quint32(0x04034b50) << quint16(20) << quint16(0) << quint16(0) << quint16(0)
      << quint16(0x21) << crc << quint32(data.size()) << quint32(data.size())
      << quint16(name.size()) << quint16(0);
    out.append(name);
    out.append(data);
    c << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0) << quint16(0)
      << quint16(0) << quint16(0x21) << crc << quint32(data.size())
      << quint32(data.size()) << quint16(name.size()) << quint16(0) << quint16(0)
      << quint16(0) << quint16(0) << quint32(0) << offset;
    central.append(name);
  }
  const quint32 cdOffset = static_cast<quint32>(out.size());
  out.append(central);
  o << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(entries.size())
    << quint16(entries.size()) << quint32(central.size()) << cdOffset << quint16(0);
  return out;
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
  const QStringList bad = {"../evil", "/abs", "\\abs", "C:/x", "C:\\x", "a/../../b"};
  int i                 = 0;
  for (const auto& entry : bad) {
    const QString path = tmp.filePath(QString("bad%1.mo2list").arg(i++));
    writeFile(path, rawZip({{"manifest.json", manifest()}, {entry.toUtf8(), "x"}}));
    try {
      Mo2ListPackage::open(path);
      ADD_FAILURE() << "accepted " << entry.toStdString();
    } catch (const PlanError& e) {
      EXPECT_EQ(e.kind(), PlanError::Kind::Invalid) << entry.toStdString();
    }
  }

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
    EXPECT_NE(QString(e.what()).contains("bundled/x/l"), false);
  }

  const QString none = tmp.filePath("none.mo2list");
  {
    QZipWriter zip(none);
    zip.addFile("README.md", "x");
  }
  try {
    Mo2ListPackage::open(none);
    ADD_FAILURE() << "accepted no manifest";
  } catch (const PlanError& e) {
    EXPECT_EQ(e.kind(), PlanError::Kind::Invalid);
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
