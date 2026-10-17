#ifndef MO2_TESTS_FUSETEST_H
#define MO2_TESTS_FUSETEST_H

#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

// true when FUSE can be used. When it can't, the caller skips the test
// (`if (!requireFuse()) GTEST_SKIP();`), unless MO2_REQUIRE_FUSE=1, which makes it fail
//
inline bool requireFuse()
{
  QProcess p;
  p.start("fusermount3", {"--version"});
  const bool available = std::filesystem::exists("/dev/fuse") &&
                         p.waitForStarted(5000) && p.waitForFinished(5000) &&
                         p.exitStatus() == QProcess::NormalExit;

  if (!available) {
    const char* require = std::getenv("MO2_REQUIRE_FUSE");
    if (require && std::string(require) == "1") {
      throw std::runtime_error(
          "FUSE is required (MO2_REQUIRE_FUSE=1) but /dev/fuse or fusermount3 is "
          "missing");
    }
    std::cerr << "skipped: FUSE is not available\n";
  }

  return available;
}

// the mo2-vfs program of the tests (MO2_VFS_BINARY, exported by build.sh)
//
inline QString vfsBinary()
{
  return qEnvironmentVariable("MO2_VFS_BINARY");
}

// true when FUSE can be used and MO2_VFS_BINARY names an executable mo2-vfs; same rule
// as requireFuse(): the caller skips the test, MO2_REQUIRE_FUSE=1 makes it fail
//
inline bool requireVfs()
{
  if (!requireFuse()) {
    return false;
  }

  const QFileInfo info(vfsBinary());
  const bool available = !vfsBinary().isEmpty() && info.isFile() && info.isExecutable();

  if (!available) {
    const char* require = std::getenv("MO2_REQUIRE_FUSE");
    if (require && std::string(require) == "1") {
      throw std::runtime_error(
          "mo2-vfs is required (MO2_REQUIRE_FUSE=1) but MO2_VFS_BINARY is unset or "
          "not executable");
    }
    std::cerr << "skipped: MO2_VFS_BINARY is unset or not an executable mo2-vfs\n";
  }

  return available;
}

#endif  // MO2_TESTS_FUSETEST_H
