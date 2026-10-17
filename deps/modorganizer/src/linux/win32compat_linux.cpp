// Linux implementations of the Win32 functions declared in win32compat_linux.h.

#include "win32compat_linux.h"

#include <uibase/linux/inifile.h>

#include <QFile>
#include <QString>

#include <fstream>
#include <iterator>
#include <string>

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

}  // namespace

LPCWSTR GetCommandLineW()
{
  static const std::wstring line = makeCommandLine();
  return line.c_str();
}

DWORD GetPrivateProfileStringW(LPCWSTR appName, LPCWSTR keyName, LPCWSTR defaultValue,
                               wchar_t* returnedString, DWORD size, LPCWSTR fileName)
{
  return MOBase::linux::getPrivateProfileString(appName, keyName, defaultValue,
                                                returnedString, size, fileName);
}

DWORD GetPrivateProfileSectionNamesW(wchar_t* returnBuffer, DWORD size,
                                     LPCWSTR fileName)
{
  return GetPrivateProfileStringW(nullptr, nullptr, nullptr, returnBuffer, size,
                                  fileName);
}
