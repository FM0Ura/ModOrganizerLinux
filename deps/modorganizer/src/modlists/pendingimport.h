#ifndef MODLISTS_PENDINGIMPORT_H
#define MODLISTS_PENDINGIMPORT_H

#include "importplan.h"

#include <QJsonObject>

#include <map>
#include <vector>

// An import that has not finished (MO2 closed in the middle of it). The record
// lives in <instance>/modlist-import.json and the package is copied to
// <instance>/modlist-import.mo2list.
namespace modlists
{

struct PendingImport
{
  QString packagePath;
  Destination destination = Destination::NewInstance;
  QString profileName;
  // indices into Plan::mods
  std::vector<int> selectedEntries;
  // keyed by the index of the entry in Plan::mods
  std::map<int, VersionChoice> versionChoices;
  QString sourceDownloadsDir;
  // indices into Plan::mods of the entries already installed
  std::vector<int> installedEntries;
};

QJsonObject toJson(const PendingImport& pending);
PendingImport pendingFromJson(const QJsonObject& json);

// selectedEntries minus installedEntries, in the order of selectedEntries
std::vector<int> remainingEntries(const PendingImport& pending, const Plan& plan);

}  // namespace modlists

#endif  // MODLISTS_PENDINGIMPORT_H
