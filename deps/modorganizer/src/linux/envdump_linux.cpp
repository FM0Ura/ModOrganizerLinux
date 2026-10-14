// Linux implementations of the crash dumps of envdump.h (on Windows, coredump() and
// coredumpOther() are in env.cpp and write minidumps).
//
// Mod Organizer doesn't write dump files on Linux: the handlers installed by
// setCrashHandlers() write a backtrace to stderr and to the log, then re-raise the
// signal with its default action so that the system writes its own core dump (a core
// file or systemd-coredump, depending on its configuration). CoreDumpTypes is kept for
// the settings but has no effect.

#include "../envdump.h"
#include "../envmodule.h"

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
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

  // the signals of a crash
  constexpr int CrashSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};

  void writeStderr(const char* s)
  {
    // async-signal-safe
    [[maybe_unused]] const auto r = ::write(STDERR_FILENO, s, std::strlen(s));
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

  // writes the backtrace of the current thread to stderr, then to the log
  //
  void logBacktrace(const char* what)
  {
    void* frames[MaxFrames];
    const int n = ::backtrace(frames, MaxFrames);

    writeStderr("ModOrganizer has crashed");
    writeStderr(what);
    writeStderr(", backtrace:\n");
    ::backtrace_symbols_fd(frames, n, STDERR_FILENO);

    // not async-signal-safe, but this is the last thing the process does
    if (char** symbols = ::backtrace_symbols(frames, n)) {
      log::error("ModOrganizer has crashed{}, backtrace:", what);

      for (int i = 0; i < n; ++i) {
        log::error("  {}", symbols[i]);
      }

      std::free(symbols);
    }
  }

  void onCrashSignal(int sig)
  {
    // the handler was installed with SA_RESETHAND: the action is the default one
    // again, a crash in here ends the process
    char what[32];
    signalText(sig, what, sizeof(what));

    logBacktrace(what);

    // delivered with the default action when the handler returns, which ends the
    // process and makes the system write a core dump; a fault (SIGSEGV, ...) would
    // also happen again on return
    ::raise(sig);
  }

}  // namespace

void setCrashHandlers()
{
  // backtrace() loads libgcc the first time it is called, which allocates: not in
  // the handler
  void* frames[1];
  ::backtrace(frames, 1);

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
  logBacktrace("");
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
