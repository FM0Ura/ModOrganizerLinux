// Linux implementations of the Win32 functions declared in win32compat_linux.h.
//
// GetPrivateProfileStringW() is MOBase::linux::getPrivateProfileString() of uibase.

#include "win32compat_linux.h"

#include <uibase/linux/inifile.h>
#include <uibase/registry.h>
#include <uibase/utility.h>

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QTimeZone>

#include <cerrno>
#include <cstring>
#include <optional>
#include <string>

namespace
{

// "a.b.c.d" to the two DWORDs of a VS_FIXEDFILEINFO version, false if `s` is not a
// version of four numbers
bool parseVersion(const QString& s, DWORD& ms, DWORD& ls)
{
  const QStringList parts = s.trimmed().split(u'.');
  if (parts.size() != 4) {
    return false;
  }

  DWORD v[4];
  for (int i = 0; i < 4; ++i) {
    bool ok = false;
    uint n  = parts[i].toUInt(&ok);
    if (!ok || n > 0xffff) {
      return false;
    }
    v[i] = n;
  }

  ms = (v[0] << 16) | v[1];
  ls = (v[2] << 16) | v[3];

  return true;
}

std::optional<VS_FIXEDFILEINFO> fixedFileInfo(LPCWSTR fileName)
{
  if (!fileName) {
    return std::nullopt;
  }

  const QString path = QString::fromWCharArray(fileName);

  VS_FIXEDFILEINFO info = {};
  info.dwSignature      = 0xfeef04bd;
  info.dwStrucVersion   = 0x00010000;

  if (!parseVersion(MOBase::getFileVersion(path), info.dwFileVersionMS,
                    info.dwFileVersionLS)) {
    return std::nullopt;
  }

  // the product version of the string table, when it is a plain version
  if (!parseVersion(MOBase::getProductVersion(path), info.dwProductVersionMS,
                    info.dwProductVersionLS)) {
    info.dwProductVersionMS = info.dwFileVersionMS;
    info.dwProductVersionLS = info.dwFileVersionLS;
  }

  return info;
}

}  // namespace

DWORD GetPrivateProfileStringW(LPCWSTR appName, LPCWSTR keyName, LPCWSTR defaultValue,
                               wchar_t* returnedString, DWORD size, LPCWSTR fileName)
{
  return MOBase::linux::getPrivateProfileString(appName, keyName, defaultValue,
                                                returnedString, size, fileName);
}

BOOL WritePrivateProfileSectionW(LPCWSTR appName, LPCWSTR keys, LPCWSTR fileName)
{
  if (!appName || !fileName) {
    errno = EINVAL;
    return 0;
  }

  // remove the section, then write the new keys (WriteRegistryValue() has the
  // WritePrivateProfileStringW semantics)
  if (!MOBase::WriteRegistryValue(appName, nullptr, nullptr, fileName)) {
    return 0;
  }

  for (const wchar_t* p = keys; p && *p; p += std::wcslen(p) + 1) {
    const std::wstring pair(p);
    const auto equals = pair.find(L'=');
    if (equals == std::wstring::npos) {
      continue;
    }

    const std::wstring key   = pair.substr(0, equals);
    const std::wstring value = pair.substr(equals + 1);

    if (!MOBase::WriteRegistryValue(appName, key.c_str(), value.c_str(), fileName)) {
      return 0;
    }
  }

  return 1;
}

BOOL FileTimeToSystemTime(const FILETIME* fileTime, SYSTEMTIME* systemTime)
{
  if (!fileTime || !systemTime) {
    errno = EINVAL;
    return 0;
  }

  const std::uint64_t ticks =
      (static_cast<std::uint64_t>(fileTime->dwHighDateTime) << 32) |
      fileTime->dwLowDateTime;

  // Windows rejects times with the high bit set
  if (ticks >= 0x8000000000000000ULL) {
    errno = EINVAL;
    return 0;
  }

  const std::uint64_t ms = ticks / 10000;
  const QDateTime epoch(QDate(1601, 1, 1), QTime(0, 0), QTimeZone::UTC);
  const QDateTime t = epoch.addMSecs(static_cast<qint64>(ms));

  if (!t.isValid() || t.date().year() > 30827) {
    errno = EINVAL;
    return 0;
  }

  systemTime->wYear         = static_cast<WORD>(t.date().year());
  systemTime->wMonth        = static_cast<WORD>(t.date().month());
  systemTime->wDayOfWeek    = static_cast<WORD>(t.date().dayOfWeek() % 7);  // Sunday 0
  systemTime->wDay          = static_cast<WORD>(t.date().day());
  systemTime->wHour         = static_cast<WORD>(t.time().hour());
  systemTime->wMinute       = static_cast<WORD>(t.time().minute());
  systemTime->wSecond       = static_cast<WORD>(t.time().second());
  systemTime->wMilliseconds = static_cast<WORD>(t.time().msec());

  return 1;
}

DWORD GetFileVersionInfoSizeW(LPCWSTR fileName, LPDWORD handle)
{
  if (handle) {
    *handle = 0;
  }

  if (!fixedFileInfo(fileName)) {
    errno = ENOENT;
    return 0;
  }

  return sizeof(VS_FIXEDFILEINFO);
}

BOOL GetFileVersionInfoW(LPCWSTR fileName, DWORD, DWORD size, LPVOID data)
{
  const auto info = fixedFileInfo(fileName);
  if (!info || !data || size < sizeof(VS_FIXEDFILEINFO)) {
    errno = info ? EINVAL : ENOENT;
    return 0;
  }

  std::memcpy(data, &*info, sizeof(VS_FIXEDFILEINFO));
  return 1;
}

BOOL VerQueryValue(const void* block, LPCWSTR subBlock, LPVOID* buffer, UINT* length)
{
  // only the root block, the VS_FIXEDFILEINFO, is available
  if (!block || !subBlock || std::wcscmp(subBlock, L"\\") != 0 || !buffer) {
    return 0;
  }

  *buffer = const_cast<void*>(block);
  if (length) {
    *length = sizeof(VS_FIXEDFILEINFO);
  }

  return 1;
}
