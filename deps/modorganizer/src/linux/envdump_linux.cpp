// Linux implementations of the crash dumps of envdump.h (on Windows, coredump() and
// coredumpOther() are in env.cpp and write minidumps).
//
// Mod Organizer doesn't write dump files on Linux: the handlers installed by
// setCrashHandlers() write a backtrace to stderr and to the crash log file, then
// re-raise the signal with its default action so that the system writes its own core
// dump (a core file or systemd-coredump, depending on its configuration).
// CoreDumpTypes is kept for the settings but has no effect.
//
// The handlers only do async-signal-safe things: the crash log is opened by
// setCrashHandlers(), not in the handler, the backtrace is written with
// backtrace_symbols_fd() (backtrace() is called once beforehand, so that libgcc is
// already loaded and it doesn't allocate), and nothing goes through spdlog or malloc,
// which could be the cause of the crash (heap corruption, a crash while logging) and
// would deadlock. In case something blocks anyway (a write to a hung file system), a
// watchdog alarm ends the process with the original signal.

#include "../envdump.h"
#include "../envmodule.h"

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

#include <uibase/log.h>

using namespace MOBase;

namespace env
{

namespace
{

  constexpr int MaxFrames = 128;

  // seconds before the watchdog ends a crash handler that blocks
  constexpr unsigned int WatchdogSeconds = 10;

  // the signals of a crash
  constexpr int CrashSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};

  // descriptor of the crash log, -1 if none
  std::atomic<int> g_logFd{-1};

  // the signal being handled, for the watchdog
  volatile sig_atomic_t g_crashSignal = 0;

  // writes to a descriptor, async-signal-safe
  //
  void writeFd(int fd, const char* s)
  {
    [[maybe_unused]] const auto r = ::write(fd, s, std::strlen(s));
  }

  // writes " (signal N)" to `out`, async-signal-safe
  //
  void signalText(int sig, char* out, std::size_t size)
  {
    char digits[16];
    int n = 0;

    unsigned int v = static_cast<unsigned int>(sig);
    do {
      digits[n++] = static_cast<char>('0' + (v % 10));
      v /= 10;
    } while (v != 0 && n < 15);

    std::size_t i = 0;
    for (const char c : std::string_view(" (signal ")) {
      if (i + 1 < size) {
        out[i++] = c;
      }
    }

    while (n > 0 && i + 1 < size) {
      out[i++] = digits[--n];
    }

    if (i + 1 < size) {
      out[i++] = ')';
    }

    out[i] = 0;
  }

  // writes the backtrace of the current thread to stderr, then to the crash log (all
  // of it to stderr first, in case writing to the log blocks), async-signal-safe
  // once backtrace() has been called once
  //
  void writeBacktrace(const char* what)
  {
    void* frames[MaxFrames];
    const int n = ::backtrace(frames, MaxFrames);

    const int fds[] = {STDERR_FILENO, g_logFd.load()};

    for (const int fd : fds) {
      if (fd == -1) {
        continue;
      }

      writeFd(fd, "ModOrganizer has crashed");
      writeFd(fd, what);
      writeFd(fd, ", backtrace:\n");
      ::backtrace_symbols_fd(frames, n, fd);
    }

    if (fds[1] != -1) {
      ::fsync(fds[1]);
    }
  }

  // ends the process with the signal being handled, async-signal-safe
  //
  [[noreturn]] void terminateWith(int sig)
  {
    ::signal(sig, SIG_DFL);

    // the signal is blocked while its handler runs
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);
    ::pthread_sigmask(SIG_UNBLOCK, &set, nullptr);

    ::raise(sig);

    // the default action of SIGABRT, SIGSEGV, ... ends the process; just in case
    ::_exit(128 + sig);
  }

  // the crash handler took too long
  //
  void onWatchdog(int)
  {
    const int sig = g_crashSignal;

    // not to the crash log, writing to it may be what blocks
    const char msg[]              = "crash handler timed out\n";
    [[maybe_unused]] const auto r = ::write(STDERR_FILENO, msg, sizeof(msg) - 1);

    terminateWith(sig != 0 ? sig : SIGABRT);
  }

  void onCrashSignal(int sig)
  {
    // the handler was installed with SA_RESETHAND: the action is the default one
    // again, a crash in here ends the process
    g_crashSignal = sig;

    ::signal(SIGALRM, onWatchdog);
    ::alarm(WatchdogSeconds);

    char what[32];
    signalText(sig, what, sizeof(what));

    writeBacktrace(what);

    ::alarm(0);

    // delivered with the default action when the handler returns, which ends the
    // process and makes the system write a core dump; a fault (SIGSEGV, ...) would
    // also happen again on return
    ::raise(sig);
  }

}  // namespace

void setCrashHandlers(const std::filesystem::path& logFile)
{
  // backtrace() loads libgcc the first time it is called, which allocates: not in
  // the handler
  void* frames[1];
  ::backtrace(frames, 1);

  if (!logFile.empty()) {
    const int fd =
        ::open(logFile.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

    if (fd == -1) {
      log::error("can't open crash log '{}', {}", logFile.string(),
                 std::strerror(errno));
    } else {
      const int old = g_logFd.exchange(fd);
      if (old != -1) {
        ::close(old);
      }
    }
  }

  // an alternate stack for this thread, so that a stack overflow can be reported
  static std::vector<char> altStack(std::max<std::size_t>(SIGSTKSZ, 64 * 1024));

  stack_t ss     = {};
  ss.ss_sp       = altStack.data();
  ss.ss_size     = altStack.size();
  const bool alt = (::sigaltstack(&ss, nullptr) == 0);

  struct sigaction sa = {};
  sa.sa_handler       = onCrashSignal;
  sa.sa_flags         = SA_RESETHAND | (alt ? SA_ONSTACK : 0);
  sigemptyset(&sa.sa_mask);

  for (const int sig : CrashSignals) {
    if (::sigaction(sig, &sa, nullptr) != 0) {
      log::error("can't install the handler for signal {}", sig);
    }
  }
}

bool coredump(const wchar_t*, CoreDumpTypes)
{
  // not called from a signal handler, the log can be used
  void* frames[MaxFrames];
  const int n = ::backtrace(frames, MaxFrames);

  writeBacktrace("");

  if (char** symbols = ::backtrace_symbols(frames, n)) {
    log::error("ModOrganizer has crashed, backtrace:");

    for (int i = 0; i < n; ++i) {
      log::error("  {}", symbols[i]);
    }

    std::free(symbols);
  }

  std::wclog << L"the system writes the core dump, see its configuration "
                L"(coredumpctl, /proc/sys/kernel/core_pattern)\n";

  return true;
}

bool coredumpOther(CoreDumpTypes)
{
  std::wclog << L"looking for the other process...\n";

  std::error_code ec;
  const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
  const auto filename =
      (ec ? QString("ModOrganizer") : QString::fromStdString(exe.filename().string()));

  const auto thisPid = static_cast<DWORD>(::getpid());

  for (const auto& p : getRunningProcesses()) {
    if (p.name() == filename && p.pid() != thisPid) {
      std::wcerr << L"found other process with pid " << p.pid() << L"\n"
                 << L"Mod Organizer doesn't write dump files on this system, use "
                 << L"'gcore " << p.pid() << L"' (gdb) to write a core dump\n";

      return false;
    }
  }

  std::wcerr << L"no other process found\n";
  return false;
}

}  // namespace env
