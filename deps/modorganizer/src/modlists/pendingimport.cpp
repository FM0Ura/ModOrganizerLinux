#include "pendingimport.h"

#include <QJsonArray>

#include <algorithm>

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

static Destination destinationFromName(const QString& s)
{
  if (s == "newProfile") {
    return Destination::NewProfile;
  }
  if (s == "existingProfile") {
    return Destination::ExistingProfile;
  }
  return Destination::NewInstance;
}

static QJsonArray intArray(const std::vector<int>& v)
{
  QJsonArray a;
  for (int i : v) {
    a.append(i);
  }
  return a;
}

static std::vector<int> intVector(const QJsonValue& v)
{
  std::vector<int> r;
  for (const QJsonValue& e : v.toArray()) {
    r.push_back(e.toInt());
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
  p.packagePath         = o["packagePath"].toString();
  p.destination         = destinationFromName(o["destination"].toString());
  p.profileName         = o["profileName"].toString();
  p.selectedEntries     = intVector(o["selectedEntries"]);
  p.sourceDownloadsDir  = o["sourceDownloadsDir"].toString();
  p.installedEntries    = intVector(o["installedEntries"]);

  const QJsonObject choices = o["versionChoices"].toObject();
  for (auto it = choices.begin(); it != choices.end(); ++it) {
    p.versionChoices[it.key().toInt()] = it.value().toString() == "keepMine"
                                             ? VersionChoice::KeepMine
                                             : VersionChoice::UseList;
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
