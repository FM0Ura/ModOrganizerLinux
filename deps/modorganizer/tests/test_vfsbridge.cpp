#include "../src/linux/vfsbridge_linux.h"
#include "tempdir.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QFileInfo>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
struct Env
{
  explicit Env(const char* bridge)
      : m_old(qgetenv("MO2_VFS_BRIDGE")), m_oldPath(qgetenv("PATH"))
  {
    qputenv("MO2_VFS_BRIDGE", bridge);
  }
  ~Env()
  {
    qputenv("MO2_VFS_BRIDGE", m_old);
    qputenv("PATH", m_oldPath);
  }
  QByteArray m_old, m_oldPath;
};

QString q(const fs::path& p)
{
  return QString::fromStdString(p.string());
}
}  // namespace

TEST(VfsBridge, AutoDetectsContainers)
{
  Env e("");
  TempDir root;
  EXPECT_EQ(vfsbridge::mode(q(root.path())), vfsbridge::Mode::Direct);
  root.createFile("run/.containerenv");
  EXPECT_EQ(vfsbridge::mode(q(root.path())), vfsbridge::Mode::Host);

  TempDir docker;
  docker.createFile(".dockerenv");
  EXPECT_EQ(vfsbridge::mode(q(docker.path())), vfsbridge::Mode::Host);
}

TEST(VfsBridge, VariableWins)
{
  TempDir root;
  root.createFile("run/.containerenv");
  {
    Env e("direct");
    EXPECT_EQ(vfsbridge::mode(q(root.path())), vfsbridge::Mode::Direct);
  }
  {
    Env e("host");
    EXPECT_EQ(vfsbridge::mode("/nonexistent-root"), vfsbridge::Mode::Host);
  }
  {
    Env e("bogus");
    EXPECT_EQ(vfsbridge::mode(q(root.path())), vfsbridge::Mode::Host);
  }
}

TEST(VfsBridge, Commands)
{
  TempDir bin;
  const auto hostExec = bin.path() / "distrobox-host-exec";
  {
    std::ofstream(hostExec) << "#!/bin/sh\nexec \"$@\"\n";
  }
  fs::permissions(hostExec, fs::perms::owner_all);

  {
    Env e("direct");
    const auto c = vfsbridge::helperCommand("/opt/mo2/mo2-vfs", {"--cleanup"});
    EXPECT_EQ(c.program, "/opt/mo2/mo2-vfs");
    EXPECT_EQ(c.arguments, QStringList{"--cleanup"});
  }
  {
    Env e("host");
    qputenv("PATH", QFile::encodeName(q(bin.path())));
    const auto c =
        vfsbridge::helperCommand("/opt/mo2/mo2-vfs", {"--log-level", "info"});
    EXPECT_EQ(c.program, q(hostExec));
    EXPECT_EQ(c.arguments, (QStringList{"/opt/mo2/mo2-vfs", "--log-level", "info"}));
  }
  {
    Env e("host");
    qputenv("PATH", "/nonexistent");
    EXPECT_THROW(vfsbridge::helperCommand("/opt/mo2/mo2-vfs", {}), std::runtime_error);
  }
}

TEST(VfsBridge, HostCanonicalPathThroughRunHost)
{
  // the box sees /home as a mount; on the host it is a symlink to var/home
  TempDir root;
  fs::create_directories(root.path() / "run/host/var/home/u/x");
  fs::create_symlink("var/home", root.path() / "run/host/home");
  fs::create_directories(root.path() / "home/u/x");

  Env e("host");
  EXPECT_EQ(vfsbridge::hostCanonicalPath("/home/u/x", q(root.path())), "/var/home/u/x");
  EXPECT_EQ(vfsbridge::hostCanonicalPath("/var/home/u/x", q(root.path())),
            "/var/home/u/x");
  // missing paths are returned as they are
  EXPECT_EQ(vfsbridge::hostCanonicalPath("/home/u/missing", q(root.path())),
            "/home/u/missing");
}

TEST(VfsBridge, HostCanonicalPathDirectMode)
{
  TempDir root;
  fs::create_directories(root.path() / "real/x");
  fs::create_symlink("real", root.path() / "link");
  const auto link = q(root.path() / "link/x");

  Env e("direct");
  EXPECT_EQ(vfsbridge::hostCanonicalPath(link), QFileInfo(link).canonicalFilePath());
  EXPECT_NE(vfsbridge::hostCanonicalPath(link), link);
  EXPECT_EQ(vfsbridge::hostCanonicalPath("/nonexistent/path"), "/nonexistent/path");
}

TEST(VfsBridge, HostCanonicalPathWithoutRunHostFallsBack)
{
  TempDir root;
  fs::create_directories(root.path() / "real/x");
  fs::create_symlink("real", root.path() / "link");
  const auto link = q(root.path() / "link/x");

  Env e("host");
  EXPECT_EQ(vfsbridge::hostCanonicalPath(link, "/nonexistent-root"),
            QFileInfo(link).canonicalFilePath());
}
