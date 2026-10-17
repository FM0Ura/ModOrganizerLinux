#ifndef FILEMAPPINGBUILDER_H
#define FILEMAPPINGBUILDER_H

#include <uibase/filemapping.h>

#include <QMap>
#include <QString>
#include <QStringList>
#include <vector>

// a mod of the active profile, as far as the mappings are concerned
struct ActiveModDir
{
  QString name;
  QString path;
  bool regular;  // false for separators, backups and the like: no mapping
};

// the mappings of the mods and of the overwrite directory, kept apart because the
// caller puts other mappings (local saves) between them: the order is the priority
struct ModMappings
{
  MappingType mods;
  MappingType overwrite;
};

// the part of OrganizerCore::fileMapping() that doesn't need the application: which
// mod directories are mapped where, in priority order, and the overwrite directory
//
// an entry called "overwrite" in activeMods is skipped. customOverwrite is the mod that
// receives the new files (empty: the overwrite directory does)
//
// throws MyException when customOverwrite is not empty and not an active mod
//
ModMappings buildModMappings(const std::vector<ActiveModDir>& activeMods,
                             const QMap<QString, QStringList>& dataMaps,
                             const QString& overwritePath,
                             const QString& customOverwrite);

#endif  // FILEMAPPINGBUILDER_H
