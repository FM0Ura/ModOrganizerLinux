// FILETIME <-> Unix time conversions, Linux only (the Win32 types come from
// <uibase/win32compat.h>). Header-only.
//
// deps/archive keeps its own copy (src/linux/fileio_linux.cpp) because it does not
// link uibase.

#ifndef UIBASE_LINUX_FILETIME_H
#define UIBASE_LINUX_FILETIME_H

#include <uibase/win32compat.h>

#include <cstdint>
#include <ctime>

namespace MOBase::linux
{

// 100 ns intervals in a second
inline constexpr std::int64_t FileTimeTicksPerSecond = 10'000'000;

// seconds between 1601-01-01 (FILETIME) and 1970-01-01 (Unix time)
inline constexpr std::int64_t FileTimeEpochOffset = 11'644'473'600;

// the 64-bit value of a FILETIME: 100 ns intervals since 1601-01-01 UTC
inline std::uint64_t fileTimeTicks(FILETIME ft)
{
  return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) |
         static_cast<std::uint64_t>(ft.dwLowDateTime);
}

// nanoseconds are truncated to 100 ns; times before 1970 work as long as they are after
// 1601
inline FILETIME toFileTime(const timespec& t)
{
  const auto v = static_cast<std::uint64_t>(
      (static_cast<std::int64_t>(t.tv_sec) + FileTimeEpochOffset) *
          FileTimeTicksPerSecond +
      static_cast<std::int64_t>(t.tv_nsec) / 100);

  FILETIME ft;
  ft.dwLowDateTime  = static_cast<DWORD>(v & 0xffffffff);
  ft.dwHighDateTime = static_cast<DWORD>(v >> 32);

  return ft;
}

inline timespec toTimespec(FILETIME ft)
{
  const auto v = fileTimeTicks(ft);

  timespec t;
  t.tv_sec = static_cast<time_t>(static_cast<std::int64_t>(v / FileTimeTicksPerSecond) -
                                 FileTimeEpochOffset);
  t.tv_nsec = static_cast<long>((v % FileTimeTicksPerSecond) * 100);

  return t;
}

}  // namespace MOBase::linux

#endif  // UIBASE_LINUX_FILETIME_H
