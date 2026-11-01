#include "pendingimport.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace modlists
{

static QString destinationName(Destination d)
{
  switch (d) {
  case Destination::NewProfile:
    return "newProfile";
  case Destination::ExistingProfile:
    return "existingProfile";
  case Destination::NewInstance:
  default:
    return "newInstance";
  }
}

static PlanError invalid(const QString& what)
{
  return PlanError(PlanError::Kind::Invalid, "Invalid pending import: " + what);
}

static Destination destinationFromName(const QString& s)
{
  if (s == "newInstance") {
    return Destination::NewInstance;
  }
  if (s == "newProfile") {
    return Destination::NewProfile;
  }
  if (s == "existingProfile") {
    return Destination::ExistingProfile;
  }
  throw invalid("unknown destination");
}

static QJsonArray intArray(const std::vector<int>& v)
{
  QJsonArray a;
  for (int i : v) {
    a.append(i);
  }
  return a;
}

static int indexFromDouble(double d, const QString& what)
{
  if (d < 0 || d > 1e9 || d != std::floor(d)) {
    throw invalid(what + " has an invalid index");
  }
  return static_cast<int>(d);
}

static std::vector<int> intVector(const QJsonValue& v, const QString& what)
{
  if (!v.isArray()) {
    throw invalid(what + " is not an array");
  }
  std::vector<int> r;
  for (const QJsonValue& e : v.toArray()) {
    if (!e.isDouble()) {
      throw invalid(what + " has a non-integer entry");
    }
    r.push_back(indexFromDouble(e.toDouble(), what));
  }
  return r;
}

QJsonObject toJson(const PendingImport& p)
{
  QJsonObject choices;
  for (const auto& [index, choice] : p.versionChoices) {
    choices[QString::number(index)] =
        choice == VersionChoice::KeepMine ? "keepMine" : "useList";
  }

  QJsonObject o;
  o["packagePath"]        = p.packagePath;
  o["destination"]        = destinationName(p.destination);
  o["profileName"]        = p.profileName;
  o["selectedEntries"]    = intArray(p.selectedEntries);
  o["versionChoices"]     = choices;
  o["sourceDownloadsDir"] = p.sourceDownloadsDir;
  o["installedEntries"]   = intArray(p.installedEntries);
  return o;
}

PendingImport pendingFromJson(const QJsonObject& o)
{
  PendingImport p;
  p.packagePath = o["packagePath"].toString();
  if (p.packagePath.isEmpty()) {
    throw invalid("packagePath is empty");
  }
  p.destination        = destinationFromName(o["destination"].toString());
  p.profileName        = o["profileName"].toString();
  p.selectedEntries    = intVector(o["selectedEntries"], "selectedEntries");
  p.sourceDownloadsDir = o["sourceDownloadsDir"].toString();
  p.installedEntries   = intVector(o["installedEntries"], "installedEntries");

  const QJsonObject choices = o["versionChoices"].toObject();
  for (auto it = choices.begin(); it != choices.end(); ++it) {
    bool ok         = false;
    const int index = it.key().toInt(&ok);
    if (!ok || index < 0) {
      throw invalid("versionChoices has an invalid key");
    }
    const QString value = it.value().toString();
    if (value == "keepMine") {
      p.versionChoices[index] = VersionChoice::KeepMine;
    } else if (value == "useList") {
      p.versionChoices[index] = VersionChoice::UseList;
    } else {
      throw invalid("versionChoices has an unknown value");
    }
  }
  return p;
}

std::vector<int> remainingEntries(const PendingImport& p, const Plan&)
{
  std::vector<int> r;
  for (int i : p.selectedEntries) {
    if (std::find(p.installedEntries.begin(), p.installedEntries.end(), i) ==
        p.installedEntries.end()) {
      r.push_back(i);
    }
  }
  return r;
}

}  // namespace modlists
