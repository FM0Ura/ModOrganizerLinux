#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "modlists/plan.h"

using namespace modlists;

namespace
{

QJsonArray fomodChoices()
{
  const auto doc = QJsonDocument::fromJson(
      R"([{"name":"Main","groups":[{"name":"Textures","choices":[{"name":"2K","idx":1}]}]}])");
  return doc.array();
}

Plan makePlan()
{
  Plan p;
  p.header.name                = "My list";
  p.header.author              = "someone";
  p.header.version             = "1.2";
  p.header.gameName            = "SkyrimSE";
  p.header.nexusDomain         = "skyrimspecialedition";
  p.header.origin              = "mo2list";
  p.header.collectionSlug      = "abc123";
  p.header.collectionRevision  = 4;

  ModEntry sep;
  sep.name        = "Textures_separator";
  sep.isSeparator = true;
  p.mods.push_back(sep);

  ModEntry m;
  m.name            = "Cool Mod";
  m.version         = "3.0";
  m.optional        = true;
  m.enabled         = false;
  m.phase           = 2;
  m.vortexModType   = "dinput";
  Source n1;
  n1.kind   = SourceKind::Nexus;
  n1.modId  = 100;
  n1.fileId = 200;
  n1.md5    = "d41d8cd98f00b204e9800998ecf8427e";
  n1.size   = 12345;
  Source n2;
  n2.kind         = SourceKind::Nexus;
  n2.modId        = 100;
  n2.fileId       = 201;
  n2.fomodChoices = fomodChoices();
  Source man;
  man.kind     = SourceKind::Manual;
  man.url      = "https://example.com/x";
  man.fileName = "x.zip";
  man.md5      = "abcd";
  man.size     = 99;
  Source b;
  b.kind       = SourceKind::Bundled;
  b.bundledDir = "Cool_Mod";
  m.sources    = {n1, n2, man, b};
  p.mods.push_back(m);

  ModEntry second;
  second.name = "Second";
  p.mods.push_back(second);

  p.rules.push_back({"Second", "after", "Cool Mod"});

  p.plugins          = {{"a.esp", true}, {"b.esp", false}};
  p.pluginsUseLoot   = false;
  p.profile.localInis  = false;
  p.profile.localSaves = true;
  p.profile.iniFiles   = {"skyrim.ini", "skyrimprefs.ini"};

  ExecutableEntry e;
  e.title            = "SKSE";
  e.binary           = "%GAME%/skse64_loader.exe";
  e.arguments        = "-foo";
  e.workingDirectory = "%GAME%";
  e.steamAppId       = "489830";
  e.proton           = "GE-Proton";
  e.environment      = {"A=1", "B=2"};
  e.showOnToolbar    = true;
  p.executables.push_back(e);
  return p;
}

void expectEqual(const Source& a, const Source& b)
{
  EXPECT_EQ(a.kind, b.kind);
  EXPECT_EQ(a.modId, b.modId);
  EXPECT_EQ(a.fileId, b.fileId);
  EXPECT_EQ(a.md5, b.md5);
  EXPECT_EQ(a.size, b.size);
  EXPECT_EQ(a.url, b.url);
  EXPECT_EQ(a.fileName, b.fileName);
  EXPECT_EQ(a.bundledDir, b.bundledDir);
  EXPECT_EQ(a.fomodChoices.has_value(), b.fomodChoices.has_value());
  if (a.fomodChoices && b.fomodChoices) {
    EXPECT_EQ(*a.fomodChoices, *b.fomodChoices);
  }
}

}  // namespace

TEST(ModlistPlan, RoundTrip)
{
  const Plan p = makePlan();
  const Plan q = fromJson(toJson(p));

  EXPECT_EQ(q.header.schemaVersion, SchemaVersion);
  EXPECT_EQ(q.header.name, p.header.name);
  EXPECT_EQ(q.header.author, p.header.author);
  EXPECT_EQ(q.header.version, p.header.version);
  EXPECT_EQ(q.header.gameName, p.header.gameName);
  EXPECT_EQ(q.header.nexusDomain, p.header.nexusDomain);
  EXPECT_EQ(q.header.origin, p.header.origin);
  EXPECT_EQ(q.header.collectionSlug, p.header.collectionSlug);
  EXPECT_EQ(q.header.collectionRevision, p.header.collectionRevision);

  ASSERT_EQ(q.mods.size(), p.mods.size());
  for (std::size_t i = 0; i < p.mods.size(); ++i) {
    const auto& a = p.mods[i];
    const auto& b = q.mods[i];
    EXPECT_EQ(a.name, b.name);
    EXPECT_EQ(a.version, b.version);
    EXPECT_EQ(a.isSeparator, b.isSeparator);
    EXPECT_EQ(a.optional, b.optional);
    EXPECT_EQ(a.enabled, b.enabled);
    EXPECT_EQ(a.phase, b.phase);
    EXPECT_EQ(a.vortexModType, b.vortexModType);
    ASSERT_EQ(a.sources.size(), b.sources.size());
    for (std::size_t j = 0; j < a.sources.size(); ++j) {
      expectEqual(a.sources[j], b.sources[j]);
    }
  }

  ASSERT_EQ(q.rules.size(), 1u);
  EXPECT_EQ(q.rules[0].mod, "Second");
  EXPECT_EQ(q.rules[0].type, "after");
  EXPECT_EQ(q.rules[0].reference, "Cool Mod");

  EXPECT_EQ(q.pluginsUseLoot, p.pluginsUseLoot);
  ASSERT_EQ(q.plugins.size(), 2u);
  EXPECT_EQ(q.plugins[0].name, "a.esp");
  EXPECT_TRUE(q.plugins[0].enabled);
  EXPECT_EQ(q.plugins[1].name, "b.esp");
  EXPECT_FALSE(q.plugins[1].enabled);

  EXPECT_EQ(q.profile.localInis, p.profile.localInis);
  EXPECT_EQ(q.profile.localSaves, p.profile.localSaves);
  EXPECT_EQ(q.profile.iniFiles, p.profile.iniFiles);

  ASSERT_EQ(q.executables.size(), 1u);
  const auto& e = q.executables[0];
  const auto& f = p.executables[0];
  EXPECT_EQ(e.title, f.title);
  EXPECT_EQ(e.binary, f.binary);
  EXPECT_EQ(e.arguments, f.arguments);
  EXPECT_EQ(e.workingDirectory, f.workingDirectory);
  EXPECT_EQ(e.steamAppId, f.steamAppId);
  EXPECT_EQ(e.proton, f.proton);
  EXPECT_EQ(e.environment, f.environment);
  EXPECT_EQ(e.showOnToolbar, f.showOnToolbar);
}

TEST(ModlistPlan, NewerSchemaRejected)
{
  QJsonObject o = toJson(makePlan());
  o["schemaVersion"] = 2;
  try {
    fromJson(o);
    FAIL() << "expected PlanError";
  } catch (const PlanError& e) {
    EXPECT_EQ(e.kind(), PlanError::Kind::NewerSchema);
  }
}

TEST(ModlistPlan, UnknownSourceFailsLoud)
{
  QJsonObject o    = toJson(makePlan());
  QJsonArray mods  = o["mods"].toArray();
  QJsonObject mod  = mods[1].toObject();
  QJsonArray srcs  = mod["sources"].toArray();
  QJsonObject src  = srcs[0].toObject();
  src["type"]      = "wabbajack";
  srcs[0]          = src;
  mod["sources"]   = srcs;
  mods[1]          = mod;
  o["mods"]        = mods;
  try {
    fromJson(o);
    FAIL() << "expected PlanError";
  } catch (const PlanError& e) {
    EXPECT_EQ(e.kind(), PlanError::Kind::UnknownSource);
    EXPECT_NE(QString::fromUtf8(e.what()).indexOf("wabbajack"), -1);
  }
}

TEST(ModlistPlan, MissingRequiredField)
{
  QJsonObject o   = toJson(makePlan());
  QJsonArray mods = o["mods"].toArray();
  QJsonObject mod = mods[1].toObject();
  mod.remove("name");
  mods[1] = mod;
  o["mods"] = mods;
  try {
    fromJson(o);
    FAIL() << "expected PlanError";
  } catch (const PlanError& e) {
    EXPECT_EQ(e.kind(), PlanError::Kind::Invalid);
  }
}
