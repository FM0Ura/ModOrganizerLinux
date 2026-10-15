// Linux implementation of setExceptionHandlers() of main.cpp (the Windows one installs
// an unhandled exception filter that writes a minidump).
//
// The crash handlers of envdump.h are process-wide signal handlers: they are installed
// once, by the first thread that asks; they write a backtrace to stderr, and to
// crash.log in the log directory once it is known (setLogDirectory() in loglist.cpp
// calls env::setCrashHandlers() again with that file).

#include "../envdump.h"
#include "../thread_utils.h"

#include <mutex>

void setExceptionHandlers()
{
  static std::once_flag once;

  std::call_once(once, [] {
    env::setCrashHandlers();
  });
}
