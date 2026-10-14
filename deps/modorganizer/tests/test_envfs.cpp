#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "envfs.h"
#include "tempdir.h"

namespace
{

std::uint64_t toUInt64(FILETIME ft)
{
  return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// records everything the walker reports; files are keyed by their path relative to
// the walked directory, built from the names given to the directory callbacks
//
struct Walk
{
  struct FileInfo
  {
    std::uint64_t size;
    std::uint64_t time;
  };

  std::vector<std::wstring> dirStack;
  std::map<std::wstring, FileInfo> files;
  std::vector<std::wstring> dirs;
  int starts      = 0;
  int ends        = 0;
  bool mismatched = false;
  int duplicates  = 0;

  std::wstring prefix() const
  {
    std::wstring s;
    for (const auto& d : dirStack) {
      s += d + L"/";
    }
    return s;
  }

  static Walk run(const std::filesystem::path& root)
  {
    Walk w;

    env::DirectoryWalker().forEachEntry(
        root.wstring(), &w,
        [](void* cx, std::wstring_view name) {
          auto* w = static_cast<Walk*>(cx);
          ++w->starts;
          w->dirs.push_back(w->prefix() + std::wstring(name));
          w->dirStack.emplace_back(name);
        },
        [](void* cx, std::wstring_view name) {
          auto* w = static_cast<Walk*>(cx);
          ++w->ends;
          if (w->dirStack.empty() || w->dirStack.back() != name) {
            w->mismatched = true;
          } else {
            w->dirStack.pop_back();
          }
        },
        [](void* cx, std::wstring_view name, FILETIME ft, std::uint64_t size) {
          auto* w        = static_cast<Walk*>(cx);
          const auto key = w->files.emplace(w->prefix() + std::wstring(name),
                                            FileInfo{size, toUInt64(ft)});
          if (!key.second) {
            ++w->duplicates;
          }
        });

    return w;
  }
};

}  // namespace

TEST(EnvFs, WalksTree)
{
  TempDir tmp;
  tmp.createFile("a.txt", 1);
  tmp.createFile("d1/b.txt", 22);
  tmp.createFile("d1/c.txt", 0);
  tmp.createFile("d1/d2/e.txt", 4096);
  tmp.createFile("d1/d2/f.txt", 70000);
  std::filesystem::create_directories(tmp.path() / "empty");

  const auto w = Walk::run(tmp.path());

  EXPECT_EQ(w.starts, 3);
  EXPECT_EQ(w.ends, 3);
  EXPECT_FALSE(w.mismatched) << "dirStart/dirEnd are not paired";
  EXPECT_TRUE(w.dirStack.empty());
  EXPECT_EQ(w.duplicates, 0);

  const std::map<std::wstring, std::uint64_t> expected = {{L"a.txt", 1},
                                                          {L"d1/b.txt", 22},
                                                          {L"d1/c.txt", 0},
                                                          {L"d1/d2/e.txt", 4096},
                                                          {L"d1/d2/f.txt", 70000}};

  ASSERT_EQ(w.files.size(), expected.size());
  for (const auto& [name, size] : expected) {
    const auto itor = w.files.find(name);
    ASSERT_NE(itor, w.files.end())
        << "missing " << QString::fromStdWString(name).toStdString();
    EXPECT_EQ(itor->second.size, size) << QString::fromStdWString(name).toStdString();
  }

  // getFilesAndDirs() builds the same tree
  const auto root = env::getFilesAndDirs(tmp.path().wstring());
  EXPECT_EQ(root.files.size(), 1u);
  EXPECT_EQ(root.dirs.size(), 2u);
}

TEST(EnvFs, FileTimeEpoch)
{
  TempDir tmp;
  const auto epoch = tmp.createFile("epoch.txt");
  const auto later = tmp.createFile("later.txt");

  // mtime = 1970-01-01 00:00:00 UTC
  const timespec times0[2] = {{0, 0}, {0, 0}};
  ASSERT_EQ(::utimensat(AT_FDCWD, epoch.c_str(), times0, 0), 0);

  // mtime = 1 s + 500 ns after the epoch
  const timespec times1[2] = {{0, 0}, {1, 500}};
  ASSERT_EQ(::utimensat(AT_FDCWD, later.c_str(), times1, 0), 0);

  const auto w = Walk::run(tmp.path());

  ASSERT_TRUE(w.files.contains(L"epoch.txt"));
  EXPECT_EQ(w.files.at(L"epoch.txt").time, 116444736000000000ull);

  ASSERT_TRUE(w.files.contains(L"later.txt"));
  EXPECT_EQ(w.files.at(L"later.txt").time,
            116444736000000000ull + 10'000'000ull + 5ull);
}

TEST(EnvFs, UnicodeNames)
{
  TempDir tmp;
  const std::wstring dirName  = L"Ünïcödé Mod";
  const std::wstring fileName = L"日本.txt";

  tmp.createFile(std::filesystem::path(dirName) / fileName, 3);

  const auto w = Walk::run(tmp.path());

  ASSERT_EQ(w.dirs.size(), 1u);
  EXPECT_EQ(w.dirs[0], dirName);
  ASSERT_EQ(w.files.size(), 1u);
  EXPECT_TRUE(w.files.contains(dirName + L"/" + fileName));

  // the walked directory itself may have non-ASCII characters
  const auto inner = Walk::run(tmp.path() / dirName);
  EXPECT_TRUE(inner.files.contains(fileName));

  const auto root = env::getFilesAndDirs(tmp.path().wstring());
  ASSERT_EQ(root.dirs.size(), 1u);
  EXPECT_EQ(root.dirs[0].name, dirName);
  ASSERT_EQ(root.dirs[0].files.size(), 1u);
  EXPECT_EQ(root.dirs[0].files[0].name, fileName);
  EXPECT_EQ(root.dirs[0].files[0].size, 3u);
}

TEST(EnvFs, SkipsBrokenSymlinkAndUnreadableDir)
{
  TempDir tmp;
  tmp.createFile("ok.txt", 2);
  tmp.createFile("sub/x.txt", 5);
  std::filesystem::create_symlink(tmp.path() / "does-not-exist", tmp.path() / "broken");
  std::filesystem::create_symlink("ok.txt", tmp.path() / "filelink");
  std::filesystem::create_directory_symlink("sub", tmp.path() / "dirlink");

  const bool root = (::geteuid() == 0);

  if (root) {
    // permissions are not enforced for root: chmod 000 would not make the directory
    // unreadable
    std::cout << "[  NOTE    ] running as root, the unreadable directory part of this "
                 "test is skipped\n";
  } else {
    tmp.createFile("locked/hidden.txt", 1);
    std::filesystem::permissions(tmp.path() / "locked", std::filesystem::perms::none);
  }

  const auto w = Walk::run(tmp.path());

  EXPECT_FALSE(w.mismatched);
  EXPECT_TRUE(w.dirStack.empty());
  EXPECT_EQ(w.starts, w.ends);

  // the broken link is skipped, links to a file or a directory are followed
  EXPECT_FALSE(w.files.contains(L"broken"));
  EXPECT_TRUE(w.files.contains(L"ok.txt"));
  EXPECT_TRUE(w.files.contains(L"sub/x.txt"));
  ASSERT_TRUE(w.files.contains(L"filelink"));
  EXPECT_EQ(w.files.at(L"filelink").size, 2u);
  ASSERT_TRUE(w.files.contains(L"dirlink/x.txt"));
  EXPECT_EQ(w.files.at(L"dirlink/x.txt").size, 5u);

  // ok.txt, sub/x.txt, filelink, dirlink/x.txt; nothing from the locked directory
  EXPECT_EQ(w.files.size(), 4u);
}

TEST(EnvFs, SymlinkLoopTerminates)
{
  TempDir tmp;
  tmp.createFile("d/a.txt", 1);
  // d/loop -> the walked directory: following it again and again would never end, so
  // links to a directory being walked are skipped
  std::filesystem::create_directory_symlink("..", tmp.path() / "d" / "loop");

  const auto w = Walk::run(tmp.path());

  EXPECT_FALSE(w.mismatched);
  EXPECT_EQ(w.starts, w.ends);
  EXPECT_TRUE(w.files.contains(L"d/a.txt"));
  EXPECT_EQ(w.files.size(), 1u);
}
