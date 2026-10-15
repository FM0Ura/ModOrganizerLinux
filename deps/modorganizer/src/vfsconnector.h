#ifndef VFSCONNECTOR_H
#define VFSCONNECTOR_H

#include "envdump.h"
#include <QList>
#include <QString>
#include <QStringList>
#include <chrono>
#include <memory>
#include <uibase/executableinfo.h>
#include <uibase/filemapping.h>
#include <uibase/log.h>

// the virtual file system that shows the mods in the game directory to the programs
// started by Mod Organizer
//
// on Windows, this is usvfs (UsvfsConnector, usvfsconnector.h); on Linux, there is no
// virtual file system yet (NullVfsConnector below, which only remembers the mappings)
//
class VfsConnector
{
public:
  virtual ~VfsConnector() = default;

  // replaces the mappings of the virtual file system
  //
  virtual void updateMapping(const MappingType& mapping) = 0;

  virtual void
  updateParams(MOBase::log::Levels logLevel, env::CoreDumpTypes coreDumpType,
               const QString& crashDumpsPath, std::chrono::seconds spawnDelay,
               QString executableBlacklist, const QStringList& skipFileSuffixes,
               const QStringList& skipDirectories) = 0;

  virtual void updateForcedLibraries(
      const QList<MOBase::ExecutableForcedLoadSetting>& forcedLibraries) = 0;

  // name of the virtual file system, for logging: "usvfs" or "none"
  //
  virtual QString name() const = 0;
};

// creates the virtual file system of this platform
//
std::unique_ptr<VfsConnector> createVfsConnector();

#ifndef _WIN32
// Linux has no virtual file system yet: this connector only keeps the last mappings
// (with '/' separators), for the tests and for the file system that will use them
// (src/linux/vfsconnector_linux.cpp)
//
class NullVfsConnector : public VfsConnector
{
public:
  void updateMapping(const MappingType& mapping) override;

  void updateParams(MOBase::log::Levels logLevel, env::CoreDumpTypes coreDumpType,
                    const QString& crashDumpsPath, std::chrono::seconds spawnDelay,
                    QString executableBlacklist, const QStringList& skipFileSuffixes,
                    const QStringList& skipDirectories) override;

  void updateForcedLibraries(
      const QList<MOBase::ExecutableForcedLoadSetting>& forcedLibraries) override;

  QString name() const override;

  const MappingType& lastMapping() const;

private:
  MappingType m_mapping;
};
#endif  // _WIN32

#endif  // VFSCONNECTOR_H
