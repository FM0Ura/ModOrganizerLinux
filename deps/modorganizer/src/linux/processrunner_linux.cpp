// Linux implementations of the Win32-only functions of processrunner.cpp (the rest of
// processrunner.cpp is shared).
//
// - run() doesn't start programs before Phase 3 of the port: it tells the user and
//   returns Error, without saving the profile, preparing the virtual file system or
//   notifying the plugins (spawn::startBinary() shows the same message to the other
//   callers, such as LOOT); documents (setFromFile() with a file that is not a
//   program) are still opened by their application, unhooked,
// - there is no virtual file system, so waitForAllUSVFSProcessesWithLock() has no
//   process to wait for,
// - a process is waited for with its pid (the HANDLE of a process on Linux, see
//   envmodule.h); there are no job objects, the tree of the first process is followed
//   instead (env::getProcessTree()), and the exit code is only known for children of
//   this process.

#include "../iuserinterface.h"
#include "../organizercore.h"
#include "../processrunner.h"
#include "../spawn.h"

#include <QEventLoop>
#include <QMessageBox>
#include <QThread>

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

  log::warn(
      "not running '{}' {}: starting programs is not available on Linux yet",
      (m_shellOpen.filePath().isEmpty() ? m_sp.binary : m_shellOpen).absoluteFilePath(),
      m_sp.arguments);

  QWidget* parent = (m_ui ? m_ui->mainWindow() : nullptr);

  QMessageBox::information(parent, QObject::tr("Mod Organizer"),
                           spawn::launchUnavailableMessage());

  return Error;
}

ProcessRunner::Results
ProcessRunner::waitForAllUSVFSProcessesWithLock(UILocker::Reasons reason)
{
  m_lockReason = reason;

  if (!m_core.settings().interface().lockGUI()) {
    // disabling locking is like clicking on unlock immediately
    return ForceUnlocked;
  }

  // no virtual file system on Linux, so no program runs in it
  return Completed;
}
