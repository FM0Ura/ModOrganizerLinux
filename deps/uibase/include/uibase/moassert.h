#ifndef UIBASE_MOASSERT_INCLUDED
#define UIBASE_MOASSERT_INCLUDED

#include "log.h"

#ifndef _WIN32
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <string>
#endif

namespace MOBase
{

#ifndef _WIN32
namespace details
{

  // whether a debugger is attached: /proc/self/status has a non-zero TracerPid
  inline bool debuggerAttached()
  {
    std::ifstream status("/proc/self/status");
    std::string line;

    while (std::getline(status, line)) {
      if (line.rfind("TracerPid:", 0) == 0) {
        return std::atoi(line.c_str() + 10) != 0;
      }
    }

    return false;
  }

}  // namespace details
#endif

template <class T>
inline void MOAssert(T&& t, const char* exp, const char* file, int line,
                     const char* func)
{
  if (!t) {
    log::error("assertion failed: {}:{} {}: '{}'", file, line, func, exp);

#ifdef _WIN32
    if (IsDebuggerPresent()) {
      DebugBreak();
    }
#else
    if (details::debuggerAttached()) {
      std::raise(SIGTRAP);
    }
#endif
  }
}

}  // namespace MOBase

#ifdef _WIN32
#define MO_ASSERT(v) MOAssert(v, #v, __FILE__, __LINE__, __FUNCSIG__)
#else
#define MO_ASSERT(v) MOAssert(v, #v, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#endif

#endif  // UIBASE_MOASSERT_INCLUDED
