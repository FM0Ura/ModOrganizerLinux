#include "filemappingbuilder.h"

#include <uibase/exceptions.h>

#include <QCoreApplication>
#include <QDir>

using namespace MOBase;

ModMappings buildModMappings(const std::vector<ActiveModDir>& activeMods,
                             const QMap<QString, QStringList>& dataMaps,
                             const QString& overwritePath,
                             const QString& customOverwrite)
{
  ModMappings result;

  bool overwriteActive = false;

  for (const auto& mod : activeMods) {
    if (mod.name.compare("overwrite", Qt::CaseInsensitive) == 0) {
      continue;
    }

    bool createTarget = customOverwrite == mod.name;
    QDir modDir       = QDir(mod.path);

    overwriteActive |= createTarget;

    if (mod.regular) {
      for (auto dataMap : dataMaps.asKeyValueRange()) {
        auto mapDir = QDir(modDir.absoluteFilePath(dataMap.first));
        if (mapDir.exists()) {
          for (auto dir : dataMap.second) {
            result.mods.insert(result.mods.end(),
                               {mapDir.absolutePath(), dir, true, createTarget});
          }
        }
      }
    }
  }

  if (!overwriteActive && !customOverwrite.isEmpty()) {
    throw MyException(
        QCoreApplication::translate(
            "OrganizerCore", "The designated write target \"%1\" is not enabled.")
            .arg(customOverwrite));
  }

  QDir overwriteDir(overwritePath);
  for (auto dataMap : dataMaps.asKeyValueRange()) {
    auto overwriteSubpath = overwriteDir.absoluteFilePath(dataMap.first);
    if (QDir(overwriteSubpath).exists()) {
      for (auto dir : dataMap.second) {
        result.overwrite.insert(result.overwrite.end(), {overwriteSubpath, dir, true,
                                                         customOverwrite.isEmpty()});
      }
    }
  }

  return result;
}
