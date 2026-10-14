/*
Mod Organizer shared UI functionality

This library is free software; you can
 * redistribute it and/or
modify it under the terms of the GNU Lesser General
 * Public
License as published by the Free Software Foundation; either
version 3 of the
 * License, or (at your option) any later version.

This library is distributed in the
 * hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied
 * warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser
 * General Public License for more details.

You should have received a copy of the GNU
 * Lesser General Public
License along with this library; if not, write to the Free
 * Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
*/

// The Win32 types and constants that appear in the public uibase API, for systems
// other than Windows. Only types and constants live here, never functions: the Linux
// implementations of the functions taking these types are in src/linux.
//
// Error codes passed around as DWORD (shell::Result::error(), formatSystemMessage(),
// errorCodeName(), ...) are errno values on Linux.
//
// On Windows this header is empty, <Windows.h> is used instead.

#ifndef UIBASE_WIN32COMPAT_H
#define UIBASE_WIN32COMPAT_H

#ifndef _WIN32

#include <climits>
#include <cstdint>

using BYTE     = std::uint8_t;
using WORD     = std::uint16_t;
using DWORD    = std::uint32_t;
using LPDWORD  = DWORD*;
using LONG     = std::int32_t;
using UINT     = unsigned int;
using BOOL     = int;
using HANDLE   = void*;
using HWND     = void*;
using HRESULT  = std::int32_t;
using NTSTATUS = std::int32_t;
using LPCWSTR  = const wchar_t*;

// a macro, like on Windows (a reinterpret_cast is not a constant expression)
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1)))

inline constexpr DWORD MAX_PATH             = 260;
inline constexpr DWORD ERROR_SUCCESS        = 0;
inline constexpr DWORD ERROR_FILE_NOT_FOUND = 2;
inline constexpr DWORD ERROR_ACCESS_DENIED  = 5;

struct FILETIME
{
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
};

struct SYSTEMTIME
{
  WORD wYear;
  WORD wMonth;
  WORD wDayOfWeek;
  WORD wDay;
  WORD wHour;
  WORD wMinute;
  WORD wSecond;
  WORD wMilliseconds;
};

struct GUID
{
  std::uint32_t Data1;
  std::uint16_t Data2;
  std::uint16_t Data3;
  std::uint8_t Data4[8];
};

constexpr bool operator==(const GUID& a, const GUID& b)
{
  if (a.Data1 != b.Data1 || a.Data2 != b.Data2 || a.Data3 != b.Data3) {
    return false;
  }

  for (int i = 0; i < 8; ++i) {
    if (a.Data4[i] != b.Data4[i]) {
      return false;
    }
  }

  return true;
}

using KNOWNFOLDERID    = GUID;
using REFKNOWNFOLDERID = const GUID&;

// the known folders supported by getKnownFolder(), with the official Windows GUIDs
inline constexpr KNOWNFOLDERID FOLDERID_Desktop = {
    0xb4bfcc3a, 0xdb2c, 0x424c, {0xb0, 0x29, 0x7f, 0xe9, 0x9a, 0x87, 0xc6, 0x41}};
inline constexpr KNOWNFOLDERID FOLDERID_Documents = {
    0xfdd39ad0, 0x238f, 0x46af, {0xad, 0xb4, 0x6c, 0x85, 0x48, 0x03, 0x69, 0xc7}};
inline constexpr KNOWNFOLDERID FOLDERID_Downloads = {
    0x374de290, 0x123f, 0x4565, {0x91, 0x64, 0x39, 0xc4, 0x92, 0x5e, 0x46, 0x7b}};
inline constexpr KNOWNFOLDERID FOLDERID_LocalAppData = {
    0xf1b32785, 0x6fba, 0x4fcf, {0x9d, 0x55, 0x7b, 0x8e, 0x7f, 0x15, 0x70, 0x91}};
inline constexpr KNOWNFOLDERID FOLDERID_RoamingAppData = {
    0x3eb685db, 0x65f9, 0x4cf6, {0xa0, 0x3a, 0xe3, 0xef, 0x65, 0x72, 0x9f, 0x3d}};
inline constexpr KNOWNFOLDERID FOLDERID_ProgramFiles = {
    0x905e63b6, 0xc1bf, 0x494e, {0xb2, 0x9c, 0x65, 0xb7, 0x32, 0xd3, 0xd2, 0x1a}};
inline constexpr KNOWNFOLDERID FOLDERID_ProgramFilesX86 = {
    0x7c5a40ef, 0xa0fb, 0x4bfc, {0x87, 0x4a, 0xc0, 0xf2, 0xe0, 0xb9, 0xfa, 0x8e}};
inline constexpr KNOWNFOLDERID FOLDERID_StartMenu = {
    0x625b53c3, 0xab48, 0x4ec1, {0xba, 0x1f, 0xa1, 0xef, 0x41, 0x46, 0xfc, 0x19}};
inline constexpr KNOWNFOLDERID FOLDERID_Windows = {
    0xf38bf404, 0x1d43, 0x42f2, {0x93, 0x05, 0x67, 0xde, 0x0b, 0x28, 0xfc, 0x23}};
inline constexpr KNOWNFOLDERID FOLDERID_OneDrive = {
    0xa52bba46, 0xe9e1, 0x435f, {0xb3, 0xd9, 0x28, 0xda, 0xa6, 0x48, 0xc0, 0xf6}};

// only used as an (always null) pointer member of TaskProgressManager
struct ITaskbarList3;

#endif  // _WIN32

#endif  // UIBASE_WIN32COMPAT_H
