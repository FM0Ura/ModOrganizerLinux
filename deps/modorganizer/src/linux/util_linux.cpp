// Linux implementations of the platform-specific functions of shared/util.h (the
// Windows ones are in shared/util.cpp, which is not built on Linux).
//
// Strings: std::string is UTF-8 and std::wstring is UTF-32 on Linux. Lowercase uses
// the simple (one character to one character) Unicode mapping, like CharLowerBuffW()
// on Windows, independently of the locale.
//
// Not here yet (shared/util.cpp, see docs/superpowers/plans/phase1-compile-errors.md
// in the superproject): createVersionInfo(), getUsvfsVersionString(),
// checkDuplicateShortcuts(), ExitModOrganizer(), ModOrganizerCanCloseNow(),
// ModOrganizerExiting(), ResetExitFlag() and isNxmLink().

#include "../shared/util.h"

#include <pthread.h>
#include <sys/stat.h>

#include <algorithm>

#include <QByteArray>
#include <QChar>
#include <QString>

namespace MOShared
{

namespace
{

  wchar_t toLowerChar(wchar_t c)
  {
    if (c < 0x80) {
      return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
    }

    return static_cast<wchar_t>(QChar::toLower(static_cast<char32_t>(c)));
  }

  bool isAscii(std::string_view s)
  {
    return std::all_of(s.begin(), s.end(), [](char c) {
      return static_cast<unsigned char>(c) < 0x80;
    });
  }

  QByteArray toNative(std::wstring_view path)
  {
    return QString::fromWCharArray(path.data(), static_cast<qsizetype>(path.size()))
        .toUtf8();
  }

}  // namespace

bool FileExists(const std::string& filename)
{
  // like GetFileAttributes(), a broken symbolic link exists
  struct stat st;
  return (::lstat(filename.c_str(), &st) == 0);
}

bool FileExists(const std::wstring& filename)
{
  struct stat st;
  return (::lstat(toNative(filename).constData(), &st) == 0);
}

bool FileExists(const std::wstring& searchPath, const std::wstring& filename)
{
  return FileExists(searchPath + L"/" + filename);
}

std::string ToString(const std::wstring& source, bool)
{
  // the narrow encoding is UTF-8, whether utf8 is set or not
  return QString::fromStdWString(source).toStdString();
}

std::wstring ToWString(const std::string& source, bool)
{
  return QString::fromStdString(source).toStdWString();
}

std::string& ToLowerInPlace(std::string& text)
{
  if (isAscii(text)) {
    std::transform(text.begin(), text.end(), text.begin(), [](char c) {
      return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
    });
  } else {
    std::wstring w = ToWString(text, true);
    text           = ToString(ToLowerInPlace(w), true);
  }

  return text;
}

std::string ToLowerCopy(const std::string& text)
{
  std::string result(text);
  ToLowerInPlace(result);
  return result;
}

std::wstring& ToLowerInPlace(std::wstring& text)
{
  std::transform(text.begin(), text.end(), text.begin(), toLowerChar);
  return text;
}

std::wstring ToLowerCopy(const std::wstring& text)
{
  std::wstring result(text);
  ToLowerInPlace(result);
  return result;
}

std::wstring ToLowerCopy(std::wstring_view text)
{
  std::wstring result(text.begin(), text.end());
  ToLowerInPlace(result);
  return result;
}

bool CaseInsensitiveEqual(const std::wstring& lhs, const std::wstring& rhs)
{
  return caseInsensitiveEqual(lhs, rhs);
}

bool caseInsensitiveEqual(std::wstring_view lhs, std::wstring_view rhs)
{
  return (lhs.size() == rhs.size()) &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](wchar_t a, wchar_t b) {
           return toLowerChar(a) == toLowerChar(b);
         });
}

int caseInsensitiveCompare(std::wstring_view lhs, std::wstring_view rhs)
{
  const auto n = std::min(lhs.size(), rhs.size());

  for (std::size_t i = 0; i < n; ++i) {
    const wchar_t a = toLowerChar(lhs[i]);
    const wchar_t b = toLowerChar(rhs[i]);

    if (a != b) {
      return (a < b) ? -1 : 1;
    }
  }

  if (lhs.size() == rhs.size()) {
    return 0;
  }

  return (lhs.size() < rhs.size()) ? -1 : 1;
}

void SetThisThreadName(const QString& s)
{
  // the kernel keeps 15 bytes and the null terminator
  QString name = s;
  while (name.toUtf8().size() > 15) {
    name.chop(1);
  }

  ::pthread_setname_np(::pthread_self(), name.toUtf8().constData());
}

}  // namespace MOShared
