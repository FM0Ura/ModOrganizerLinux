// Linux implementations of the members of Loot (loot.h) that start lootcli and wait for
// it; the Windows ones are in loot.cpp, the free functions they use in loot_linux.cpp.
//
// Like Run (processrunner_linux.cpp), lootcli is started through mo2-vfs
// (VfsConnector::spawn()) with the virtual file system mounted over the game
// directory, the usvfs of Windows (spec of Phase 4b, section 3.4): it sees the plugins
// of the mods in the data directory. When Mod Organizer runs in a container, mo2-vfs
// and lootcli run on the host: the output of lootcli goes to a file in the logs of the
// instance (AsyncPipe reads it), the end of lootcli is seen through /proc (the PID
// namespace is the host's) and its exit code comes from the exited event of mo2-vfs
// (exitcodes_linux.h). Cancelling asks mo2-vfs to end it (VfsConnector::kill()): a
// process of the host may not be signalled from the container. When its process group
// ends, mo2-vfs unmounts unless another program uses the mount.

#include "../loot.h"
#include "../organizercore.h"
#include "../settings.h"
#include "exitcodes_linux.h"
#include "loot_linux.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QThread>

#include <iplugingame.h>
#include <log.h>
#include <report.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <sys/types.h>

using namespace MOBase;

namespace
{
// how long a cancelled lootcli may take to end (mo2-vfs sends SIGKILL after 3 s)
constexpr qint64 CancelTimeoutMs = 10'000;

pid_t pidOf(const env::HandlePtr& h)
{
  return static_cast<pid_t>(reinterpret_cast<std::intptr_t>(h.get()));
}

bool alive(pid_t pid)
{
  // EPERM: a process of the host that the container may not signal still runs
  return ::kill(pid, 0) == 0 || errno == EPERM;
}
}  // namespace

bool Loot::spawnLootcli(QWidget*, bool didUpdateMasterList, env::HandlePtr)
{
  LootcliRun run;
  run.lootcli        = lootcliPath();
  run.game           = m_core.managedGame()->lootGameName();
  run.gamePath       = m_core.managedGame()->gameDirectory().absolutePath();
  run.pluginListPath = m_core.profilePath() + "/loadorder.txt";
  run.logLevel       = QString::fromStdString(
      lootcli::logLevelToString(m_core.settings().diagnostics().lootLogLevel()));
  run.reportPath           = reportPath();
  run.sortedPluginListPath = sortedPluginListPath();
  run.outputPath           = m_pipe->path();
  run.language             = m_core.settings().interface().language();
  run.skipUpdateMasterlist = didUpdateMasterList;

  try {
    // Loot::start() gave the mappings to the file system (prepareVFS())
    m_core.vfs().activate();
    const auto pid = m_core.vfs().spawn(lootcliSpawnRequest(run));
    log::debug("lootcli started through mo2-vfs, pid {}", pid);
    m_lootProcess.reset(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(pid)));
    return true;
  } catch (const std::exception& e) {
    // nothing will end a group that keeps the file system mounted
    m_core.vfs().releaseIfIdle();
    log::error("failed to start lootcli: {}", e.what());
    // the dialog of LOOT opens only once lootcli runs
    reportError(tr("failed to start loot: %1").arg(QString::fromStdString(e.what())));
    return false;
  }
}

bool Loot::waitForCompletion()
{
  const auto pid     = pidOf(m_lootProcess);
  bool killRequested = false;
  QElapsedTimer sinceKill;

  log::debug("loot thread waiting for completion on lootcli ({})", pid);

  while (alive(pid)) {
    if (m_cancel && !killRequested) {
      log::debug("asking mo2-vfs to end lootcli");
      killRequested = true;
      sinceKill.start();

      // the connector belongs to the main thread; OrganizerCore outlives this run
      VfsConnector* vfs = &m_core.vfs();
      QMetaObject::invokeMethod(
          qApp,
          [vfs, pid] {
            try {
              vfs->kill(pid);
            } catch (const std::exception& e) {
              log::error("cannot end lootcli: {}", e.what());
            }
          },
          Qt::QueuedConnection);
    }

    if (killRequested && sinceKill.elapsed() > CancelTimeoutMs) {
      log::error("lootcli did not end within {} ms of the cancel", CancelTimeoutMs);
      return false;
    }

    processStdout(m_pipe->read());
    QThread::msleep(100);
  }

  processStdout(m_pipe->read());

  // taken even when cancelled, so that it doesn't stay recorded
  const auto code = exitcodes::take(pid, std::chrono::seconds(2));

  if (m_cancel) {
    log::debug("lootcli ended after the cancel");
    return false;
  }

  if (!code) {
    emit log(log::Levels::Error,
             tr("lootcli ended, but mo2-vfs did not report its exit code"));
    return false;
  }

  if (*code != 0) {
    // what the dynamic loader or a crash wrote, which is not in the protocol
    for (const auto& line : lootcliOtherOutput(m_pipe->path())) {
      m_errors.push_back(line);
      emit log(log::Levels::Error, line);
    }

    emit log(log::Levels::Error,
             tr("Loot failed. Exit code was: 0x%1").arg(*code, 0, 16));
    return false;
  }

  return true;
}
