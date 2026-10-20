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
#include <stdexcept>
#include <uibase/executableinfo.h>
#include <uibase/filemapping.h>
#include <uibase/log.h>
#include <vector>

// a program started by the helper of the virtual file system, where the helper runs
// (on the host when Mod Organizer runs in a container); Linux only
struct VfsSpawnRequest
{
  QStringList argv;         // argv[0] is looked up in the PATH of the helper
  QStringList environment;  // NAME=value, over the environment of the helper
  QString workingDirectory;
  QString winePrefix;  // the compatdata directory of a Proton program, or empty
  QString output;      // file for the output and errors of the program, or empty
};

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

  // true while mounted, while being mounted (activate()) and, on Linux, while a mount
  // that is not usable any more could not be unmounted: the data directory would not
  // show the game's own files, so the directory refresh waits until this is false
  virtual bool isActive() const { return false; }

  // true when inactive within the timeout
  virtual bool waitInactive(std::chrono::milliseconds timeout)
  {
    Q_UNUSED(timeout);
    return true;
  }

  // called in the thread of the connector's object when the file system was unmounted
  // (queued: skipped if it was activated again meanwhile); it must not call the
  // connector while a request of the connector is running (activate(),
  // updateMapping(), ...), which throws or is logged as an error
  virtual void setDeactivatedCallback(std::function<void()> f) { Q_UNUSED(f); }

  // why the virtual file system stopped while it was mounted (the helper program ended
  // or stopped answering), once: empty when it didn't or when it was already taken
  virtual QString takeLostReason() { return {}; }

  // starts a program through the helper (started if needed, without mounting) and
  // returns its pid; while mounted, its process group keeps the file system mounted;
  // throws std::runtime_error with the reason when it can't start
  virtual qint64 spawn(const VfsSpawnRequest& request)
  {
    Q_UNUSED(request);
    throw std::runtime_error("starting programs through the virtual file system is "
                             "not supported on this platform");
  }

  // ends a program started by spawn() and its process group: the helper sends SIGTERM,
  // then SIGKILL after 3 s (a program on the host may not be signalled from a
  // container); its exit code comes as usual; throws std::runtime_error with the reason
  // when the helper refuses (not one of its running programs) or is not running
  virtual void kill(qint64 pid) { Q_UNUSED(pid); }

  // the pids the virtual file system waits for: programs started by spawn() that still
  // run, and the Wine servers that keep it mounted (empty without a helper)
  virtual std::vector<qint64> runningPrograms() { return {}; }
};

// creates the virtual file system of this platform
//
std::unique_ptr<VfsConnector> createVfsConnector();

#endif  // VFSCONNECTOR_H
