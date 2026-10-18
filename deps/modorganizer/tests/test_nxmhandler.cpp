#include "../src/linux/nxmhandler_linux.h"
#include "tempdir.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

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
  EXPECT_EQ(decide("modorganizer2-nxm-handler.desktop", false), Action::WriteFile);
  EXPECT_EQ(decide(" modorganizer2-nxm-handler.desktop\n", false), Action::WriteFile);
  EXPECT_EQ(decide("vortex-nxm.desktop", false), Action::None);

  EXPECT_EQ(decide("", true), Action::WriteFileAndSetDefault);
  EXPECT_EQ(decide("modorganizer2-nxm-handler.desktop", true),
            Action::WriteFileAndSetDefault);
  EXPECT_EQ(decide("vortex-nxm.desktop", true), Action::WriteFileAndSetDefault);
}

TEST(NxmHandler, Paths)
{
  TempDir data;
  ScopedEnv xdg("XDG_DATA_HOME", data.path().c_str());
  const auto dir = QString::fromStdString(data.path().string()) + "/applications";
  EXPECT_EQ(nxmhandler::applicationsDir(), dir);
  EXPECT_EQ(nxmhandler::desktopFilePath(), dir + "/modorganizer2-nxm-handler.desktop");
}

TEST(NxmHandler, CommandsDirect)
{
  ScopedEnv bridge("MO2_VFS_BRIDGE", "direct");

  auto c = nxmhandler::queryCommand();
  EXPECT_EQ(c.program, "xdg-mime");
  EXPECT_EQ(c.arguments, (QStringList{"query", "default", "x-scheme-handler/nxm"}));

  c = nxmhandler::setDefaultCommand();
  EXPECT_EQ(c.program, "xdg-mime");
  EXPECT_EQ(c.arguments, (QStringList{"default", "modorganizer2-nxm-handler.desktop",
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
