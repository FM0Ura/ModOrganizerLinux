// Tests for the archive library on Linux: they open the small archives from data/ with
// the real 7z.so (path in MO2_ARCHIVE_TEST_7Z, see CMakeLists.txt) and check listing,
// extraction, UTF-8 names, corrupt archives and passwords.
//
// There is no RAR case: no free tool can create .rar files, and the Archive API does
// not expose the list of supported formats, so RAR/RAR5 extraction is covered by the
// manual checklist instead.

#include <gtest/gtest.h>

#include <archive/archive.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace
{

// Make the library under test load the 7z.so built by this project, unless the
// caller already chose one.
const int g_libraryEnv = [] {
  return ::setenv("MO2_7Z_LIBRARY", MO2_ARCHIVE_TEST_7Z, 0);
}();

// UTF-8 -> wchar_t (UTF-32 on Linux) without depending on the locale.
std::wstring widen(std::string_view utf8)
{
  std::wstring out;
  for (std::size_t i = 0; i < utf8.size();) {
    const unsigned char c = utf8[i];
    int extra;
    char32_t cp;
    if (c < 0x80) {
      cp    = c;
      extra = 0;
    } else if (c < 0xE0) {
      cp    = c & 0x1F;
      extra = 1;
    } else if (c < 0xF0) {
      cp    = c & 0x0F;
      extra = 2;
    } else {
      cp    = c & 0x07;
      extra = 3;
    }
    for (int k = 1; k <= extra && i + k < utf8.size(); ++k) {
      cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
    }
    out.push_back(static_cast<wchar_t>(cp));
    i += 1 + extra;
  }
  return out;
}

std::wstring widenPath(const fs::path& p)
{
  return widen(std::string_view(p.native()));
}

std::string slurp(const fs::path& p)
{
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

class ArchiveTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    static int counter = 0;
    m_out =
        fs::temp_directory_path() / ("mo2-archive-test-" + std::to_string(::getpid()) +
                                     "-" + std::to_string(counter++));
    fs::remove_all(m_out);
    fs::create_directories(m_out);

    m_archive = CreateArchive();
    ASSERT_TRUE(m_archive);
    ASSERT_TRUE(m_archive->isValid());
    ASSERT_EQ(m_archive->getLastError(), Archive::Error::ERROR_NONE);
  }

  void TearDown() override
  {
    m_archive.reset();
    std::error_code ec;
    fs::remove_all(m_out, ec);
  }

  static fs::path data(const char* name)
  {
    return fs::path(MO2_ARCHIVE_TEST_DATA) / name;
  }

  bool open(const char* name, Archive::PasswordCallback password = {})
  {
    return m_archive->open(widenPath(data(name)), password);
  }

  std::set<std::wstring> listing() const
  {
    std::set<std::wstring> names;
    for (const FileData* f : m_archive->getFileList()) {
      names.insert(f->getArchiveFilePath());
    }
    return names;
  }

  // Ask for every entry to be extracted under its own archive path, like the MO2
  // installer does for a plain extraction.
  bool extractAll(std::vector<std::wstring>* errors = nullptr)
  {
    for (FileData* f : m_archive->getFileList()) {
      f->addOutputFilePath(f->getArchiveFilePath());
    }
    return m_archive->extract(widenPath(m_out), Archive::ProgressCallback{},
                              Archive::FileChangeCallback{},
                              [errors](std::wstring const& e) {
                                if (errors) {
                                  errors->push_back(e);
                                }
                              });
  }

  fs::path m_out;
  std::unique_ptr<Archive> m_archive;
};

}  // namespace

TEST_F(ArchiveTest, ListsFiles)
{
  for (const char* name : {"simple.7z", "simple.zip"}) {
    SCOPED_TRACE(name);
    ASSERT_TRUE(open(name)) << "last error: "
                            << static_cast<int>(m_archive->getLastError());
    const auto names = listing();
    EXPECT_TRUE(names.count(L"a.txt"));
    EXPECT_TRUE(names.count(L"dir/B.txt"));
    m_archive->close();
  }
}

TEST_F(ArchiveTest, ExtractsContent)
{
  for (const char* name : {"simple.7z", "simple.zip"}) {
    SCOPED_TRACE(name);
    const fs::path out = m_out / name;
    fs::create_directories(out);

    auto archive = CreateArchive();
    ASSERT_TRUE(archive->open(widenPath(data(name)), {}));
    for (FileData* f : archive->getFileList()) {
      f->addOutputFilePath(f->getArchiveFilePath());
    }
    ASSERT_TRUE(archive->extract(widenPath(out), Archive::ProgressCallback{},
                                 Archive::FileChangeCallback{},
                                 Archive::ErrorCallback{}));

    EXPECT_EQ(slurp(out / "a.txt"), "hello");
    EXPECT_EQ(slurp(out / "dir" / "B.txt"), "world");
  }
}

TEST_F(ArchiveTest, BackslashPathsBecomeDirectories)
{
  // some Windows tools store "dir\B.txt" in ZIP files
  ASSERT_TRUE(open("backslash.zip"));
  const auto names = listing();
  EXPECT_TRUE(names.count(L"a.txt"));
  EXPECT_TRUE(names.count(L"dir/B.txt"));

  ASSERT_TRUE(extractAll());
  EXPECT_EQ(slurp(m_out / "a.txt"), "hello");
  EXPECT_TRUE(fs::is_directory(m_out / "dir"));
  EXPECT_EQ(slurp(m_out / "dir" / "B.txt"), "world");
}

TEST_F(ArchiveTest, UnicodeNames)
{
  ASSERT_TRUE(open("unicode.7z"));
  ASSERT_TRUE(extractAll());

  const fs::path expected = m_out / fs::path(std::u8string(u8"Ünïcödé")) /
                            fs::path(std::u8string(u8"日本.txt"));
  ASSERT_TRUE(fs::exists(expected)) << "missing " << expected.string();
  EXPECT_EQ(slurp(expected), "unicode");

  // the names on disk are UTF-8, not mangled by the locale
  std::set<std::string> top;
  for (const auto& e : fs::directory_iterator(m_out)) {
    top.insert(e.path().filename().string());
  }
  EXPECT_EQ(top, std::set<std::string>{"\xC3\x9C"
                                       "n\xC3\xAF"
                                       "c\xC3\xB6"
                                       "d\xC3\xA9"});
}

TEST_F(ArchiveTest, CorruptFails)
{
  bool ok = open("corrupt.7z");
  if (ok) {
    ok = extractAll();
  }
  EXPECT_FALSE(ok);
  EXPECT_NE(m_archive->getLastError(), Archive::Error::ERROR_NONE);
}

TEST_F(ArchiveTest, MissingArchiveFails)
{
  EXPECT_FALSE(open("does-not-exist.7z"));
  EXPECT_EQ(m_archive->getLastError(), Archive::Error::ERROR_ARCHIVE_NOT_FOUND);
}

TEST_F(ArchiveTest, PasswordCallback)
{
  int asked = 0;

  {
    // right password: lists and extracts (the fixture also encrypts its headers)
    ASSERT_TRUE(open("password.7z", [&] {
      ++asked;
      return std::wstring(L"mo2");
    }));
    EXPECT_GT(asked, 0);
    ASSERT_TRUE(extractAll());
    EXPECT_EQ(slurp(m_out / "a.txt"), "hello");
    EXPECT_EQ(slurp(m_out / "dir" / "B.txt"), "world");
    m_archive->close();
  }

  fs::remove_all(m_out);
  fs::create_directories(m_out);

  {
    // empty password: must fail cleanly, without crashing or producing the content
    std::vector<std::wstring> errors;
    bool ok = open("password.7z", [] {
      return std::wstring();
    });
    if (ok) {
      ok = extractAll(&errors) && errors.empty();
    }
    EXPECT_FALSE(ok);
    EXPECT_FALSE(fs::exists(m_out / "a.txt") && slurp(m_out / "a.txt") == "hello");
  }
}

// 7-Zip keeps the names of non-BMP characters as UTF-16 surrogate pairs even in its
// 32-bit wchar_t strings; the API must hand out (and write to disk) real code points.
TEST_F(ArchiveTest, NonBmpNames)
{
  ASSERT_TRUE(open("emoji.zip"));

  const std::wstring expected = L"smile_\U0001F600.txt";
  ASSERT_EQ(m_archive->getFileList().size(), 1u);
  const std::wstring name = m_archive->getFileList()[0]->getArchiveFilePath();
  EXPECT_EQ(name, expected);
  ASSERT_EQ(name.size(), 11u);  // one wchar_t for the emoji, not two surrogates

  ASSERT_TRUE(extractAll());
  std::set<std::string> names;
  for (const auto& e : fs::directory_iterator(m_out)) {
    names.insert(e.path().filename().string());
  }
  EXPECT_EQ(names, std::set<std::string>{"smile_\xF0\x9F\x98\x80.txt"});
  EXPECT_EQ(slurp(m_out / "smile_\xF0\x9F\x98\x80.txt"), "emoji");
}

TEST_F(ArchiveTest, PasswordWithNonBmpCharacter)
{
  // password.7z-like archive protected with "pä<U+1F600>mo2" (see make_fixtures.sh)
  ASSERT_TRUE(open("password-emoji.7z", [] {
    return std::wstring(L"p\u00E4\U0001F600mo2");
  }));
  ASSERT_TRUE(extractAll());
  EXPECT_EQ(slurp(m_out / "a.txt"), "hello");
}

TEST_F(ArchiveTest, WrongNonBmpPasswordFails)
{
  bool ok = open("password-emoji.7z", [] {
    return std::wstring(L"p\u00E4mo2");
  });
  if (ok) {
    ok = extractAll();
  }
  EXPECT_FALSE(ok);
}

// Permissions stored in the archive are applied masked by the umask, without
// setuid/setgid/sticky bits and only for regular files and directories. The owner
// always keeps read/write (and search for directories) access.
TEST_F(ArchiveTest, UnixModesAreMaskedAndSafe)
{
  const mode_t um = ::umask(0);
  ::umask(um);

  ASSERT_TRUE(open("modes.zip"));
  ASSERT_TRUE(extractAll());

  auto modeOf = [&](const char* name) {
    struct stat st;
    EXPECT_EQ(::lstat((m_out / name).c_str(), &st), 0) << name;
    return st;
  };
  const mode_t special = S_ISUID | S_ISGID | S_ISVTX;

  struct Expect
  {
    const char* name;
    mode_t stored;
    mode_t extra;  // bits always granted to the owner
  };
  for (const Expect& e :
       {Expect{"exec.sh", 0755, 0600}, Expect{"private.txt", 0600, 0600},
        Expect{"readonly.txt", 0444, 0600}, Expect{"suid.bin", 0755, 0600},
        Expect{"sticky.txt", 0777, 0600}, Expect{"bin", 0700, 0700},
        Expect{"open", 0777, 0700}}) {
    SCOPED_TRACE(e.name);
    const auto st = modeOf(e.name);
    EXPECT_EQ(st.st_mode & special, 0u);
    EXPECT_EQ(st.st_mode & 0777, (e.stored & ~um) | e.extra);
  }

  // the FIFO entry is extracted as a plain file whose stored mode is ignored
  const auto fifo = modeOf("fifo");
  EXPECT_TRUE(S_ISREG(fifo.st_mode));
  EXPECT_EQ(fifo.st_mode & (special | 0777), static_cast<mode_t>(0666 & ~um));
}

TEST(ArchiveLibraryTest, MissingLibraryInvalidatesArchive)
{
  const char* previous    = ::getenv("MO2_7Z_LIBRARY");
  const std::string saved = previous ? previous : "";

  ::setenv("MO2_7Z_LIBRARY", "/nonexistent/7z.so", 1);
  auto archive = CreateArchive();
  EXPECT_FALSE(archive->isValid());
  EXPECT_EQ(archive->getLastError(), Archive::Error::ERROR_LIBRARY_NOT_FOUND);
  EXPECT_FALSE(
      archive->open(widenPath(fs::path(MO2_ARCHIVE_TEST_DATA) / "simple.7z"), {}));

  if (previous) {
    ::setenv("MO2_7Z_LIBRARY", saved.c_str(), 1);
  } else {
    ::unsetenv("MO2_7Z_LIBRARY");
  }
}
