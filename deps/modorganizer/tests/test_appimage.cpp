#include "../src/linux/appimage_linux.h"

#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <string>

extern char** environ;

using Map = std::map<std::string, std::string>;

TEST(HostEnvironment, RestoresChangedVariables)
{
  const auto r = appimage::hostEnvironment(
      {{"LD_LIBRARY_PATH", "/app/lib"},
       {"MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH", "/host/lib"}});

  ASSERT_TRUE(r.count("LD_LIBRARY_PATH"));
  EXPECT_EQ(r.at("LD_LIBRARY_PATH"), std::optional<std::string>("/host/lib"));
}

TEST(HostEnvironment, RemovesVariablesThatDidNotExist)
{
  const auto r = appimage::hostEnvironment(
      {{"LD_LIBRARY_PATH", "/app/lib"},
       {"MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH", "MO2_APPIMAGE_UNSET"}});

  ASSERT_TRUE(r.count("LD_LIBRARY_PATH"));
  EXPECT_EQ(r.at("LD_LIBRARY_PATH"), std::nullopt);
}

TEST(HostEnvironment, RemovesAppImageVariables)
{
  const auto r = appimage::hostEnvironment({{"APPIMAGE", "/x.AppImage"},
                                            {"APPDIR", "/tmp/.mount"},
                                            {"ARGV0", "x"},
                                            {"OWD", "/home"},
                                            {"MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH", "/h"}});

  for (const char* v : {"APPIMAGE", "APPDIR", "ARGV0", "OWD",
                        "MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH"}) {
    ASSERT_TRUE(r.count(v)) << v;
    EXPECT_EQ(r.at(v), std::nullopt) << v;
  }
}

TEST(HostEnvironment, KeepsUnmarkedVariables)
{
  const auto r = appimage::hostEnvironment(
      {{"HOME", "/h"}, {"APPIMAGE", "/x.AppImage"}});

  EXPECT_EQ(r.count("HOME"), 0u);
}

TEST(HostEnvironment, OutsideAppImageIsEmpty)
{
  EXPECT_TRUE(appimage::hostEnvironment({{"HOME", "/h"}, {"PATH", "/usr/bin"}}).empty());
}

namespace
{
// restores the variables a test changes, and info()
class EnvGuard
{
public:
  explicit EnvGuard(std::initializer_list<const char*> names)
  {
    for (const char* n : names) {
      const char* v = ::getenv(n);
      m_saved.push_back({n, v != nullptr, v ? v : ""});
    }
  }

  ~EnvGuard()
  {
    for (const auto& s : m_saved) {
      if (s.had) {
        ::setenv(s.name.c_str(), s.value.c_str(), 1);
      } else {
        ::unsetenv(s.name.c_str());
      }
    }
    appimage::setInfoForTesting({});
  }

private:
  struct Saved
  {
    std::string name;
    bool had;
    std::string value;
  };
  std::vector<Saved> m_saved;
};
}  // namespace

TEST(CaptureAndRestore, AppliesToProcess)
{
  EnvGuard guard({"APPIMAGE", "APPDIR", "LD_LIBRARY_PATH",
                  "MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH", "MO2_TEST_CHILD_ENV"});

  ::setenv("APPIMAGE", "/x/Mod Organizer.AppImage", 1);
  ::setenv("APPDIR", "/tmp/.mount_x", 1);
  ::setenv("LD_LIBRARY_PATH", "/app/lib", 1);
  ::setenv("MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH", "/host/lib", 1);

  appimage::captureAndRestore();

  EXPECT_EQ(::getenv("APPIMAGE"), nullptr);
  EXPECT_EQ(::getenv("APPDIR"), nullptr);
  EXPECT_EQ(::getenv("MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH"), nullptr);
  ASSERT_NE(::getenv("LD_LIBRARY_PATH"), nullptr);
  EXPECT_STREQ(::getenv("LD_LIBRARY_PATH"), "/host/lib");
  EXPECT_EQ(appimage::info().appImage, "/x/Mod Organizer.AppImage");
  EXPECT_EQ(appimage::info().appDir, "/tmp/.mount_x");
  EXPECT_TRUE(appimage::info().active());

  // a child process does not see the markers either
  char path[] = "/tmp/mo2-appimage-test-XXXXXX";
  const int fd = ::mkstemp(path);
  ASSERT_GE(fd, 0);

  // fork and exec rather than posix_spawn(): the spawn.h of Mod Organizer shadows the
  // one of the system in the include path of the tests
  const pid_t pid = ::fork();
  ASSERT_GE(pid, 0);
  if (pid == 0) {
    ::dup2(fd, 1);
    char arg0[]        = "env";
    char* const argv[] = {arg0, nullptr};
    ::execve("/usr/bin/env", argv, environ);
    ::_exit(127);
  }
  int status = 0;
  ::waitpid(pid, &status, 0);

  std::string out;
  char buf[4096];
  ::lseek(fd, 0, SEEK_SET);
  for (ssize_t n; (n = ::read(fd, buf, sizeof(buf))) > 0;) {
    out.append(buf, static_cast<size_t>(n));
  }
  ::close(fd);
  ::unlink(path);

  EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  EXPECT_FALSE(out.empty());
  EXPECT_EQ(out.find("MO2_APPIMAGE_"), std::string::npos);
  EXPECT_NE(out.find("LD_LIBRARY_PATH=/host/lib"), std::string::npos);
}

TEST(CaptureAndRestore, DoesNothingOutsideAppImage)
{
  EnvGuard guard({"APPIMAGE", "LD_LIBRARY_PATH"});

  ::unsetenv("APPIMAGE");
  ::setenv("LD_LIBRARY_PATH", "/keep", 1);
  appimage::captureAndRestore();

  EXPECT_FALSE(appimage::info().active());
  EXPECT_STREQ(::getenv("LD_LIBRARY_PATH"), "/keep");
}

TEST(CaptureAndRestore, MarkersWithoutAppImageAreRestored)
{
  // squashfs-root/AppRun of an extracted AppImage: AppRun's markers, no APPIMAGE; the
  // programs started by Mod Organizer must not get AppRun's LD_LIBRARY_PATH either
  EnvGuard guard({"APPIMAGE", "LD_LIBRARY_PATH", "MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH"});

  ::unsetenv("APPIMAGE");
  ::setenv("LD_LIBRARY_PATH", "/run/user/1000/mo2-hostlibs-1000", 1);
  ::setenv("MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH", "MO2_APPIMAGE_UNSET", 1);
  appimage::captureAndRestore();

  EXPECT_EQ(::getenv("LD_LIBRARY_PATH"), nullptr);
  EXPECT_EQ(::getenv("MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH"), nullptr);
  // not an AppImage: no self-update
  EXPECT_FALSE(appimage::info().active());
}
