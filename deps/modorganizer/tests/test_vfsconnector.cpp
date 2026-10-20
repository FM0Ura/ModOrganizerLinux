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
#include "linux/exitcodes_linux.h"
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
    m_oldBridge = qgetenv("MO2_VFS_BRIDGE");
    qputenv("MO2_VFS_BRIDGE", "direct");
    qputenv("MO2_VFS_STATE_DIR", QByteArray::fromStdString(m_state.path().string()));
  }

  void TearDown() override
  {
    restore("MO2_VFS_STATE_DIR", m_oldState);
    restore("MO2_VFS_BINARY", m_oldBinary);
    restore("MO2_VFS_BRIDGE", m_oldBridge);
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

  QByteArray m_oldState, m_oldBinary, m_oldBridge;
};

// the fields after the command of /proc/<pid>/stat: state ppid pgrp session
struct ProcStat
{
  char state  = '?';
  int ppid    = 0;
  int pgrp    = 0;
  int session = 0;
};

ProcStat procStat(const fs::path& statFile)
{
  ProcStat st;
  const auto line  = readFile(statFile);
  const auto close = line.rfind(')');
  if (close != std::string::npos) {
    std::istringstream rest(line.substr(close + 2));
    rest >> st.state >> st.ppid >> st.pgrp >> st.session;
  }
  return st;
}

// the mo2-vfs child of this process (0 if none)
pid_t helperPid()
{
  for (const auto& e : fs::directory_iterator("/proc")) {
    const auto name = e.path().filename().string();
    if (name.find_first_not_of("0123456789") != std::string::npos) {
      continue;
    }
    if (readFile(e.path() / "stat").find("(mo2-vfs)") == std::string::npos) {
      continue;
    }
    const auto st = procStat(e.path() / "stat");
    if (st.ppid == ::getpid() && st.state != 'Z') {
      return std::stoi(name);
    }
  }
  return 0;
}

// runs the event loop for a while
void processEvents(int ms)
{
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

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
  if (!requireVfs())
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
  if (!requireVfs())
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
  if (!requireVfs())
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
  if (!requireVfs())
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
  if (!requireVfs())
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

TEST_F(VfsConnectorTest, SpawnWithoutMountRunsAndRecordsExitCode)
{
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  TempDir t;
  const auto out = t.path() / "out.txt";

  VfsSpawnRequest r;
  r.argv             = {"sh", "-c", "echo \"$MO2_T\" > '" + q(out) + "'; exit 7"};
  r.environment      = {"MO2_T=hello"};
  r.workingDirectory = q(t.path());
  const auto pid     = vfs->spawn(r);
  ASSERT_GT(pid, 0);

  const auto code = exitcodes::take(pid, std::chrono::seconds(5));
  ASSERT_TRUE(code);
  EXPECT_EQ(*code, 7);
  EXPECT_EQ(readFile(out), "hello\n");
  EXPECT_FALSE(vfs->isActive());
}

TEST_F(VfsConnectorTest, SpawnFailureIsException)
{
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  VfsSpawnRequest r;
  r.argv             = {"/nonexistent/umu-run"};
  r.workingDirectory = "/";
  try {
    vfs->spawn(r);
    FAIL() << "no exception";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("/nonexistent/umu-run"), std::string::npos)
        << e.what();
  }
}

TEST_F(VfsConnectorTest, KillEndsASpawnedProgram)
{
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  TempDir t;

  VfsSpawnRequest r;
  r.argv             = {"sleep", "60"};
  r.workingDirectory = q(t.path());
  const auto pid     = vfs->spawn(r);
  ASSERT_GT(pid, 0);

  vfs->kill(pid);

  const auto code = exitcodes::take(pid, std::chrono::seconds(5));
  ASSERT_TRUE(code);
  EXPECT_EQ(*code, 143);  // 128 + SIGTERM
}

TEST_F(VfsConnectorTest, KillOfAnEndedProgramThrows)
{
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  TempDir t;

  VfsSpawnRequest r;
  r.argv             = {"true"};
  r.workingDirectory = q(t.path());
  const auto pid     = vfs->spawn(r);
  ASSERT_TRUE(exitcodes::take(pid, std::chrono::seconds(5)));

  EXPECT_THROW(vfs->kill(pid), std::runtime_error);
}

TEST_F(VfsConnectorTest, KillWithoutHelperThrows)
{
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  EXPECT_THROW(vfs->kill(12345), std::runtime_error);
}

TEST_F(VfsConnectorTest, SpawnedProgramKeepsTheMountAndIsListed)
{
  if (!requireVfs())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};
  vfs->updateMapping(l.mapping());
  vfs->activate();

  VfsSpawnRequest r;
  r.argv             = {"sh", "-c", "ls > ../seen.txt; sleep 0.5"};
  r.workingDirectory = q(l.data);
  const auto pid     = vfs->spawn(r);

  EXPECT_EQ(vfs->runningPrograms(), std::vector<qint64>{pid});
  vfs->releaseIfIdle();  // a spawned program counts as watched: nothing happens
  EXPECT_TRUE(vfs->isActive());

  EXPECT_TRUE(vfs->waitInactive(std::chrono::seconds(5)));
  EXPECT_EQ(readFile(l.tmp.path() / "seen.txt"),
            "a.esp\nb.esp\noriginal.esm\ntextures\n");
  EXPECT_EQ(exitcodes::take(pid, std::chrono::seconds(2)), 0);
  EXPECT_TRUE(vfs->runningPrograms().empty());
}

TEST_F(VfsConnectorTest, ActivateAfterUpdateMappingUpdatesOnce)
{
  if (!requireVfs())
    GTEST_SKIP();

  Layout l;
  // counts the update requests by wrapping the real helper
  const auto log     = m_state.path() / "requests.log";
  const auto wrapper = m_state.path() / "wrapper.sh";
  {
    std::ofstream(wrapper) << "#!/bin/sh\n"
                           << "tee -a '" << log.string() << "' | '"
                           << vfsBinary().toStdString() << "' \"$@\"\n";
  }
  fs::permissions(wrapper, fs::perms::owner_all);
  qputenv("MO2_VFS_BINARY", QByteArray::fromStdString(wrapper.string()));

  const auto vfs = createVfsConnector();
  Release release{*vfs};
  vfs->updateMapping(l.mapping());
  vfs->activate();
  // the second Run: beforeRun() calls updateMapping() and then activate()
  vfs->updateMapping(l.mapping());
  vfs->activate();

  const auto requests = readFile(log);
  std::size_t updates = 0;
  for (auto p = requests.find("\"op\":\"update\""); p != std::string::npos;
       p      = requests.find("\"op\":\"update\"", p + 1)) {
    ++updates;
  }
  EXPECT_EQ(updates, 1u) << requests;
}

TEST_F(VfsConnectorTest, AutoDeactivatesWhenGroupEnds)
{
  if (!requireVfs())
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
  if (!requireVfs())
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
  if (!requireVfs())
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
  if (!requireVfs())
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
  if (!requireVfs())
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
  if (!requireVfs())
    GTEST_SKIP();

  Layout l;
  const auto real = vfsBinary();  // before the fake replaces it

  // answers the mount and never the update
  const auto pidFile = m_state.path() / "fake.pid";
  fakeHelper("[ \"$1\" = \"--cleanup\" ] && exit 0\n"
             "echo $$ > '" +
             pidFile.string() +
             "'\n"
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

  // no program is watched: the hung helper was killed (and reaped)
  const auto fakePid = std::stoi(readFile(pidFile));
  EXPECT_NE(::kill(fakePid, 0), 0);

  // the helper that stopped answering lost the file system
  EXPECT_TRUE(vfs->takeLostReason().contains("did not reply"));

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

// an update that failed in updateMapping() must not make activate() skip its own
TEST_F(VfsConnectorTest, ActivateUpdatesAgainAfterFailedUpdateMapping)
{
  fakeHelper("[ \"$1\" = \"--cleanup\" ] && exit 0\n"
             "[ \"$1\" = \"--list-mounts\" ] && exit 0\n"
             "echo '{\"event\":\"hello\",\"version\":\"0\"}'\n"
             "read line\n"
             "echo '{\"event\":\"ready\",\"roots\":[]}'\n"
             "read line\n"
             "echo '{\"event\":\"updated\"}'\n"
             "read line\n"
             "echo '{\"event\":\"error\",\"code\":\"bad_request\","
             "\"message\":\"nope\",\"path\":null}'\n"
             "read line\n"
             "echo '{\"event\":\"error\",\"code\":\"bad_request\","
             "\"message\":\"still nope\",\"path\":null}'\n"
             "read line\n");

  const auto vfs = createVfsConnector();
  vfs->updateMapping({Mapping{"/a", "/b", true, false}});
  vfs->activate();  // mount

  vfs->updateMapping({Mapping{"/a", "/b", true, false}});  // updated
  vfs->updateMapping({Mapping{"/a", "/b", true, false}});  // error, logged

  try {
    vfs->activate();
    FAIL() << "activate() did not send its own update";
  } catch (const std::runtime_error& e) {
    EXPECT_EQ(std::string(e.what()), "still nope");
  }
}

TEST_F(VfsConnectorTest, SpawnWithoutPidIsException)
{
  fakeHelper("echo '{\"event\":\"hello\",\"version\":\"0\"}'\n"
             "read line\n"
             "echo '{\"event\":\"spawned\"}'\n"
             "read line\n");

  const auto vfs = createVfsConnector();
  VfsSpawnRequest r;
  r.argv             = {"true"};
  r.workingDirectory = "/";
  EXPECT_THROW(vfs->spawn(r), std::runtime_error);
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
  if (!requireVfs())
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

  const pid_t helper = helperPid();
  ASSERT_GT(helper, 0);

  // nothing was lost yet
  EXPECT_TRUE(vfs->takeLostReason().isEmpty());

  ::kill(helper, SIGKILL);

  EXPECT_TRUE(vfs->waitInactive(std::chrono::seconds(5)));
  EXPECT_FALSE(vfs->isActive());

  // the dead mount (ENOTCONN) was cleaned up before the deactivation: the refresh that
  // follows sees the original files
  EXPECT_FALSE(mounted(l.data));
  EXPECT_EQ(list(l.data), std::set<std::string>{"original.esm"});

  QEventLoop loop;
  QTimer::singleShot(100, &loop, &QEventLoop::quit);
  loop.exec();
  EXPECT_EQ(called, 1);

  // why the file system stopped, for the user at the end of the run; once
  const auto reason = vfs->takeLostReason();
  EXPECT_TRUE(reason.contains("mo2-vfs ended")) << reason.toStdString();
  EXPECT_TRUE(vfs->takeLostReason().isEmpty());

  // the next activation starts a new helper (after cleaning up the dead mount)
  vfs->activate();
  EXPECT_TRUE(vfs->isActive());
  EXPECT_TRUE(fs::exists(l.data / "a.esp"));

  vfs->releaseIfIdle();
  EXPECT_FALSE(mounted(l.data));

  // a normal unmount is not a loss
  EXPECT_TRUE(vfs->takeLostReason().isEmpty());
}

// the game is running when the helper dies: the dead mount is cleaned up all the same
TEST_F(VfsConnectorTest, HelperCrashWithWatchedGroupCleansUp)
{
  if (!requireVfs())
    GTEST_SKIP();

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  const auto vfs = createVfsConnector();
  Release release{*vfs};

  vfs->updateMapping(l.mapping());
  vfs->activate();
  ASSERT_TRUE(vfs->isActive());

  const pid_t pid = ::fork();
  ASSERT_GE(pid, 0);
  if (pid == 0) {
    ::setpgid(0, 0);
    ::execlp("sleep", "sleep", "3", nullptr);
    ::_exit(127);
  }
  ::setpgid(pid, pid);
  vfs->watchProcessGroup(pid);

  const pid_t helper = helperPid();
  ASSERT_GT(helper, 0);
  ::kill(helper, SIGKILL);

  EXPECT_TRUE(vfs->waitInactive(std::chrono::seconds(5)));
  EXPECT_FALSE(mounted(l.data));
  EXPECT_EQ(list(l.data), std::set<std::string>{"original.esm"});
  EXPECT_TRUE(vfs->takeLostReason().contains("mo2-vfs ended"));

  ::kill(pid, SIGKILL);
  int status = 0;
  ::waitpid(pid, &status, 0);
}

// the helper ended with its mount left behind, then an unhooked Run started a new
// helper (spawn()): the next activation still cleans up the left mount first and, as it
// can't be unmounted, fails instead of mounting over it
TEST_F(VfsConnectorTest, SpawnAfterLostHelperKeepsTheLeftMountGuard)
{
  const auto count    = m_state.path() / "count";
  const auto requests = m_state.path() / "requests.log";
  fakeHelper(R"([ "$1" = "--cleanup" ] && exit 0
if [ "$1" = "--list-mounts" ]; then echo '{"roots":["/x/Data"]}'; exit 0; fi
n=$(( $(cat ')" +
             count.string() + R"(' 2>/dev/null || echo 0) + 1 ))
echo $n > ')" +
             count.string() + R"('
echo '{"event":"hello","version":"0"}'
if [ $n = 1 ]; then
  read line
  echo '{"event":"ready","roots":["/x/Data"]}'
  read line
  echo '{"event":"spawned","pid":1111}'
  sleep 0.2
  exit 1
fi
while read line; do
  echo "$line" >> ')" +
             requests.string() + R"('
  case "$line" in
    *'"op":"spawn"'*) echo '{"event":"spawned","pid":4242}' ;;
    *) echo '{"event":"ready","roots":["/x/Data"]}' ;;
  esac
done
)");

  const auto vfs = createVfsConnector();
  VfsSpawnRequest r;
  r.argv             = {"true"};
  r.workingDirectory = "/";

  // a hooked Run
  vfs->activate();
  EXPECT_EQ(vfs->spawn(r), 1111);

  // the helper ends and its mount can't be unmounted
  processEvents(600);
  ASSERT_TRUE(vfs->isActive());
  ASSERT_TRUE(vfs->staleMounts().contains("/x/Data"));

  // an unhooked Run starts a new helper
  EXPECT_EQ(vfs->spawn(r), 4242);
  EXPECT_TRUE(vfs->isActive());

  // a hooked Run
  try {
    vfs->activate();
    FAIL() << "activate() mounted over the left mount";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("still mounted"), std::string::npos)
        << e.what();
  }
  EXPECT_TRUE(vfs->isActive());
  EXPECT_EQ(readFile(requests).find("\"op\":\"mount\""), std::string::npos)
      << readFile(requests);
}

// the mount table can't be read after the helper ended: its mount counts as still
// mounted (the refresh must not walk a mount that may still be there)
TEST_F(VfsConnectorTest, UnreadableMountTableKeepsTheMount)
{
  fakeHelper(R"([ "$1" = "--cleanup" ] && exit 0
[ "$1" = "--list-mounts" ] && exit 1
echo '{"event":"hello","version":"0"}'
read line
echo '{"event":"ready","roots":["/x/Data"]}'
sleep 0.2
exit 1
)");

  const auto vfs = createVfsConnector();
  int called     = 0;
  vfs->setDeactivatedCallback([&] {
    ++called;
  });

  vfs->activate();
  ASSERT_TRUE(vfs->isActive());

  EXPECT_FALSE(vfs->waitInactive(std::chrono::seconds(1)));
  processEvents(100);
  EXPECT_TRUE(vfs->isActive());
  EXPECT_EQ(called, 0);
  EXPECT_TRUE(vfs->staleMounts().contains("/x/Data"));
  const auto reason = vfs->takeLostReason();
  EXPECT_TRUE(reason.contains("mount table")) << reason.toStdString();

  // still unknown: not mounted over
  EXPECT_THROW(vfs->activate(), std::runtime_error);
  EXPECT_TRUE(vfs->isActive());
}

// the helper ended between activate() and spawn(): the program is not started without
// the virtual file system (it would see and write the real data directory)
TEST_F(VfsConnectorTest, HelperLostBeforeSpawnIsException)
{
  const auto count = m_state.path() / "count";
  fakeHelper(R"([ "$1" = "--cleanup" ] && exit 0
if [ "$1" = "--list-mounts" ]; then echo '{"roots":[]}'; exit 0; fi
n=$(( $(cat ')" +
             count.string() + R"(' 2>/dev/null || echo 0) + 1 ))
echo $n > ')" +
             count.string() + R"('
echo '{"event":"hello","version":"0"}'
if [ $n = 1 ]; then
  read line
  echo '{"event":"ready","roots":["/x/Data"]}'
  sleep 0.2
  exit 1
fi
while read line; do
  echo '{"event":"spawned","pid":4242}'
done
)");

  const auto vfs = createVfsConnector();
  VfsSpawnRequest r;
  r.argv             = {"true"};
  r.workingDirectory = "/";

  vfs->activate();
  EXPECT_TRUE(vfs->waitInactive(std::chrono::seconds(2)));

  try {
    vfs->spawn(r);
    FAIL() << "spawn() started the program without the virtual file system";
  } catch (const std::runtime_error& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("lost before the program started"), std::string::npos) << what;
    EXPECT_NE(what.find("mo2-vfs ended"), std::string::npos) << what;
  }
  EXPECT_EQ(readFile(count), "1\n");

  // the next Run, unhooked, starts a new helper
  EXPECT_EQ(vfs->spawn(r), 4242);
}

// the helper ended as it replied to the mount, and both were handled by the same pass
// of the event loop: activate() fails instead of leaving an active file system without
// a helper
TEST_F(VfsConnectorTest, HelperEndingAsItMountsIsException)
{
  const auto marker = m_state.path() / "mount-read";
  fakeHelper(R"([ "$1" = "--cleanup" ] && exit 0
if [ "$1" = "--list-mounts" ]; then echo '{"roots":[]}'; exit 0; fi
echo '{"event":"hello","version":"0"}'
read line
touch ')" + marker.string() +
             R"('
sleep 0.3
echo '{"event":"ready","roots":["/x/Data"]}'
exit 1
)");

  const auto vfs = createVfsConnector();
  int called     = 0;
  vfs->setDeactivatedCallback([&] {
    ++called;
  });

  // blocks the event loop of the mount request until the helper has replied and ended
  QTimer poll;
  poll.setInterval(5);
  QObject::connect(&poll, &QTimer::timeout, [&] {
    if (fs::exists(marker)) {
      poll.stop();
      ::usleep(800 * 1000);
    }
  });
  poll.start();

  EXPECT_THROW(vfs->activate(), std::runtime_error);
  EXPECT_FALSE(vfs->isActive());
  processEvents(100);
  EXPECT_EQ(called, 1);
}

// the helper runs in a session of its own: the signals of the terminal of Mod
// Organizer don't reach it
TEST_F(VfsConnectorTest, HelperHasItsOwnSession)
{
  const auto statFile = m_state.path() / "fake.stat";
  fakeHelper("[ \"$1\" = \"--cleanup\" ] && exit 0\n"
             "cat /proc/$$/stat > '" +
             statFile.string() +
             "'\n"
             "echo '{\"event\":\"hello\",\"version\":\"0\"}'\n"
             "read line\n"
             "echo '{\"event\":\"error\",\"code\":\"io\",\"message\":\"no\"}'\n"
             "read line\n");

  const auto vfs = createVfsConnector();
  EXPECT_THROW(vfs->activate(), std::runtime_error);

  const auto st = procStat(statFile);
  EXPECT_NE(st.session, 0);
  EXPECT_NE(st.session, ::getsid(0));
  EXPECT_NE(st.pgrp, ::getpgrp());
}

// refreshes started from the event loop of the mount request see an active file
// system (and are deferred)
TEST_F(VfsConnectorTest, ActiveWhileActivating)
{
  fakeHelper("[ \"$1\" = \"--cleanup\" ] && exit 0\n"
             "echo '{\"event\":\"hello\",\"version\":\"0\"}'\n"
             "read line\n"
             "sleep 0.5\n"
             "echo '{\"event\":\"ready\",\"roots\":[]}'\n"
             "read line\n"
             "echo '{\"event\":\"unmounted\"}'\n"
             "read line\n");

  const auto vfs = createVfsConnector();
  int called     = 0;
  vfs->setDeactivatedCallback([&] {
    ++called;
  });

  bool ran = false, activeInside = false;
  QTimer::singleShot(100, [&] {
    ran          = true;
    activeInside = vfs->isActive();
  });

  EXPECT_FALSE(vfs->isActive());
  vfs->activate();
  EXPECT_TRUE(ran);
  EXPECT_TRUE(activeInside);
  EXPECT_TRUE(vfs->isActive());

  vfs->releaseIfIdle();
  EXPECT_FALSE(vfs->isActive());
  processEvents(100);
  EXPECT_EQ(called, 1);
}

// a failed activation delivers the deactivation callback (the refresh deferred while
// activating runs)
TEST_F(VfsConnectorTest, FailedActivationCallsBack)
{
  fakeHelper("[ \"$1\" = \"--cleanup\" ] && exit 0\n"
             "echo '{\"event\":\"hello\",\"version\":\"0\"}'\n"
             "read line\n"
             "echo '{\"event\":\"error\",\"code\":\"io\",\"message\":\"no\"}'\n"
             "read line\n");

  const auto vfs = createVfsConnector();
  int called     = 0;
  vfs->setDeactivatedCallback([&] {
    ++called;
  });

  EXPECT_THROW(vfs->activate(), std::runtime_error);
  EXPECT_FALSE(vfs->isActive());
  processEvents(100);
  EXPECT_EQ(called, 1);
}

// the helper could not unmount when the watched programs ended: the file system stays
// active (no refresh over the mount) until the helper reports the unmount it retries
TEST_F(VfsConnectorTest, FailedAutoUnmountKeepsActive)
{
  fakeHelper("[ \"$1\" = \"--cleanup\" ] && exit 0\n"
             "echo '{\"event\":\"hello\",\"version\":\"0\"}'\n"
             "read line\n"
             "echo '{\"event\":\"ready\",\"roots\":[\"/x/Data\"]}'\n"
             "read line\n"
             "echo '{\"event\":\"ok\"}'\n"
             "echo '{\"event\":\"error\",\"code\":\"io\",\"message\":"
             "\"automatic unmount failed: boom\",\"path\":\"/x/Data\","
             "\"spontaneous\":true}'\n"
             "sleep 1\n"
             "echo '{\"event\":\"unmounted\"}'\n"
             "read line\n");

  const auto vfs = createVfsConnector();
  int called     = 0;
  vfs->setDeactivatedCallback([&] {
    ++called;
  });

  vfs->activate();
  vfs->watchProcessGroup(12345);
  ASSERT_TRUE(vfs->isActive());

  // the error is not the end of the file system
  processEvents(300);
  EXPECT_TRUE(vfs->isActive());
  EXPECT_EQ(called, 0);
  EXPECT_TRUE(vfs->staleMounts().contains("/x/Data"));
  EXPECT_FALSE(vfs->waitInactive(std::chrono::milliseconds(200)));

  // the retry worked
  EXPECT_TRUE(vfs->waitInactive(std::chrono::seconds(3)));
  processEvents(100);
  EXPECT_EQ(called, 1);
  EXPECT_TRUE(vfs->takeLostReason().contains("boom"));
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

TEST_F(VfsConnectorTest, HostBridgeRunsTheHelperThroughDistroboxHostExec)
{
  if (!requireVfs())
    GTEST_SKIP();

  const auto bin      = m_state.path() / "bin";
  const auto hostExec = bin / "distrobox-host-exec";
  const auto marker   = m_state.path() / "bridge-used";
  fs::create_directories(bin);
  {
    std::ofstream(hostExec) << "#!/bin/sh\ntouch '" << marker.string()
                            << "'\nexec \"$@\"\n";
  }
  fs::permissions(hostExec, fs::perms::owner_all);
  const auto oldPath = qgetenv("PATH");
  qputenv("PATH", QFile::encodeName(q(bin)) + ":" + oldPath);
  qputenv("MO2_VFS_BRIDGE", "host");

  Layout l;
  qputenv("MO2_VFS_BINARY", vfsBinary().toUtf8());
  {
    const auto vfs = createVfsConnector();
    Release release{*vfs};
    vfs->updateMapping(l.mapping());
    vfs->activate();
    EXPECT_TRUE(fs::exists(l.data / "a.esp"));
  }
  qputenv("PATH", oldPath);
  EXPECT_TRUE(fs::exists(marker));
}

TEST_F(VfsConnectorTest, ParamsSkipListsAreSent)
{
  if (!requireVfs())
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
