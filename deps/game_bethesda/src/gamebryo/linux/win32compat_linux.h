// The Win32 types, constants and functions that the shared sources of game_bethesda use
// outside of _WIN32 blocks, for Linux. This header is included before every source file
// of game_gamebryo and of the libraries and plugins that link it (-include, see
// src/gamebryo/CMakeLists.txt), after <uibase/win32compat.h>, which has the Win32 types
// and constants of the uibase API.
//
// Each function keeps its Windows meaning in Linux terms; the ones that are not inline
// are in win32compat_linux.cpp.

#ifndef GAMEBRYO_WIN32COMPAT_LINUX_H
#define GAMEBRYO_WIN32COMPAT_LINUX_H

#ifdef __cplusplus

#include <uibase/win32compat.h>

#include <cstdint>
#include <cwchar>

using WCHAR     = wchar_t;
using PWSTR     = wchar_t*;
using LPVOID    = void*;
using ULONGLONG = std::uint64_t;

// the shared sources name the structure tag (struct _SYSTEMTIME), like Windows
#define _SYSTEMTIME SYSTEMTIME

union ULARGE_INTEGER
{
  struct
  {
    DWORD LowPart;
    DWORD HighPart;
  };
  ULONGLONG QuadPart;
};
#define _ULARGE_INTEGER ULARGE_INTEGER

// registry keys: there is no registry on Linux, every lookup fails (see
// GameGamebryo::findInRegistry() in gamegamebryo_linux.cpp)
struct HKEY__;
using HKEY = HKEY__*;
#define HKEY_CURRENT_USER                                                              \
  (reinterpret_cast<HKEY>(static_cast<std::uintptr_t>(0x80000001)))
#define HKEY_LOCAL_MACHINE                                                             \
  (reinterpret_cast<HKEY>(static_cast<std::uintptr_t>(0x80000002)))

inline constexpr KNOWNFOLDERID FOLDERID_ProgramData = {
    0x62ab5d82, 0xfdc1, 0x4dc3, {0xa9, 0xdd, 0x07, 0x0d, 0x1d, 0x49, 0x5d, 0x97}};

// the fixed part of a version resource
struct VS_FIXEDFILEINFO
{
  DWORD dwSignature;
  DWORD dwStrucVersion;
  DWORD dwFileVersionMS;
  DWORD dwFileVersionLS;
  DWORD dwProductVersionMS;
  DWORD dwProductVersionLS;
  DWORD dwFileFlagsMask;
  DWORD dwFileFlags;
  DWORD dwFileOS;
  DWORD dwFileType;
  DWORD dwFileSubtype;
  DWORD dwFileDateMS;
  DWORD dwFileDateLS;
};

// reads INI files like Windows does (the rules of MOBase::WriteRegistryValue() on
// Linux): section and key names are case-insensitive, values are trimmed and unquoted,
// a missing file, section or key gives the default value; a null appName lists the
// section names, a null keyName the keys of the section (null-separated, with an extra
// null at the end); returns the number of characters written without the last null
DWORD GetPrivateProfileStringW(LPCWSTR appName, LPCWSTR keyName, LPCWSTR defaultValue,
                               wchar_t* returnedString, DWORD size, LPCWSTR fileName);

// replaces the keys of a section by `keys` (null-separated "key=value" strings ending
// with an extra null); a null `keys` removes the section
BOOL WritePrivateProfileSectionW(LPCWSTR appName, LPCWSTR keys, LPCWSTR fileName);

// FILETIME (100-nanosecond intervals since 1601-01-01 UTC) to SYSTEMTIME, UTC; fails
// for times after 30827 like on Windows
BOOL FileTimeToSystemTime(const FILETIME* fileTime, SYSTEMTIME* systemTime);

// the version resource of PE executables, as far as VerQueryValue(block, L"\\") goes:
// the block holds the VS_FIXEDFILEINFO of the file (from MOBase::getFileVersion() and
// getProductVersion()); GetFileVersionInfoSizeW() returns 0 if the file has none
DWORD GetFileVersionInfoSizeW(LPCWSTR fileName, LPDWORD handle);
BOOL GetFileVersionInfoW(LPCWSTR fileName, DWORD handle, DWORD size, LPVOID data);
BOOL VerQueryValue(const void* block, LPCWSTR subBlock, LPVOID* buffer, UINT* length);

#endif  // __cplusplus

#endif  // GAMEBRYO_WIN32COMPAT_LINUX_H
