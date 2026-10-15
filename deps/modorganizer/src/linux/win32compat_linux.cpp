// Linux implementations of the Win32 functions declared in win32compat_linux.h.

#include "win32compat_linux.h"

#include <QFile>
#include <QString>
#include <QStringDecoder>
#include <QStringList>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace
{

// "arg" -> "\"arg\"" with backslashes and quotes escaped (see GetCommandLineW())
std::wstring quoteArgument(const std::wstring& arg)
{
  std::wstring s = L"\"";

  for (const wchar_t c : arg) {
    if (c == L'\\' || c == L'"' || c == L'\'') {
      s += L'\\';
    }

    s += c;
  }

  s += L'"';
  return s;
}

std::wstring makeCommandLine()
{
  // the arguments as the process got them, separated by nulls
  std::ifstream in("/proc/self/cmdline", std::ios::binary);
  const std::string raw((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());

  std::wstring line;
  std::size_t start = 0;

  while (start < raw.size()) {
    std::size_t end = raw.find('\0', start);
    if (end == std::string::npos) {
      end = raw.size();
    }

    const QByteArray arg(raw.data() + start, static_cast<qsizetype>(end - start));

    if (!line.empty()) {
      line += L' ';
    }

    line += quoteArgument(QFile::decodeName(arg).toStdWString());
    start = end + 1;
  }

  return line;
}

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

}  // namespace

LPCWSTR GetCommandLineW()
{
  static const std::wstring line = makeCommandLine();
  return line.c_str();
}

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

DWORD GetPrivateProfileSectionNamesW(wchar_t* returnBuffer, DWORD size,
                                     LPCWSTR fileName)
{
  return GetPrivateProfileStringW(nullptr, nullptr, nullptr, returnBuffer, size,
                                  fileName);
}
