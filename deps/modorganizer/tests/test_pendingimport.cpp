#include <gtest/gtest.h>

#include "modlists/pendingimport.h"

using namespace modlists;

TEST(PendingImport, RoundTrip)
{
  PendingImport p;
  p.packagePath         = "/inst/modlist-import.mo2list";
  p.destination         = Destination::ExistingProfile;
  p.profileName         = "Default";
  p.selectedEntries     = {0, 2, 5};
  p.versionChoices      = {{2, VersionChoice::KeepMine}, {5, VersionChoice::UseList}};
  p.sourceDownloadsDir  = "/old/downloads";
  p.installedEntries    = {2};

  const QJsonObject json = toJson(p);
  EXPECT_EQ(json["destination"].toString(), "existingProfile");
  EXPECT_EQ(json["versionChoices"].toObject()["2"].toString(), "keepMine");

  const PendingImport q = pendingFromJson(json);
  EXPECT_EQ(q.packagePath, p.packagePath);
  EXPECT_EQ(q.destination, p.destination);
  EXPECT_EQ(q.profileName, p.profileName);
  EXPECT_EQ(q.selectedEntries, p.selectedEntries);
  EXPECT_EQ(q.versionChoices, p.versionChoices);
  EXPECT_EQ(q.sourceDownloadsDir, p.sourceDownloadsDir);
  EXPECT_EQ(q.installedEntries, p.installedEntries);

  for (auto d : {Destination::NewInstance, Destination::NewProfile}) {
    p.destination = d;
    EXPECT_EQ(pendingFromJson(toJson(p)).destination, d);
  }
}

TEST(PendingImport, ResumeSkipsInstalled)
{
  Plan plan;
  PendingImport p;
  p.selectedEntries  = {0, 1, 2};
  p.installedEntries = {1};
  EXPECT_EQ(remainingEntries(p, plan), (std::vector<int>{0, 2}));
}

namespace
{
QJsonObject validJson()
{
  PendingImport p;
  p.packagePath      = "/x.mo2list";
  p.selectedEntries  = {0, 1};
  p.installedEntries = {1};
  p.versionChoices   = {{1, VersionChoice::KeepMine}};
  return toJson(p);
}

void expectInvalid(const QJsonObject& o)
{
  try {
    pendingFromJson(o);
    ADD_FAILURE() << "did not throw";
  } catch (const PlanError& e) {
    EXPECT_EQ(e.kind(), PlanError::Kind::Invalid);
  }
}
}  // namespace

TEST(PendingImport, MalformedFailsLoudly)
{
  EXPECT_NO_THROW(pendingFromJson(validJson()));

  auto with = [](const QString& key, const QJsonValue& v) {
    QJsonObject o = validJson();
    o[key]        = v;
    return o;
  };
  auto without = [](const QString& key) {
    QJsonObject o = validJson();
    o.remove(key);
    return o;
  };

  expectInvalid(without("destination"));
  expectInvalid(with("destination", "elsewhere"));
  expectInvalid(with("packagePath", ""));
  expectInvalid(without("packagePath"));
  expectInvalid(with("selectedEntries", "nope"));
  expectInvalid(without("installedEntries"));
  expectInvalid(with("selectedEntries", QJsonArray{0, "a"}));
  expectInvalid(with("selectedEntries", QJsonArray{0, 1.5}));
  expectInvalid(with("installedEntries", QJsonArray{-1}));
  expectInvalid(with("versionChoices", QJsonObject{{"x", "useList"}}));
  expectInvalid(with("versionChoices", QJsonObject{{"-2", "useList"}}));
  expectInvalid(with("versionChoices", QJsonObject{{"1", "maybe"}}));
}
