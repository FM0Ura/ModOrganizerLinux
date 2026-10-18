// Linux implementations of the Win32-only functions of processrunner.cpp (the rest of
// processrunner.cpp is shared).
//
// - run() starts programs like the Windows run() does: hooked (the default), the
//   profile is saved and OrganizerCore::beforeRun() prepares the virtual file system
//   and mounts it (VfsConnector::activate()), then the program is started by mo2-vfs
//   (VfsConnector::spawn()), which keeps the file system mounted while the process
//   group of the program lives, and postRun() waits for it. The launch is decided by
//   proton::makeLaunch() (protonlauncher_linux.cpp): native programs (ELF executables
//   and #! scripts) run directly, Windows programs (.exe, .bat, ...) through umu-run
//   in the Proton prefix of their Steam game, with the Proton version and the
//   environment of the executable. A launch error (no Steam game, no prefix, ...) is
//   reported before anything is mounted. If the program doesn't start, the file
//   system is unmounted unless another program uses it (VfsConnector::releaseIfIdle()).
//   Unhooked, it is started without the virtual file system. The errors are shown in
//   a dialog only when there is a user interface (not in the run command line, where
//   they are only logged). There is no Steam check and no executable blacklist
//   (no-ops on Linux, see spawn_linux.cpp), and a program inside the mods directory is
//   not redirected to its place in the data directory through the "launch" command of
//   another instance (adjustForVirtualized(), which the Linux "launch" doesn't
//   support): it runs from where it is. Documents (setFromFile() with a file that is
//   not a program) are opened by their application, unhooked.
// - waitForAllUSVFSProcessesWithLock() waits for the programs mo2-vfs started
//   (VfsConnector::runningPrograms(): the programs and the Wine servers that keep the
//   mount) instead of the processes usvfs knows,
// - a process is waited for with its pid (the HANDLE of a process on Linux, see
//   envmodule.h); there are no job objects, the tree of the first process is followed
//   instead (env::getProcessTree(), through /proc, with the PID namespace shared with
//   the container), and the exit code of a program started by mo2-vfs, which is not a
//   child of this process, comes from the helper's "exited" event
//   (exitcodes::take()).

#include "../iuserinterface.h"
#include "../organizercore.h"
#include "../processrunner.h"
#include "../profile.h"
#include "../shared/appconfig.h"
#include "../spawn.h"
#include "exitcodes_linux.h"
#include "protonlauncher_linux.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QMessageBox>
#include <QThread>

#include <uibase/exceptions.h>
#include <uibase/log.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <signal.h>
#include <sys/wait.h>
#include <thread>

using namespace MOBase;

// processrunner.cpp
void waitForProcessesThread(ProcessRunner::Results& result, HANDLE job,
                            UILocker::Session* ls, std::atomic<bool>& interrupt);

namespace
{

pid_t toPid(HANDLE h)
{
  return static_cast<pid_t>(reinterpret_cast<std::intptr_t>(h));
}

}  // namespace

std::optional<ProcessRunner::Results> singleWait(HANDLE handle, DWORD pid)
{
  if (handle == INVALID_HANDLE_VALUE) {
    return ProcessRunner::Error;
  }

  const pid_t p = toPid(handle);

  // WNOWAIT leaves a child that has exited to waitForProcess(), which gets its exit
  // code
  siginfo_t info = {};
  if (::waitid(P_PID, static_cast<id_t>(p), &info, WEXITED | WNOHANG | WNOWAIT) == 0) {
    if (info.si_pid == p) {
      log::debug("process {} completed", pid);
      return ProcessRunner::Completed;
    }
  } else if (errno == ECHILD) {
    // not a child of this process
    if (::kill(p, 0) != 0 && errno == ESRCH) {
      log::debug("process {} completed", pid);
      return ProcessRunner::Completed;
    }
  } else {
    const auto e = errno;
    log::error("failed waiting for {}, {}", pid, std::strerror(e));
    return ProcessRunner::Error;
  }

  // still running, wait a bit like WaitForSingleObject() does on Windows
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  return {};
}

ProcessRunner::Results waitForProcesses(const std::vector<HANDLE>& initialProcesses,
                                        UILocker::Session* ls)
{
  if (initialProcesses.empty()) {
    // nothing to wait for
    return ProcessRunner::Completed;
  }

  // no job objects: the tree of the first process is monitored
  HANDLE monitor = initialProcesses[0];

  auto results = ProcessRunner::Running;
  std::atomic<bool> interrupt(false);

  auto* t = QThread::create(waitForProcessesThread, std::ref(results), monitor, ls,
                            std::ref(interrupt));

  QEventLoop events;
  QObject::connect(t, &QThread::finished, [&] {
    events.quit();
  });

  t->start();
  events.exec();

  if (t->isRunning()) {
    interrupt = true;
    t->wait();
  }

  delete t;

  return results;
}

ProcessRunner::Results waitForProcess(HANDLE initialProcess, LPDWORD exitCode,
                                      UILocker::Session* ls)
{
  const auto r = waitForProcesses({initialProcess}, ls);

  // as long as it's not running anymore, try to get the exit code
  if (exitCode && r != ProcessRunner::Running) {
    int status = 0;

    if (::waitpid(toPid(initialProcess), &status, WNOHANG) == toPid(initialProcess)) {
      if (WIFEXITED(status)) {
        *exitCode = static_cast<DWORD>(WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        *exitCode = static_cast<DWORD>(128 + WTERMSIG(status));
      }
    } else if (const auto code =
                   exitcodes::take(toPid(initialProcess), std::chrono::seconds(2))) {
      // started by mo2-vfs: not a child of this process
      *exitCode = static_cast<DWORD>(*code);
    } else {
      log::warn("failed to get exit code of process {}",
                static_cast<int>(toPid(initialProcess)));
    }
  }

  return r;
}

ProcessRunner::Results ProcessRunner::run()
{
  // a document (setFromFile() with a file that is not a program) is opened by its
  // application, like on Windows, but never hooked: there is no virtual file system
  if (shouldRunShell() &&
      spawn::getFileExecutionType(m_shellOpen) == spawn::FileExecutionTypes::Other) {
    m_sp.hooked = false;

    if (const auto r = runShell()) {
      return *r;
    }

    return postRun();
  }

  QWidget* parent = (m_ui ? m_ui->mainWindow() : nullptr);

  // without a user interface (the run command) a dialog would never be answered
  auto showError = [&](const QString& text) {
    log::error("{}", text);
    if (m_ui) {
      QMessageBox::critical(parent, QObject::tr("Mod Organizer"), text);
    }
  };

  if (shouldRunShell()) {
    // setFromFile() keeps only documents for the shell, handled above
    showError(QObject::tr("Cannot start \"%1\".").arg(m_shellOpen.absoluteFilePath()));
    return Error;
  }

  // the output of the program, next to the logs of Mod Organizer (none in the tests)
  QString output;
  if (const auto dp = qApp->property("dataPath"); !dp.isNull()) {
    output = dp.toString() + "/" + QString::fromStdWString(AppConfig::logPath()) +
             "/run-" + m_sp.binary.completeBaseName() + ".log";
  }

  VfsSpawnRequest request;
  try {
    const auto* game = m_core.managedGame();
    request          = proton::makeLaunch(
        {m_sp.binary, m_sp.arguments, m_sp.currentDirectory.path(), m_sp.steamAppID,
         game ? game->steamAPPId() : QString(), m_sp.proton, m_sp.environment, output});
  } catch (const proton::LaunchError& e) {
    showError(QString::fromStdString(e.what()));
    return Error;
  }

  if (m_sp.hooked) {
    if (m_profileName.isEmpty()) {
      // get the current profile name if it wasn't overridden
      const auto profile = m_core.currentProfile();
      if (!profile) {
        throw MyException(QObject::tr("No profile set"));
      }

      m_profileName = profile->name();
    }

    // saves the profile, mounts the virtual file system, notifies the plugins, etc.;
    // false if a plugin doesn't want the program to run or the file system can't be
    // mounted (the user has been told)
    if (!m_core.beforeRun(m_sp.binary, m_sp.currentDirectory, m_sp.arguments,
                          m_profileName, m_customOverwrite, m_forcedLibraries)) {
      return Error;
    }
  }

  qint64 pid = 0;
  try {
    pid = m_core.vfs().spawn(request);
  } catch (const std::exception& e) {
    auto text =
        QObject::tr("Cannot start \"%1\": %2")
            .arg(m_sp.binary.absoluteFilePath(), QString::fromStdString(e.what()));
    if (request.argv.value(0) == "umu-run" &&
        QString::fromStdString(e.what()).contains("umu-run")) {
      // spawn_failed for argv[0]: umu-run is not in the PATH of the host
      text += "\n\n" + QObject::tr("Windows programs run with umu-run: install "
                                   "umu-launcher on the host.");
    }
    showError(text);
    if (m_sp.hooked) {
      // nothing will end the group that keeps the file system mounted
      m_core.vfs().releaseIfIdle();
    }
    return Error;
  }

  log::debug("started '{}', pid {}", m_sp.binary.absoluteFilePath(), pid);
  m_handle.reset(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(pid)));

  return postRun();
}

ProcessRunner::Results
ProcessRunner::waitForAllUSVFSProcessesWithLock(UILocker::Reasons reason)
{
  m_lockReason = reason;

  if (!m_core.settings().interface().lockGUI()) {
    // disabling locking is like clicking on unlock immediately
    return ForceUnlocked;
  }

  auto r = Error;

  for (;;) {
    withLock([&](auto& ls) {
      // the programs started through mo2-vfs and the Wine servers that keep its mount
      std::vector<HANDLE> processes;
      for (const auto pid : m_core.vfs().runningPrograms()) {
        processes.push_back(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(pid)));
      }

      if (processes.empty()) {
        r = Completed;
        return;
      }

      r = waitForProcesses(processes, &ls);

      if (r != Completed) {
        // error, cancelled, or unlocked
        return;
      }

      // this process is completed, check for others
      r = Running;
    });

    if (r != Running) {
      break;
    }
  }

  return r;
}
