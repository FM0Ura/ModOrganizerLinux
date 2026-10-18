#include "../src/linux/nxmhandler_linux.h"
#include "tempdir.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTimer>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <tuple>

namespace fs = std::filesystem;
using nxmhandler::Action;
using nxmhandler::Environment;

namespace
{
// sets (or, with nullptr, unsets) an environment variable for the lifetime of the
// object
struct ScopedEnv
{
  ScopedEnv(const char* name, const char* value)
      : m_name(name), m_had(qEnvironmentVariableIsSet(name)), m_old(qgetenv(name))
  {
    if (value) {
      qputenv(name, value);
    } else {
      qunsetenv(name);
    }
  }
  ~ScopedEnv()
  {
    if (m_had) {
      qputenv(m_name, m_old);
    } else {
      qunsetenv(m_name);
    }
  }
  const char* m_name;
  bool m_had;
  QByteArray m_old;
};

Environment direct(const QString& exe)
{
  Environment e;
  e.executable = exe;
  return e;
}
}  // namespace

TEST(NxmHandler, QuoteExecArgument)
{
  using nxmhandler::quoteExecArgument;
  EXPECT_EQ(quoteExecArgument("/opt/mo2/bin/ModOrganizer"),
            "/opt/mo2/bin/ModOrganizer");
  EXPECT_EQ(quoteExecArgument("/home/u/Mod Organizer/ModOrganizer"),
            "\"/home/u/Mod Organizer/ModOrganizer\"");
  EXPECT_EQ(quoteExecArgument("/x/a$b\"c\\d`e"), "\"/x/a\\$b\\\"c\\\\d\\`e\"");
  EXPECT_EQ(quoteExecArgument("/x/100%"), "/x/100%%");
  EXPECT_EQ(quoteExecArgument("/x/Módulos/ModOrganizer"), "/x/Módulos/ModOrganizer");
  EXPECT_EQ(quoteExecArgument(""), "\"\"");
}

TEST(NxmHandler, LauncherDirect)
{
  EXPECT_EQ(nxmhandler::launcher(direct("/opt/mo2/bin/ModOrganizer")),
            QStringList{"/opt/mo2/bin/ModOrganizer"});
}

TEST(NxmHandler, LauncherDistrobox)
{
  Environment e = direct("/home/u/Mod Organizer/bin/ModOrganizer");
  e.inContainer = true;
  e.containerId = "mo2-run";
  EXPECT_EQ(nxmhandler::launcher(e),
            (QStringList{"distrobox", "enter", "mo2-run", "--",
                         "/home/u/Mod Organizer/bin/ModOrganizer"}));
}

TEST(NxmHandler, LauncherAppImageWins)
{
  Environment e = direct("/tmp/.mount_x/usr/bin/ModOrganizer");
  e.inContainer = true;
  e.containerId = "mo2-run";
  e.appImage    = "/home/u/Apps/ModOrganizer.AppImage";
  EXPECT_EQ(nxmhandler::launcher(e), QStringList{"/home/u/Apps/ModOrganizer.AppImage"});
}

TEST(NxmHandler, ContainerWithoutIdIsRefused)
{
  Environment e = direct("/opt/mo2/bin/ModOrganizer");
  e.inContainer = true;
  EXPECT_THROW(nxmhandler::launcher(e), std::runtime_error);
  EXPECT_THROW(nxmhandler::desktopFileContent(e), std::runtime_error);
}

TEST(NxmHandler, DesktopFileContent)
{
  Environment e = direct("/home/u/Mod Organizer/bin/ModOrganizer");
  e.inContainer = true;
  e.containerId = "mo2-run";

  EXPECT_EQ(
      nxmhandler::desktopFileContent(e),
      "[Desktop Entry]\n"
      "Type=Application\n"
      "Name=Mod Organizer 2 (nxm links)\n"
      "Comment=Download Nexus Mods links with Mod Organizer 2\n"
      "Exec=distrobox enter mo2-run -- \"/home/u/Mod Organizer/bin/ModOrganizer\" "
      "%u\n"
      "Terminal=false\n"
      "NoDisplay=true\n"
      "MimeType=x-scheme-handler/nxm;\n");
}

// the string escapes of the Desktop Entry Specification apply on top of the quoting of
// Exec: a backslash of the quoted argument is written twice
TEST(NxmHandler, DesktopFileEscapesBackslashes)
{
  const auto content = nxmhandler::desktopFileContent(direct("/x/a$b/ModOrganizer"));
  EXPECT_TRUE(content.contains("\nExec=\"/x/a\\\\$b/ModOrganizer\" %u\n"))
      << content.toStdString();
}

TEST(NxmHandler, Decide)
{
  using nxmhandler::decide;
  EXPECT_EQ(decide("", false), Action::WriteFileAndSetDefault);
  EXPECT_EQ(decide("  \n", false), Action::WriteFileAndSetDefault);
  EXPECT_EQ(decide("org.modorganizer.ModOrganizer2.nxm.desktop", false),
            Action::WriteFile);
  EXPECT_EQ(decide(" org.modorganizer.ModOrganizer2.nxm.desktop\n", false),
            Action::WriteFile);
  EXPECT_EQ(decide("vortex-nxm.desktop", false), Action::None);
  // the file of the Wine installer of Mod Organizer 2 is another handler, not ours
  EXPECT_EQ(decide("modorganizer2-nxm-handler.desktop", false), Action::None);

  EXPECT_EQ(decide("", true), Action::WriteFileAndSetDefault);
  EXPECT_EQ(decide("org.modorganizer.ModOrganizer2.nxm.desktop", true),
            Action::WriteFileAndSetDefault);
  EXPECT_EQ(decide("vortex-nxm.desktop", true), Action::WriteFileAndSetDefault);
  EXPECT_EQ(decide("modorganizer2-nxm-handler.desktop", true),
            Action::WriteFileAndSetDefault);
}

TEST(NxmHandler, Paths)
{
  TempDir data;
  ScopedEnv xdg("XDG_DATA_HOME", data.path().c_str());
  const auto dir = QString::fromStdString(data.path().string()) + "/applications";
  EXPECT_EQ(nxmhandler::applicationsDir(), dir);
  EXPECT_EQ(nxmhandler::desktopFilePath(),
            dir + "/org.modorganizer.ModOrganizer2.nxm.desktop");
}

TEST(NxmHandler, CommandsDirect)
{
  ScopedEnv bridge("MO2_VFS_BRIDGE", "direct");

  auto c = nxmhandler::queryCommand();
  EXPECT_EQ(c.program, "xdg-mime");
  EXPECT_EQ(c.arguments, (QStringList{"query", "default", "x-scheme-handler/nxm"}));

  c = nxmhandler::setDefaultCommand();
  EXPECT_EQ(c.program, "xdg-mime");
  EXPECT_EQ(c.arguments,
            (QStringList{"default", "org.modorganizer.ModOrganizer2.nxm.desktop",
                         "x-scheme-handler/nxm"}));

  c = nxmhandler::updateDatabaseCommand("/home/u/.local/share/applications");
  EXPECT_EQ(c.program, "update-desktop-database");
  EXPECT_EQ(c.arguments, QStringList{"/home/u/.local/share/applications"});
}

TEST(NxmHandler, CommandsHost)
{
  TempDir bin;
  const auto hostExec = bin.path() / "distrobox-host-exec";
  {
    std::ofstream(hostExec) << "#!/bin/sh\nexec \"$@\"\n";
  }
  fs::permissions(hostExec, fs::perms::owner_all);

  ScopedEnv bridge("MO2_VFS_BRIDGE", "host");
  ScopedEnv path("PATH", bin.path().c_str());

  const auto c = nxmhandler::queryCommand();
  EXPECT_EQ(c.program, QString::fromStdString(hostExec.string()));
  EXPECT_EQ(c.arguments,
            (QStringList{"xdg-mime", "query", "default", "x-scheme-handler/nxm"}));
}

TEST(NxmHandler, CurrentEnvironment)
{
  ScopedEnv bridge("MO2_VFS_BRIDGE", "host");
  ScopedEnv appImage("APPIMAGE", "/a/b.AppImage");
  ScopedEnv container("CONTAINER_ID", "box");

  const auto e = nxmhandler::currentEnvironment();
  EXPECT_EQ(e.appImage, "/a/b.AppImage");
  EXPECT_EQ(e.containerId, "box");
  EXPECT_TRUE(e.inContainer);
  EXPECT_FALSE(e.executable.isEmpty());
}

namespace
{
// a fake xdg-mime and update-desktop-database in a directory put first in the PATH:
// they append their arguments to calls.log; `xdg-mime query` prints the content of
// default.txt, `xdg-mime default` writes it
struct FakeTools
{
  TempDir dir;
  fs::path bin, data, log, defaultFile;

  FakeTools()
      : bin(dir.path() / "bin"), data(dir.path() / "data"),
        log(dir.path() / "calls.log"), defaultFile(dir.path() / "default.txt")
  {
    fs::create_directories(bin);
    script("xdg-mime", "echo \"xdg-mime $*\" >> '" + log.string() +
                           "'\n"
                           "case \"$1\" in\n"
                           "  query) cat '" +
                           defaultFile.string() +
                           "' 2>/dev/null; exit 0 ;;\n"
                           "  default) echo \"$2\" > '" +
                           defaultFile.string() +
                           "' ;;\n"
                           "esac\n");
    script("update-desktop-database",
           "echo \"update-desktop-database $*\" >> '" + log.string() + "'\n");
  }

  void script(const std::string& name, const std::string& body)
  {
    const auto p = bin / name;
    std::ofstream(p) << "#!/bin/sh\n" << body;
    fs::permissions(p, fs::perms::owner_all);
  }

  void setDefault(const std::string& handler)
  {
    std::ofstream(defaultFile) << handler << "\n";
  }

  QString calls() const
  {
    QFile f(QString::fromStdString(log.string()));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
  }

  QString desktopFile() const
  {
    QFile f(QString::fromStdString(
        (data / "applications" / "org.modorganizer.ModOrganizer2.nxm.desktop")
            .string()));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
  }

  std::string pathWithSh() const { return bin.string() + ":/usr/bin:/bin"; }
};

// runs registerHandler() and its event loop until `done` (10 s at most)
std::pair<bool, QString> registerAndWait(bool force)
{
  std::optional<std::pair<bool, QString>> r;
  bool synchronous = true;
  QEventLoop loop;

  nxmhandler::registerHandler(force, [&](bool ok, const QString& message) {
    r = {ok, message};
    loop.quit();
  });

  // the contract: never called from inside registerHandler()
  synchronous = r.has_value();
  EXPECT_FALSE(synchronous);

  if (!r) {
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
  }

  return r.value_or(std::pair<bool, QString>{false, "timeout"});
}
}  // namespace

TEST(NxmHandlerRegistration, NoHandlerWritesTheFileAndSetsTheDefault)
{
  FakeTools t;
  ScopedEnv bridge("MO2_VFS_BRIDGE", "direct");
  ScopedEnv appImage("APPIMAGE", nullptr);
  ScopedEnv path("PATH", t.pathWithSh().c_str());
  ScopedEnv xdg("XDG_DATA_HOME", t.data.c_str());

  const auto [ok, message] = registerAndWait(false);
  ASSERT_TRUE(ok) << message.toStdString();

  EXPECT_TRUE(t.desktopFile().contains(
      "Exec=" + nxmhandler::quoteExecArgument(QCoreApplication::applicationFilePath())))
      << t.desktopFile().toStdString();
  EXPECT_TRUE(t.calls().contains("xdg-mime query default x-scheme-handler/nxm"));
  EXPECT_TRUE(
      t.calls().contains("xdg-mime default org.modorganizer.ModOrganizer2.nxm.desktop "
                         "x-scheme-handler/nxm"));
  EXPECT_TRUE(t.calls().contains("update-desktop-database "));
}

TEST(NxmHandlerRegistration, OurHandlerOnlyRewritesTheFile)
{
  FakeTools t;
  ScopedEnv bridge("MO2_VFS_BRIDGE", "direct");
  ScopedEnv appImage("APPIMAGE", nullptr);
  ScopedEnv path("PATH", t.pathWithSh().c_str());
  ScopedEnv xdg("XDG_DATA_HOME", t.data.c_str());

  t.setDefault("org.modorganizer.ModOrganizer2.nxm.desktop");
  fs::create_directories(t.data / "applications");
  std::ofstream(t.data / "applications" / "org.modorganizer.ModOrganizer2.nxm.desktop")
      << "old\n";

  const auto [ok, message] = registerAndWait(false);
  ASSERT_TRUE(ok) << message.toStdString();

  EXPECT_TRUE(t.desktopFile().startsWith("[Desktop Entry]\n"));
  EXPECT_FALSE(t.calls().contains("xdg-mime default "));
}

TEST(NxmHandlerRegistration, OtherHandlerIsKeptUnlessForced)
{
  FakeTools t;
  ScopedEnv bridge("MO2_VFS_BRIDGE", "direct");
  ScopedEnv appImage("APPIMAGE", nullptr);
  ScopedEnv path("PATH", t.pathWithSh().c_str());
  ScopedEnv xdg("XDG_DATA_HOME", t.data.c_str());

  t.setDefault("vortex-nxm.desktop");

  auto [ok, message] = registerAndWait(false);
  ASSERT_TRUE(ok) << message.toStdString();
  EXPECT_TRUE(t.desktopFile().isEmpty());
  EXPECT_FALSE(t.calls().contains("xdg-mime default "));

  std::tie(ok, message) = registerAndWait(true);
  ASSERT_TRUE(ok) << message.toStdString();
  EXPECT_FALSE(t.desktopFile().isEmpty());
  EXPECT_TRUE(
      t.calls().contains("xdg-mime default org.modorganizer.ModOrganizer2.nxm.desktop "
                         "x-scheme-handler/nxm"));
}

TEST(NxmHandlerRegistration, MissingXdgMimeFails)
{
  TempDir empty, data;
  ScopedEnv bridge("MO2_VFS_BRIDGE", "direct");
  ScopedEnv appImage("APPIMAGE", nullptr);
  ScopedEnv path("PATH", empty.path().c_str());
  ScopedEnv xdg("XDG_DATA_HOME", data.path().c_str());

  const auto [ok, message] = registerAndWait(false);
  EXPECT_FALSE(ok);
  EXPECT_TRUE(message.contains("xdg-mime")) << message.toStdString();
  EXPECT_FALSE(fs::exists(data.path() / "applications"));
}

TEST(NxmHandlerRegistration, ContainerWithoutIdFails)
{
  FakeTools t;
  ScopedEnv bridge("MO2_VFS_BRIDGE", "host");
  ScopedEnv appImage("APPIMAGE", nullptr);
  ScopedEnv container("CONTAINER_ID", nullptr);
  ScopedEnv path("PATH", t.pathWithSh().c_str());
  ScopedEnv xdg("XDG_DATA_HOME", t.data.c_str());

  const auto [ok, message] = registerAndWait(true);
  EXPECT_FALSE(ok);
  EXPECT_TRUE(message.contains("CONTAINER_ID")) << message.toStdString();
  EXPECT_TRUE(t.calls().isEmpty());
}

TEST(NxmHandlerRegistration, HostModeGoesThroughDistroboxHostExec)
{
  FakeTools t;
  t.script("distrobox-host-exec",
           "echo \"host $*\" >> '" + t.log.string() + "'\nexec \"$@\"\n");
  ScopedEnv bridge("MO2_VFS_BRIDGE", "host");
  ScopedEnv appImage("APPIMAGE", nullptr);
  ScopedEnv container("CONTAINER_ID", "test-box");
  ScopedEnv path("PATH", t.pathWithSh().c_str());
  ScopedEnv xdg("XDG_DATA_HOME", t.data.c_str());

  const auto [ok, message] = registerAndWait(false);
  ASSERT_TRUE(ok) << message.toStdString();

  EXPECT_TRUE(t.calls().contains("host xdg-mime query default x-scheme-handler/nxm"));
  EXPECT_TRUE(t.desktopFile().contains("Exec=distrobox enter test-box -- "))
      << t.desktopFile().toStdString();
}
