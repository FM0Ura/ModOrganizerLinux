// The few Win32 functions and MSVC macros that the shared sources use outside of their
// _WIN32 blocks, for Linux; included by pch.h on Linux only (after
// <uibase/win32compat.h>, which has the Win32 types and constants of the uibase API).
//
// Each one keeps its Windows meaning in Linux terms; the functions that are not inline
// are in win32compat_linux.cpp.

#ifndef MODORGANIZER_WIN32COMPAT_LINUX_INCLUDED
#define MODORGANIZER_WIN32COMPAT_LINUX_INCLUDED

#include <uibase/win32compat.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <thread>

using UINT32 = std::uint32_t;

// the error of the last failed operation: errno, which the Linux implementations of
// uibase and of src/linux set where Windows documents ::GetLastError()
inline DWORD GetLastError()
{
  return static_cast<DWORD>(errno);
}

// shellDelete() never asks for a confirmation on Linux, nothing is ever cancelled
inline constexpr DWORD ERROR_CANCELLED = ECANCELED;

inline void Sleep(DWORD milliseconds)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// a process HANDLE is the pid on Linux (see envmodule.h)
inline DWORD GetProcessId(HANDLE process)
{
  return static_cast<DWORD>(reinterpret_cast<std::intptr_t>(process));
}

// the command line of this process, made from its arguments: each one between double
// quotes, with backslashes and quotes escaped by a backslash, which is what
// boost::program_options::split_unix() parses back (empty arguments are lost)
LPCWSTR GetCommandLineW();

// reads INI files like Windows does:
// - section and key names are matched case-insensitively, after trimming whitespace,
//   the first section or key of a name wins,
// - values are trimmed, a pair of quotes ("" or '') around a value is removed,
// - lines starting with ';' are comments,
// - files are UTF-8 (with or without a BOM), UTF-16LE with a BOM, or Latin-1 when
//   they are not valid UTF-8,
// - a null appName lists the section names, a null keyName the keys of the section,
//   as null-terminated strings followed by an extra null,
// - a missing file, section or key gives the default value (empty if null),
// - returns the number of characters written, without the terminating null; a
//   truncated result returns size - 1 (size - 2 for lists)
DWORD GetPrivateProfileStringW(LPCWSTR appName, LPCWSTR keyName, LPCWSTR defaultValue,
                               wchar_t* returnedString, DWORD size, LPCWSTR fileName);

DWORD GetPrivateProfileSectionNamesW(wchar_t* returnBuffer, DWORD size,
                                     LPCWSTR fileName);

// MSVC's name of the current function, with its signature
#define __FUNCSIG__ __PRETTY_FUNCTION__

#endif  // MODORGANIZER_WIN32COMPAT_LINUX_INCLUDED
