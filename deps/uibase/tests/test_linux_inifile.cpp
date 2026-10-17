// INI reader (uibase/linux/inifile.h). Only built on Linux.

#include <gtest/gtest.h>

#include <cerrno>
#include <string>

#include <QFile>
#include <QTemporaryDir>

#include <uibase/linux/inifile.h>

using namespace MOBase::linux;

namespace
{

IniFile ini(const char* text)
{
  return IniFile::parse(QByteArray(text));
}

// getPrivateProfileString() of a key, as UTF-8
std::string get(const QString& file, const wchar_t* section, const wchar_t* key,
                const wchar_t* def = L"")
{
  wchar_t buffer[256];
  const DWORD n = getPrivateProfileString(section, key, def, buffer, 256,
                                          file.toStdWString().c_str());
  return QString::fromWCharArray(buffer, static_cast<qsizetype>(n)).toStdString();
}

}  // namespace

TEST(LinuxIniFile, ExistingKey)
{
  const auto i = ini("[General]\nname=value\n");
  EXPECT_EQ(i.value("General", "name"), QString("value"));
}

TEST(LinuxIniFile, MissingKeyAndSection)
{
  const auto i = ini("[General]\nname=value\n");
  EXPECT_FALSE(i.value("General", "other"));
  EXPECT_FALSE(i.value("Other", "name"));
  EXPECT_TRUE(i.keyNames("Other").isEmpty());
}

TEST(LinuxIniFile, NamesAreCaseInsensitiveAndTrimmed)
{
  const auto i = ini("[ General ]\n  Name  =  value  \n");
  EXPECT_EQ(i.value("general", "NAME"), QString("value"));
}

TEST(LinuxIniFile, Quotes)
{
  const auto i = ini("[s]\na=\"x y\"\nb='z'\nc=\"open\nd=\"\"\n");
  EXPECT_EQ(i.value("s", "a"), QString("x y"));
  EXPECT_EQ(i.value("s", "b"), QString("z"));
  EXPECT_EQ(i.value("s", "c"), QString("\"open"));
  EXPECT_EQ(i.value("s", "d"), QString(""));
}

TEST(LinuxIniFile, Comments)
{
  // ';' starts a comment line; '#' does not (a key named "#c")
  const auto i = ini("[s]\n; a=1\n#c=2\nb=3\n");
  EXPECT_FALSE(i.value("s", "a"));
  EXPECT_EQ(i.value("s", "#c"), QString("2"));
  EXPECT_EQ(i.value("s", "b"), QString("3"));
}

TEST(LinuxIniFile, CrLf)
{
  const auto i = ini("[s]\r\na=1\r\nb=2\r\n");
  EXPECT_EQ(i.value("s", "a"), QString("1"));
  EXPECT_EQ(i.value("s", "b"), QString("2"));
  EXPECT_EQ(i.sectionNames(), QStringList{"s"});
}

TEST(LinuxIniFile, RepeatedKeyFirstWins)
{
  const auto i = ini("[s]\na=1\na=2\n[S]\na=3\nb=4\n");
  EXPECT_EQ(i.value("s", "a"), QString("1"));
  EXPECT_EQ(i.value("s", "b"), QString("4"));
  EXPECT_EQ(i.sectionNames(), QStringList{"s"});
}

TEST(LinuxIniFile, KeysOutsideSectionIgnoredAndKeyWithoutEquals)
{
  const auto i = ini("a=1\n[s]\nflag\n");
  EXPECT_FALSE(i.value("", "a"));
  EXPECT_EQ(i.value("s", "flag"), QString(""));
}

TEST(LinuxIniFile, Encodings)
{
  EXPECT_EQ(IniFile::parse("\xef\xbb\xbf[s]\na=\xc3\xa9\n").value("s", "a"),
            QString::fromUtf8("\xc3\xa9"));
  // invalid UTF-8 is Latin-1
  EXPECT_EQ(IniFile::parse("[s]\na=\xe9\n").value("s", "a"), QString(QChar(0xe9)));
  // UTF-16LE with a BOM
  const QByteArray u16("\xff\xfe[\0s\0]\0\n\0a\0=\0\x31\0", 16);
  EXPECT_EQ(IniFile::parse(u16).value("s", "a"), QString("1"));
}

TEST(LinuxIniFile, LoadMissingFile)
{
  errno = 0;
  EXPECT_FALSE(IniFile::load("/nonexistent/file.ini"));
  EXPECT_EQ(errno, ENOENT);
}

TEST(LinuxIniFile, GetPrivateProfileString)
{
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString path = dir.filePath("t.ini");

  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.write("[A]\r\nk=v\r\nz = \"q\"\r\n[B]\r\nx=1\r\n");
  f.close();

  EXPECT_EQ(get(path, L"a", L"K"), "v");
  EXPECT_EQ(get(path, L"A", L"z"), "q");
  EXPECT_EQ(get(path, L"A", L"missing", L"def  "), "def");
  EXPECT_EQ(get(path, L"Nope", L"k", L"d"), "d");
  EXPECT_EQ(get("/nonexistent.ini", L"A", L"k", L"d"), "d");

  // lists
  wchar_t buffer[64] = {};
  DWORD n            = getPrivateProfileString(nullptr, nullptr, nullptr, buffer, 64,
                                               path.toStdWString().c_str());
  EXPECT_EQ(n, 4u);
  EXPECT_EQ(std::wstring(buffer, 5), std::wstring(L"A\0B\0\0", 5));

  n = getPrivateProfileString(L"A", nullptr, nullptr, buffer, 64,
                              path.toStdWString().c_str());
  EXPECT_EQ(n, 4u);
  EXPECT_EQ(std::wstring(buffer, 4), std::wstring(L"k\0z\0", 4));

  // truncation
  wchar_t small[3];
  n = getPrivateProfileString(L"A", L"k", L"", small, 1, path.toStdWString().c_str());
  EXPECT_EQ(n, 0u);
  n = getPrivateProfileString(L"A", L"z", L"", small, 3, path.toStdWString().c_str());
  EXPECT_EQ(n, 1u);

  errno = 0;
  getPrivateProfileString(L"A", L"k", L"", buffer, 64, nullptr);
  EXPECT_EQ(errno, ENOENT);
}
