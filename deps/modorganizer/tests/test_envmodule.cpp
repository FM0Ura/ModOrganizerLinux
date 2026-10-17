#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>

#include <QFileInfo>
#include <QProcess>

#include "envmodule.h"

namespace
{

// the HANDLE of a process on Linux, see envmodule.h
HANDLE processHandle(qint64 pid)
{
  return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(pid));
}

const env::Process* findPid(const std::vector<env::Process>& v, DWORD pid)
{
  const auto itor = std::find_if(v.begin(), v.end(), [&](auto&& p) {
    return p.pid() == pid;
  });

  return (itor == v.end() ? nullptr : &*itor);
}

}  // namespace

TEST(EnvModule, FindsSelf)
{
  const auto ps = env::getRunningProcesses();
  ASSERT_GT(ps.size(), 1u);

  const auto* self = findPid(ps, static_cast<DWORD>(::getpid()));
  ASSERT_NE(self, nullptr);

  EXPECT_EQ(self->ppid(), static_cast<DWORD>(::getppid()));
  EXPECT_EQ(
      self->name(),
      QFileInfo(QString::fromStdString(std::filesystem::read_symlink("/proc/self/exe")))
          .fileName());

  // the parent is running too
  EXPECT_NE(findPid(ps, static_cast<DWORD>(::getppid())), nullptr);
}

TEST(EnvModule, ProcessNameAndParent)
{
  const auto pid = static_cast<DWORD>(::getpid());

  EXPECT_EQ(env::getProcessName(pid), "organizer_tests");
  EXPECT_EQ(env::getProcessName(processHandle(pid)), "organizer_tests");
  EXPECT_EQ(env::getProcessParentID(pid), static_cast<DWORD>(::getppid()));
  EXPECT_EQ(env::getProcessParentID(processHandle(pid)),
            static_cast<DWORD>(::getppid()));

  // a Process built from a handle has the pid of the handle (and, like on Windows,
  // no parent nor name)
  const env::Process p(processHandle(pid));
  EXPECT_TRUE(p.isValid());
  EXPECT_EQ(p.pid(), pid);
  EXPECT_TRUE(p.canAccess());

  // the handle to wait for a process is its pid, and empty if it doesn't exist
  const auto h = p.openHandleForWait();
  EXPECT_EQ(h.get(), processHandle(pid));
  EXPECT_FALSE(env::Process(0x3fffffff, 0, {}).openHandleForWait());

  // pid 1 (init) exists and belongs to root
  if (::geteuid() != 0) {
    EXPECT_FALSE(env::Process(1, 0, {}).canAccess());
  }
}

TEST(EnvModule, ProcessTreeHasChildren)
{
  QProcess child;
  child.start("sleep", {"10"});
  ASSERT_TRUE(child.waitForStarted());

  const auto root = env::getProcessTree(processHandle(::getpid()));

  // the root is not a process, it contains the process given to getProcessTree()
  EXPECT_FALSE(root.isValid());
  ASSERT_EQ(root.children().size(), 1u);

  const auto& self = root.children()[0];
  EXPECT_EQ(self.pid(), static_cast<DWORD>(::getpid()));

  const auto* c = findPid(self.children(), static_cast<DWORD>(child.processId()));
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->name(), "sleep");

  child.kill();
  child.waitForFinished();
}

// a process that has exited but was not waited for yet (a zombie) is not running: it
// is not listed, and the tree of a zombie is empty, so waiting for the tree of a
// program that exited ends (ProcessRunner waits with WNOWAIT and reaps it later)
TEST(EnvModule, ZombiesAreNotRunning)
{
  const pid_t pid = ::fork();
  ASSERT_NE(pid, -1);

  if (pid == 0) {
    ::_exit(0);
  }

  // until it has exited, without reaping it
  siginfo_t info = {};
  ASSERT_EQ(::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOWAIT), 0);

  EXPECT_EQ(findPid(env::getRunningProcesses(), static_cast<DWORD>(pid)), nullptr);
  EXPECT_TRUE(env::getProcessTree(processHandle(pid)).children().empty());

  ASSERT_EQ(::waitpid(pid, nullptr, 0), pid);
}

TEST(EnvModule, LoadedModules)
{
  const auto modules = env::getLoadedModules();
  ASSERT_FALSE(modules.empty());

  const auto exe =
      QString::fromStdString(std::filesystem::read_symlink("/proc/self/exe").string());

  bool foundExe = false, foundQt = false;

  for (auto&& m : modules) {
    // unique, existing files
    EXPECT_EQ(std::count_if(modules.begin(), modules.end(),
                            [&](auto&& other) {
                              return other.path() == m.path();
                            }),
              1)
        << m.path().toStdString();

    EXPECT_TRUE(QFileInfo(m.path()).isFile()) << m.path().toStdString();
    EXPECT_GT(m.fileSize(), 0u) << m.path().toStdString();
    EXPECT_FALSE(m.toString().isEmpty());

    if (m.path() == exe) {
      foundExe = true;
    }

    if (QFileInfo(m.path()).fileName().startsWith("libQt6Core.so")) {
      foundQt = true;
    }
  }

  EXPECT_TRUE(foundExe);
  EXPECT_TRUE(foundQt);
}
