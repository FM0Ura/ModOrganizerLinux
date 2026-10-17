// Linux implementations of the Win32-only functions of processrunner.cpp (the rest of
// processrunner.cpp is shared).
//
// - run() starts native programs (spawn::isNativeProgram(): ELF executables and #!
//   scripts) like the Windows run() starts programs: hooked (the default), the profile
//   is saved and OrganizerCore::beforeRun() prepares the virtual file system and
//   mounts it (VfsConnector::activate()), spawn::startBinary() starts the program in
//   a process group of its own, which mo2-vfs watches to keep the file system mounted
//   while the group lives (VfsConnector::watchProcessGroup()), and postRun() waits
//   for it; if the program doesn't start, the file system is unmounted unless another
//   program uses it (VfsConnector::releaseIfIdle()). Unhooked, it is started without
//   the virtual file system. There is no Steam check and no executable blacklist
//   (no-ops on Linux, see spawn_linux.cpp), and a program inside the mods directory is
//   not redirected to its place in the data directory through the "launch" command of
//   another instance (adjustForVirtualized(), which the Linux "launch" doesn't
//   support): it runs from where it is.
// - Windows programs (.exe, .bat, ...) are not started before Phase 3 of the port: it
//   tells the user and returns Error, without saving the profile, preparing the virtual
//   file system or notifying the plugins (spawn::startBinary() shows the same message
//   to the other callers, such as LOOT); documents (setFromFile() with a file that is
//   not a program) are still opened by their application, unhooked,
// - waitForAllUSVFSProcessesWithLock() doesn't look for the programs that use the
//   virtual file system (usvfs knows them, mo2-vfs only knows their process groups),
// - a process is waited for with its pid (the HANDLE of a process on Linux, see
//   envmodule.h); there are no job objects, the tree of the first process is followed
//   instead (env::getProcessTree()), and the exit code is only known for children of
//   this process.

#include "../iuserinterface.h"
#include "../organizercore.h"
#include "../processrunner.h"
#include "../profile.h"
#include "../spawn.h"

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

  if (shouldRunShell() || !spawn::isNativeProgram(m_sp.binary)) {
    log::warn("not running '{}' {}: starting Windows programs is not available on "
              "Linux yet",
              (m_shellOpen.filePath().isEmpty() ? m_sp.binary : m_shellOpen)
                  .absoluteFilePath(),
              m_sp.arguments);

    QMessageBox::information(parent, QObject::tr("Mod Organizer"),
                             spawn::launchUnavailableMessage());

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

  m_handle.reset(spawn::startBinary(parent, m_sp));

  if (m_handle.get() == INVALID_HANDLE_VALUE) {
    if (m_sp.hooked) {
      // nothing will end the group that keeps the file system mounted
      m_core.vfs().releaseIfIdle();
    }

    return Error;
  }

  if (m_sp.hooked) {
    m_core.vfs().watchProcessGroup(toPid(m_handle.get()));
  }

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

  // the programs that use the virtual file system are not known here (mo2-vfs keeps
  // it mounted while their process groups live)
  return Completed;
}
