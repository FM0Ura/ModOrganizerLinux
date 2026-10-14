// Private helpers for the shared uibase sources compiled on Linux: the few Win32/CRT
// functions they call outside of the _WIN32 blocks. Never installed, never included
// by public headers.

#ifndef UIBASE_LINUX_WIN32_PRIVATE_H
#define UIBASE_LINUX_WIN32_PRIVATE_H

#include <cerrno>
#include <cstddef>
#include <cstdio>

#include "../../include/uibase/win32compat.h"

// in the global namespace, like the Win32 functions (callers use ::GetLastError())

// the error of the last failed operation; the Linux implementations of the functions
// documented as "call ::GetLastError()" set errno
inline DWORD GetLastError()
{
  return static_cast<DWORD>(errno);
}

// MSVC's bounds-checked sprintf for arrays
template <std::size_t N, class... Args>
int sprintf_s(char (&buffer)[N], const char* format, Args... args)
{
  return std::snprintf(buffer, N, format, args...);
}

inline void DebugBreak()
{
  __builtin_trap();
}

#endif  // UIBASE_LINUX_WIN32_PRIVATE_H
