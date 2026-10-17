#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QString>
#include <QTimer>

#include <csignal>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fusetest.h"
#include "shared/util.h"
#include "spawn.h"
#include "tempdir.h"
#include "vfsconnector.h"

namespace fs = std::filesystem;

namespace
{
QString q(const fs::path& p)
{
  return QString::fromStdString(p.string());
}

std::string readFile(const fs::path& p)
{
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool mounted(const fs::path& p)
{
  const auto info = readFile("/proc/self/mountinfo");
  return info.find(" " + p.string() + " ") != std::string::npos;
}

std::set<std::string> list(const fs::path& dir)
{
  std::set<std::string> names;
  for (const auto& e : fs::recursive_directory_iterator(dir)) {
    names.insert(fs::relative(e.path(), dir).string());
  }
  return names;
}

// the environment of every test: its own state directory and, unless a test sets
// another one, the real helper
class VfsConnectorTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_oldState  = qgetenv("MO2_VFS_STATE_DIR");
    m_oldBinary = qgetenv("MO2_VFS_BINARY");
    qputenv("MO2_VFS_STATE_DIR", QByteArray::fromStdString(m_state.path().string()));
  }

  void TearDown() override
  {
    restore("MO2_VFS_STATE_DIR", m_oldState);
    restore("MO2_VFS_BINARY", m_oldBinary);
  }

  // a shell script that is the helper
  void fakeHelper(const std::string& body)
  {
    const auto script = m_state.path() / "fake-mo2-vfs";
    {
      std::ofstream out(script);
      out << "#!/bin/sh\n" << body;
    }
    fs::permissions(script, fs::perms::owner_all);
    qputenv("MO2_VFS_BINARY", QByteArray::fromStdString(script.string()));
  }

  // data/ (original.esm), mods A and B, overwrite
  struct Layout
  {
    TempDir tmp;
    fs::path data, a, b, overwrite;

    Layout()
    {
      data      = tmp.path() / "data";
      a         = tmp.path() / "mods/A";
      b         = tmp.path() / "mods/B";
      overwrite = tmp.path() / "overwrite";
      tmp.createFile("data/original.esm");
      tmp.createFile("mods/A/a.esp");
      tmp.createFile("mods/A/textures/a.dds");
      tmp.createFile("mods/B/b.esp");
      fs::create_directories(overwrite);
    }

    MappingType mapping() const
    {
      return {Mapping{q(a), q(data), true, false}, Mapping{q(b), q(data), true, false},
              Mapping{q(overwrite), q(data), true, true}};
    }
  };

  TempDir m_state;

private:
  static void restore(const char* name, const QByteArray& value)
  {
    if (value.isNull()) {
      qunsetenv(name);
    } else {
      qputenv(name, value);
    }
  }

  QByteArray m_oldState, m_oldBinary;
};

// releases the mount of a test that failed before it
struct Release
{
  VfsConnector& vfs;
  ~Release()
  {
    try {
      vfs.releaseIfIdle();
    } catch (...) {
    }
  }
};
}  // namespace

TEST_F(VfsConnectorTest, LinuxName)
{
  fakeHelper("exit 0\n");
  EXPECT_EQ(createVfsConnector()->name(), "mo2-vfs");
}

TEST_F(VfsConnectorTest, UpdateMappingDoesNotMount)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();

  vfs->updateMapping(l.mapping());

  EXPECT_FALSE(vfs->isActive());
  EXPECT_FALSE(mounted(l.data));
  EXPECT_EQ(list(l.data), std::set<std::string>{"original.esm"});
}

TEST_F(VfsConnectorTest, ActivateMountsAndMergesMods)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};

  vfs->updateMapping(l.mapping());
  vfs->activate();

  EXPECT_TRUE(vfs->isActive());
  EXPECT_TRUE(mounted(l.data));
  EXPECT_EQ(list(l.data), (std::set<std::string>{"original.esm", "a.esp", "b.esp",
                                                 "textures", "textures/a.dds"}));

  // a file created in the data directory goes to overwrite
  {
    std::ofstream(l.data / "new.txt") << "x";
  }
  EXPECT_TRUE(fs::exists(l.overwrite / "new.txt"));

  vfs->releaseIfIdle();
  EXPECT_FALSE(vfs->isActive());
  EXPECT_FALSE(mounted(l.data));
}

// the mappings are disk paths: the ones built by OrganizerCore::fileMapping() and the
// ones given by plugins (local saves, file mappers) may contain backslashes, which are
// separators on Windows only
TEST_F(VfsConnectorTest, BackslashPathsWork)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};

  // the directory mappings are plain; the file ones join the relative path with '\'
  vfs->updateMapping({
      Mapping{q(l.a), q(l.data), true, false},
      Mapping{q(l.b) + "\\b.esp", q(l.data) + "\\renamed.esp", false, false},
      Mapping{q(l.overwrite), q(l.data), true, true},
  });
  vfs->activate();

  EXPECT_TRUE(vfs->isActive());
  EXPECT_TRUE(fs::exists(l.data / "a.esp"));
  EXPECT_TRUE(fs::exists(l.data / "textures/a.dds"));
  EXPECT_TRUE(fs::exists(l.data / "renamed.esp"));
}

TEST_F(VfsConnectorTest, ReleaseIfIdleUnmounts)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};

  vfs->updateMapping(l.mapping());
  vfs->activate();
  ASSERT_TRUE(mounted(l.data));

  vfs->releaseIfIdle();
  EXPECT_FALSE(vfs->isActive());
  EXPECT_FALSE(mounted(l.data));

  // and it can be mounted again
  vfs->activate();
  EXPECT_TRUE(mounted(l.data));
  vfs->releaseIfIdle();
  EXPECT_FALSE(mounted(l.data));
}

TEST_F(VfsConnectorTest, ActivateTwiceUpdates)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};

  vfs->updateMapping({Mapping{q(l.a), q(l.data), true, false}});
  vfs->activate();
  EXPECT_FALSE(fs::exists(l.data / "b.esp"));

  vfs->updateMapping({Mapping{q(l.a), q(l.data), true, false},
                      Mapping{q(l.b), q(l.data), true, false}});
  vfs->activate();
  EXPECT_TRUE(vfs->isActive());
  EXPECT_TRUE(fs::exists(l.data / "b.esp"));
}

TEST_F(VfsConnectorTest, AutoDeactivatesWhenGroupEnds)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};

  int called = 0;
  vfs->setDeactivatedCallback([&] {
    ++called;
  });

  vfs->updateMapping(l.mapping());
  vfs->activate();
  ASSERT_TRUE(vfs->isActive());

  // a program in a group of its own (spawn.h is shadowed by the one of the
  // application, so no posix_spawn)
  const pid_t pid = ::fork();
  ASSERT_GE(pid, 0);
  if (pid == 0) {
    ::setpgid(0, 0);
    ::execlp("sleep", "sleep", "1", nullptr);
    ::_exit(127);
  }
  ::setpgid(pid, pid);

  vfs->watchProcessGroup(pid);

  // not released while a group is watched
  vfs->releaseIfIdle();
  EXPECT_TRUE(vfs->isActive());

  EXPECT_TRUE(vfs->waitInactive(std::chrono::seconds(5)));
  EXPECT_FALSE(vfs->isActive());
  EXPECT_FALSE(mounted(l.data));

  // the callback is queued
  QEventLoop loop;
  QTimer::singleShot(100, &loop, &QEventLoop::quit);
  loop.exec();
  EXPECT_EQ(called, 1);

  int status = 0;
  ::waitpid(pid, &status, 0);
}

// what ProcessRunner::run() does on Linux, without the rest of the application: mount,
// start the program with spawn::startBinary() (in a group of its own), watch its group;
// the program sees the mods and the file system goes away when it ends
TEST_F(VfsConnectorTest, StartedProgramSeesTheMods)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};

  const auto out    = l.tmp.path() / "out.txt";
  const auto script = l.tmp.path() / "tool.sh";
  {
    std::ofstream s(script);
    s << "#!/bin/sh\nls > \"$1\"\nsleep 0.3\n";
  }
  fs::permissions(script, fs::perms::owner_all);

  vfs->updateMapping(l.mapping());
  vfs->activate();
  ASSERT_TRUE(vfs->isActive());

  spawn::SpawnParameters sp;
  sp.binary           = QFileInfo(q(script));
  sp.arguments        = "\"" + q(out) + "\"";
  sp.currentDirectory = QDir(q(l.data));

  const HANDLE h = spawn::startBinary(nullptr, sp);
  ASSERT_NE(h, INVALID_HANDLE_VALUE);
  const auto pid = static_cast<pid_t>(reinterpret_cast<std::intptr_t>(h));

  vfs->watchProcessGroup(pid);

  EXPECT_TRUE(vfs->waitInactive(std::chrono::seconds(5)));
  EXPECT_FALSE(mounted(l.data));

  int status = 0;
  ASSERT_EQ(::waitpid(pid, &status, 0), pid);
  EXPECT_EQ(WEXITSTATUS(status), 0);

  // the working directory is the mounted data directory
  EXPECT_EQ(readFile(out), "a.esp\nb.esp\noriginal.esm\ntextures\n");
}

TEST_F(VfsConnectorTest, UpdateMappingWhileActiveUpdates)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};

  vfs->updateMapping({Mapping{q(l.a), q(l.data), true, false}});
  vfs->activate();
  EXPECT_FALSE(fs::exists(l.data / "b.esp"));

  // no activate(): the mounted file system follows
  vfs->updateMapping({Mapping{q(l.a), q(l.data), true, false},
                      Mapping{q(l.b), q(l.data), true, false}});
  EXPECT_TRUE(vfs->isActive());
  EXPECT_TRUE(fs::exists(l.data / "b.esp"));
}

TEST_F(VfsConnectorTest, NestedRequestThrows)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};

  vfs->updateMapping(l.mapping());
  vfs->activate();

  // an event processed while the update waits for its reply
  bool threw = false, ran = false;
  QTimer::singleShot(0, [&] {
    ran = true;
    try {
      vfs->activate();
    } catch (const std::runtime_error& e) {
      threw = std::string(e.what()).find("already in progress") != std::string::npos;
    }
  });

  vfs->updateMapping(l.mapping());

  EXPECT_TRUE(ran);
  EXPECT_TRUE(threw);
  EXPECT_TRUE(vfs->isActive());
}

TEST_F(VfsConnectorTest, CallbackOfOldDeactivationDoesNotFire)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};

  int called = 0;
  vfs->setDeactivatedCallback([&] {
    ++called;
  });

  vfs->updateMapping(l.mapping());
  vfs->activate();
  vfs->releaseIfIdle();  // callback queued
  vfs->activate();       // active again before it is delivered

  QEventLoop loop;
  QTimer::singleShot(100, &loop, &QEventLoop::quit);
  loop.exec();
  EXPECT_EQ(called, 0);
  EXPECT_TRUE(vfs->isActive());

  vfs->releaseIfIdle();
  QTimer::singleShot(100, &loop, &QEventLoop::quit);
  loop.exec();
  EXPECT_EQ(called, 1);
}

TEST_F(VfsConnectorTest, UpdateTimeoutDeactivatesAndRemounts)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  const auto real = vfsBinary();  // before the fake replaces it

  // answers the mount and never the update
  fakeHelper("[ \"$1\" = \"--cleanup\" ] && exit 0\n"
             "echo '{\"event\":\"hello\",\"version\":\"0\"}'\n"
             "read line\n"
             "echo '{\"event\":\"ready\",\"roots\":[]}'\n"
             "read line\n"
             "read line\n");
  qputenv("MO2_VFS_TIMEOUT_MS", "300");

  const auto vfs = createVfsConnector();
  int called     = 0;
  vfs->setDeactivatedCallback([&] {
    ++called;
  });

  vfs->updateMapping(l.mapping());
  vfs->activate();
  ASSERT_TRUE(vfs->isActive());

  vfs->updateMapping(l.mapping());  // the update times out (and is only logged)
  EXPECT_FALSE(vfs->isActive());

  QEventLoop loop;
  QTimer::singleShot(100, &loop, &QEventLoop::quit);
  loop.exec();
  EXPECT_EQ(called, 1);

  // the next activation starts a new helper, which gets a mount
  qunsetenv("MO2_VFS_TIMEOUT_MS");
  qputenv("MO2_VFS_BINARY", real.toUtf8());
  Release release{*vfs};
  vfs->activate();
  EXPECT_TRUE(vfs->isActive());
  EXPECT_TRUE(mounted(l.data));
}

TEST_F(VfsConnectorTest, ActivateErrorIsException)
{
  fakeHelper("echo '{\"event\":\"hello\",\"version\":\"0\"}'\n"
             "read line\n"
             "echo '{\"event\":\"error\",\"code\":\"mount_busy\","
             "\"message\":\"busy\",\"path\":\"/x\"}'\n"
             "read line\n");

  const auto vfs = createVfsConnector();
  vfs->updateMapping({Mapping{"/a", "/b", true, false}});

  try {
    vfs->activate();
    FAIL() << "activate() did not throw";
  } catch (const std::runtime_error& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("busy"), std::string::npos) << what;
    EXPECT_NE(what.find("/x"), std::string::npos) << what;
  }
  EXPECT_FALSE(vfs->isActive());
}

TEST_F(VfsConnectorTest, ErrorWithoutPath)
{
  fakeHelper("echo '{\"event\":\"hello\",\"version\":\"0\"}'\n"
             "read line\n"
             "echo '{\"event\":\"error\",\"code\":\"bad_request\","
             "\"message\":\"nope\",\"path\":null}'\n"
             "read line\n");

  const auto vfs = createVfsConnector();
  try {
    vfs->activate();
    FAIL() << "activate() did not throw";
  } catch (const std::runtime_error& e) {
    EXPECT_EQ(std::string(e.what()), "nope");
  }
}

TEST_F(VfsConnectorTest, MissingBinaryIsException)
{
  qputenv("MO2_VFS_BINARY", "/nonexistent/mo2-vfs");
  const auto vfs = createVfsConnector();

  try {
    vfs->activate();
    FAIL() << "activate() did not throw";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("/nonexistent/mo2-vfs"), std::string::npos)
        << e.what();
  }
  EXPECT_FALSE(vfs->isActive());
}

TEST_F(VfsConnectorTest, HelperThatDiesWithoutHelloIsException)
{
  fakeHelper("echo 'boom' >&2\nexit 3\n");

  const auto vfs = createVfsConnector();
  try {
    vfs->activate();
    FAIL() << "activate() did not throw";
  } catch (const std::runtime_error& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("boom"), std::string::npos) << what;
  }
}

TEST_F(VfsConnectorTest, HelperCrashDeactivates)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};

  int called = 0;
  vfs->setDeactivatedCallback([&] {
    ++called;
  });

  vfs->updateMapping(l.mapping());
  vfs->activate();
  ASSERT_TRUE(vfs->isActive());

  // the helper is a child of this process
  pid_t helper = 0;
  for (const auto& e : fs::directory_iterator("/proc")) {
    const auto name = e.path().filename().string();
    if (name.find_first_not_of("0123456789") != std::string::npos) {
      continue;
    }
    std::ifstream stat(e.path() / "stat");
    std::string line;
    std::getline(stat, line);
    // pid (comm) state ppid
    const auto close = line.rfind(')');
    if (close == std::string::npos || line.find("(mo2-vfs)") == std::string::npos) {
      continue;
    }
    std::istringstream rest(line.substr(close + 2));
    char state;
    int ppid;
    rest >> state >> ppid;
    if (ppid == ::getpid()) {
      helper = std::stoi(name);
    }
  }
  ASSERT_GT(helper, 0);

  ::kill(helper, SIGKILL);

  EXPECT_TRUE(vfs->waitInactive(std::chrono::seconds(2)));
  EXPECT_FALSE(vfs->isActive());

  QEventLoop loop;
  QTimer::singleShot(100, &loop, &QEventLoop::quit);
  loop.exec();
  EXPECT_EQ(called, 1);

  // the next activation starts a new helper (after cleaning up the dead mount)
  vfs->activate();
  EXPECT_TRUE(vfs->isActive());
  EXPECT_TRUE(fs::exists(l.data / "a.esp"));

  vfs->releaseIfIdle();
  EXPECT_FALSE(mounted(l.data));
}

TEST_F(VfsConnectorTest, StaleMountsReported)
{
  fakeHelper("if [ \"$1\" = \"--cleanup\" ]; then\n"
             "  echo '{\"root\":\"/x\",\"ok\":false}'\n"
             "  exit 1\n"
             "fi\n");

  const auto vfs = createVfsConnector();
  EXPECT_EQ(vfs->staleMounts(), QStringList{"/x"});
}

TEST_F(VfsConnectorTest, NoStaleMountsWhenCleanupSucceeds)
{
  fakeHelper("echo '{\"root\":\"/x\",\"ok\":true}'\nexit 0\n");

  const auto vfs = createVfsConnector();
  EXPECT_TRUE(vfs->staleMounts().isEmpty());
}

TEST_F(VfsConnectorTest, ParamsSkipListsAreSent)
{
  if (!requireFuse())
    GTEST_SKIP();

  Layout l;
  l.tmp.createFile("mods/A/a.esp.mohidden");
  l.tmp.createFile("mods/A/fomod/info.xml");
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};

  vfs->updateParams(MOBase::log::Debug, env::CoreDumpTypes::Mini, "/tmp/dumps",
                    std::chrono::seconds(0), "", {".mohidden"}, {"fomod"});
  vfs->updateMapping(l.mapping());
  vfs->activate();

  EXPECT_TRUE(fs::exists(l.data / "a.esp"));
  EXPECT_FALSE(fs::exists(l.data / "a.esp.mohidden"));
  EXPECT_FALSE(fs::exists(l.data / "fomod"));
}

TEST_F(VfsConnectorTest, AcceptsForcedLibraries)
{
  fakeHelper("exit 0\n");
  const auto vfs = createVfsConnector();

  // nothing to configure, but OrganizerCore calls it
  vfs->updateForcedLibraries({});
  EXPECT_EQ(vfs->name(), "mo2-vfs");
}

// shown in the about dialog and logged on startup
TEST(VfsConnector, NoUsvfsOnLinux)
{
  EXPECT_EQ(MOShared::getUsvfsVersionString(), "none");
}
