// The Win32 functions of src/linux/win32compat_linux.h and the Linux parts of
// shared/util.h that replace Win32 code (ToFILETIME(), createVersionInfo()).

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <boost/program_options/parsers.hpp>

#include <QString>

#include "linux/win32compat_linux.h"
#include "shared/util.h"
#include "tempdir.h"

using namespace MOShared;

namespace
{

std::vector<std::wstring> split(const wchar_t* list)
{
  std::vector<std::wstring> v;
  for (const wchar_t* p = list; *p; p += wcslen(p) + 1) {
    v.push_back(p);
  }
  return v;
}

std::wstring iniPath(const TempDir& tmp, const char* content)
{
  const auto path = tmp.path() / "test.ini";
  std::ofstream(path, std::ios::binary) << content;
  return path.wstring();
}

}  // namespace

TEST(Win32Compat, ReadsIniValues)
{
  TempDir tmp;
  const auto ini = iniPath(tmp, "; comment\r\n"
                                "[General]\r\n"
                                "  sLocalSavePath = Saves\\Profile\\  \r\n"
                                "quoted=\"  spaced  \"\r\n"
                                "empty=\r\n"
                                "[Display]\n"
                                "iSize H=1080\n"
                                "[general]\n"
                                "late=merged\n");

  wchar_t buffer[260];

  EXPECT_EQ(GetPrivateProfileStringW(L"General", L"SLocalSavePath", L"", buffer, 260,
                                     ini.c_str()),
            14u);
  EXPECT_STREQ(buffer, L"Saves\\Profile\\");

  // section and key names are case-insensitive, quotes are removed
  GetPrivateProfileStringW(L"GENERAL", L"Quoted", L"", buffer, 260, ini.c_str());
  EXPECT_STREQ(buffer, L"  spaced  ");

  EXPECT_EQ(
      GetPrivateProfileStringW(L"General", L"empty", L"def", buffer, 260, ini.c_str()),
      0u);
  EXPECT_STREQ(buffer, L"");

  // keys of a repeated section are added to the first one
  GetPrivateProfileStringW(L"General", L"late", L"", buffer, 260, ini.c_str());
  EXPECT_STREQ(buffer, L"merged");

  // missing key, section or file: the default, without its trailing blanks
  EXPECT_EQ(GetPrivateProfileStringW(L"General", L"missing", L"dflt  ", buffer, 260,
                                     ini.c_str()),
            4u);
  EXPECT_STREQ(buffer, L"dflt");

  GetPrivateProfileStringW(L"Nope", L"key", nullptr, buffer, 260, ini.c_str());
  EXPECT_STREQ(buffer, L"");

  const auto missing = (tmp.path() / "missing.ini").wstring();
  GetPrivateProfileStringW(L"General", L"key", L"x", buffer, 260, missing.c_str());
  EXPECT_STREQ(buffer, L"x");

  // truncated
  EXPECT_EQ(
      GetPrivateProfileStringW(L"Display", L"iSize H", L"", buffer, 3, ini.c_str()),
      2u);
  EXPECT_STREQ(buffer, L"10");
}

TEST(Win32Compat, ListsIniSectionsAndKeys)
{
  TempDir tmp;
  const auto ini = iniPath(
      tmp, "\xef\xbb\xbf[General]\nb=1\na=2\nB=3\n[Display]\nx=1\n[general]\nc=4\n");

  std::vector<wchar_t> buffer(1000);

  const auto n = GetPrivateProfileSectionNamesW(
      buffer.data(), static_cast<DWORD>(buffer.size()), ini.c_str());
  EXPECT_EQ(n, 16u);  // "General\0Display\0"
  EXPECT_EQ(split(buffer.data()), (std::vector<std::wstring>{L"General", L"Display"}));

  GetPrivateProfileStringW(L"general", nullptr, nullptr, buffer.data(),
                           static_cast<DWORD>(buffer.size()), ini.c_str());
  EXPECT_EQ(split(buffer.data()), (std::vector<std::wstring>{L"b", L"a", L"c"}));

  // truncated lists still end with two nulls
  EXPECT_EQ(GetPrivateProfileSectionNamesW(buffer.data(), 6, ini.c_str()), 4u);
  EXPECT_EQ(split(buffer.data()), (std::vector<std::wstring>{L"Gene"}));
}

TEST(Win32Compat, ReadsLatin1AndUtf16Ini)
{
  TempDir tmp;

  auto ini = iniPath(tmp, "[S]\nk=caf\xe9\n");
  wchar_t buffer[64];
  GetPrivateProfileStringW(L"S", L"k", L"", buffer, 64, ini.c_str());
  EXPECT_STREQ(buffer, L"café");

  ini = iniPath(tmp, "[S]\nk=caf\xc3\xa9\n");
  GetPrivateProfileStringW(L"S", L"k", L"", buffer, 64, ini.c_str());
  EXPECT_STREQ(buffer, L"café");

  const auto utf16 = (tmp.path() / "utf16.ini");
  {
    std::ofstream out(utf16, std::ios::binary);
    out.write("\xff\xfe", 2);
    for (const char c : std::string("[S]\r\nk=v\r\n")) {
      out.put(c);
      out.put('\0');
    }
  }
  GetPrivateProfileStringW(L"S", L"k", L"", buffer, 64, utf16.wstring().c_str());
  EXPECT_STREQ(buffer, L"v");
}

TEST(Win32Compat, CommandLineRoundTrips)
{
  // the arguments of this process, as the kernel has them
  std::ifstream in("/proc/self/cmdline", std::ios::binary);
  const std::string raw((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());

  std::vector<std::wstring> expected;
  for (std::size_t start = 0; start < raw.size();) {
    const auto end = raw.find('\0', start);
    const auto arg = raw.substr(start, end - start);
    if (!arg.empty()) {
      expected.push_back(QString::fromStdString(arg).toStdWString());
    }
    start = end + 1;
  }

  const std::wstring line = GetCommandLineW();
  EXPECT_EQ(boost::program_options::split_unix(line), expected);
}

TEST(Win32Compat, ErrorsAndProcesses)
{
  errno = ENOENT;
  EXPECT_EQ(GetLastError(), static_cast<DWORD>(ENOENT));

  EXPECT_EQ(GetProcessId(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(1234))),
            1234u);
}

TEST(Util, ToFileTimeIsSince1601)
{
  // 2000-01-01 00:00:00 UTC is 125911584000000000 in 100 ns since 1601-01-01
  const auto sys  = std::chrono::sys_days(std::chrono::year(2000) / 1 / 1);
  const auto file = std::chrono::file_clock::from_sys(sys);
  const auto ft   = ToFILETIME(
      std::chrono::time_point_cast<std::filesystem::file_time_type::duration>(file));

  const auto v = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) |
                 static_cast<std::uint64_t>(ft.dwLowDateTime);
  EXPECT_EQ(v, 125'911'584'000'000'000ULL);
}

TEST(Util, VersionComesFromVersionRc)
{
  // VER_FILEVERSION_STR of version.rc, "2.5.2" for example
  const auto numbers = QString::fromUtf8(MO2_VERSION_STRING).split('.');
  ASSERT_GE(numbers.size(), 3);

  const auto v = createVersionInfo();
  EXPECT_EQ(v.major(), numbers[0].toInt());
  EXPECT_EQ(v.minor(), numbers[1].toInt());
  EXPECT_EQ(v.patch(), numbers[2].toInt());
  EXPECT_GE(v.major(), 2);
}
