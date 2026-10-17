#ifndef VFSCONNECTOR_H
#define VFSCONNECTOR_H

#include "envdump.h"
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <chrono>
#include <functional>
#include <memory>
#include <uibase/executableinfo.h>
#include <uibase/filemapping.h>
#include <uibase/log.h>

// the virtual file system that shows the mods in the game directory to the programs
// started by Mod Organizer
//
// on Windows, this is usvfs (UsvfsConnector, usvfsconnector.h); on Linux, it is the
// FUSE file system of the mo2-vfs program (LinuxVfsConnector,
// linux/vfsconnector_linux.cpp)
//
// usvfs is active as long as the process that uses it lives; the Linux file system is
// mounted explicitly: the methods below the mappings and parameters are for that, and
// do nothing by default
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

  // name of the virtual file system, for logging: "usvfs" or "mo2-vfs"
  //
  virtual QString name() const = 0;

  // mount points left by a previous run that couldn't be unmounted, for the user
  virtual QStringList staleMounts() const { return {}; }

  // mounts the virtual file system with the last mappings (or updates it if it is
  // already mounted); throws std::runtime_error with the reason when it can't
  virtual void activate() {}

  // the virtual file system stays mounted while this process group has processes
  virtual void watchProcessGroup(qint64 pgid) { Q_UNUSED(pgid); }

  // unmounts if no process group is watched (the program didn't start)
  virtual void releaseIfIdle() {}

  virtual bool isActive() const { return false; }

  // true when inactive within the timeout
  virtual bool waitInactive(std::chrono::milliseconds timeout)
  {
    Q_UNUSED(timeout);
    return true;
  }

  // called in the thread of the connector's object when the file system was unmounted
  virtual void setDeactivatedCallback(std::function<void()> f) { Q_UNUSED(f); }
};

// creates the virtual file system of this platform
//
std::unique_ptr<VfsConnector> createVfsConnector();

#endif  // VFSCONNECTOR_H
