// Linux implementations of the Win32 functions declared in win32compat_linux.h.
//
// GetPrivateProfileStringW() follows the implementation of the application
// (modorganizer's src/linux/win32compat_linux.cpp), which plugins cannot link.

#include "win32compat_linux.h"

#include <uibase/registry.h>
#include <uibase/utility.h>

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QString>
#include <QStringDecoder>
#include <QStringList>
#include <QTime>
#include <QTimeZone>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace
{

struct IniKey
{
  QString name;
  QString value;
};

struct IniSection
{
  QString name;
  std::vector<IniKey> keys;
};

QString decodeIni(const QByteArray& data)
{
  if (data.startsWith("\xff\xfe")) {
    return QStringDecoder(QStringDecoder::Utf16LE).decode(data.mid(2));
  }

  if (data.startsWith("\xef\xbb\xbf")) {
    return QString::fromUtf8(data.mid(3));
  }

  QStringDecoder utf8(QStringDecoder::Utf8);
  const QString s = utf8.decode(data);

  if (utf8.hasError()) {
    return QString::fromLatin1(data);
  }

  return s;
}

// removes one pair of matching quotes around a trimmed value
QString unquote(const QString& value)
{
  if (value.size() >= 2 && ((value.front() == u'"' && value.back() == u'"') ||
                            (value.front() == u'\'' && value.back() == u'\''))) {
    return value.mid(1, value.size() - 2);
  }

  return value;
}

// the sections of the file, in order, with their keys; nullopt if the file can't be
// read
std::optional<std::vector<IniSection>> readIni(LPCWSTR fileName)
{
  if (!fileName) {
    errno = EINVAL;
    return std::nullopt;
  }

  QFile file(QString::fromWCharArray(fileName));
  if (!file.open(QIODevice::ReadOnly)) {
    errno = ENOENT;
    return std::nullopt;
  }

  const QString text = decodeIni(file.readAll());

  std::vector<IniSection> sections;
  IniSection* current = nullptr;

  for (const auto& rawLine : text.split(u'\n')) {
    const QString line = rawLine.trimmed();

    if (line.isEmpty() || line.startsWith(u';')) {
      continue;
    }

    if (line.startsWith(u'[')) {
      const auto close = line.indexOf(u']');
      const QString name =
          (close == -1 ? line.mid(1) : line.mid(1, close - 1)).trimmed();

      auto itor = std::find_if(sections.begin(), sections.end(), [&](auto&& s) {
        return s.name.compare(name, Qt::CaseInsensitive) == 0;
      });

      if (itor == sections.end()) {
        sections.push_back({name, {}});
        current = &sections.back();
      } else {
        // keys of a repeated section are added to the first one
        current = &*itor;
      }

      continue;
    }

    if (!current) {
      // not in a section
      continue;
    }

    const auto equals  = line.indexOf(u'=');
    const QString name = (equals == -1 ? line : line.left(equals)).trimmed();
    const QString value =
        (equals == -1 ? QString() : unquote(line.mid(equals + 1).trimmed()));

    const bool exists =
        std::any_of(current->keys.begin(), current->keys.end(), [&](auto&& k) {
          return k.name.compare(name, Qt::CaseInsensitive) == 0;
        });

    if (!exists) {
      current->keys.push_back({name, value});
    }
  }

  return sections;
}

// copies `s` and its null into `out`, truncated to `size`; returns the number of
// characters copied without the null, size - 1 if truncated
DWORD copyString(const QString& s, wchar_t* out, DWORD size)
{
  if (!out || size == 0) {
    return 0;
  }

  const std::wstring ws = s.toStdWString();
  const auto n          = std::min<std::size_t>(ws.size(), size - 1);

  std::copy_n(ws.data(), n, out);
  out[n] = L'\0';

  return static_cast<DWORD>(n);
}

// copies the strings, each followed by a null, then an extra null, truncated to
// `size`; returns the number of characters copied without the last null, size - 2
// if truncated
DWORD copyList(const QStringList& list, wchar_t* out, DWORD size)
{
  if (!out || size == 0) {
    return 0;
  }

  if (size == 1) {
    out[0] = L'\0';
    return 0;
  }

  std::wstring all;
  for (const auto& s : list) {
    all += s.toStdWString();
    all += L'\0';
  }

  if (all.size() + 1 <= size) {
    std::copy_n(all.data(), all.size(), out);
    out[all.size()] = L'\0';
    return static_cast<DWORD>(all.size());
  }

  // truncated: the last string is cut, and the list still ends with two nulls
  std::copy_n(all.data(), size - 2, out);
  out[size - 2] = L'\0';
  out[size - 1] = L'\0';

  return size - 2;
}

QString defaultString(LPCWSTR defaultValue)
{
  if (!defaultValue) {
    return {};
  }

  // Windows removes the trailing blanks of the default value
  QString s = QString::fromWCharArray(defaultValue);
  while (!s.isEmpty() && s.back().isSpace()) {
    s.chop(1);
  }

  return s;
}

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
  const auto sections = readIni(fileName);

  if (!appName) {
    QStringList names;

    if (sections) {
      for (const auto& s : *sections) {
        names.push_back(s.name);
      }
    }

    return copyList(names, returnedString, size);
  }

  const IniSection* section = nullptr;

  if (sections) {
    const QString name = QString::fromWCharArray(appName).trimmed();

    for (const auto& s : *sections) {
      if (s.name.compare(name, Qt::CaseInsensitive) == 0) {
        section = &s;
        break;
      }
    }
  }

  if (!keyName) {
    QStringList names;

    if (section) {
      for (const auto& k : section->keys) {
        names.push_back(k.name);
      }
    }

    return copyList(names, returnedString, size);
  }

  if (section) {
    const QString name = QString::fromWCharArray(keyName).trimmed();

    for (const auto& k : section->keys) {
      if (k.name.compare(name, Qt::CaseInsensitive) == 0) {
        return copyString(k.value, returnedString, size);
      }
    }
  }

  errno = ENOENT;
  return copyString(defaultString(defaultValue), returnedString, size);
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
