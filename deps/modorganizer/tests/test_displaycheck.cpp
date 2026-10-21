// the display check of main() on Linux: QApplication aborts without a display, so the
// missing display is reported before it is created

#include <gtest/gtest.h>

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <initializer_list>
#include <utility>

#include "linux/displaycheck_linux.h"

namespace
{
QProcessEnvironment environment(std::initializer_list<std::pair<QString, QString>> vars)
{
  QProcessEnvironment env;
  for (const auto& [name, value] : vars) {
    env.insert(name, value);
  }
  return env;
}

const QStringList NoArgs{"ModOrganizer"};
}  // namespace

TEST(DisplayCheck, NoDisplayIsReported)
{
  const auto message = missingDisplayMessage(NoArgs, environment({}));
  EXPECT_TRUE(message.startsWith("Mod Organizer needs a graphical session"))
      << message.toStdString();
  EXPECT_TRUE(message.contains("XAUTHORITY")) << message.toStdString();
}

TEST(DisplayCheck, WaylandOrX11DisplayIsEnough)
{
  EXPECT_TRUE(
      missingDisplayMessage(NoArgs, environment({{"WAYLAND_DISPLAY", "wayland-0"}}))
          .isEmpty());
  EXPECT_TRUE(
      missingDisplayMessage(NoArgs, environment({{"DISPLAY", ":0"}})).isEmpty());
}

TEST(DisplayCheck, EmptyVariablesAreNoDisplay)
{
  EXPECT_FALSE(missingDisplayMessage(
                   NoArgs, environment({{"WAYLAND_DISPLAY", ""}, {"DISPLAY", ""}}))
                   .isEmpty());
}

TEST(DisplayCheck, PlatformsWithoutADisplayPass)
{
  for (const char* platform : {"offscreen", "minimal", "vnc"}) {
    EXPECT_TRUE(
        missingDisplayMessage(NoArgs, environment({{"QT_QPA_PLATFORM", platform}}))
            .isEmpty())
        << platform;
  }
}

TEST(DisplayCheck, WaylandAndXcbPlatformsNeedADisplay)
{
  for (const char* platform : {"wayland", "xcb", "wayland;xcb", "wayland-egl"}) {
    EXPECT_FALSE(
        missingDisplayMessage(NoArgs, environment({{"QT_QPA_PLATFORM", platform}}))
            .isEmpty())
        << platform;
  }
}

TEST(DisplayCheck, PlatformArgumentWinsOverTheVariable)
{
  EXPECT_TRUE(missingDisplayMessage({"ModOrganizer", "-platform", "offscreen"},
                                    environment({{"QT_QPA_PLATFORM", "xcb"}}))
                  .isEmpty());
  EXPECT_TRUE(missingDisplayMessage({"ModOrganizer", "--platform", "offscreen"},
                                    environment({}))
                  .isEmpty());
  EXPECT_FALSE(missingDisplayMessage({"ModOrganizer", "-platform", "xcb"},
                                     environment({{"QT_QPA_PLATFORM", "offscreen"}}))
                   .isEmpty());
}

TEST(DisplayCheck, PlatformArgumentWithoutValueIsIgnored)
{
  EXPECT_TRUE(missingDisplayMessage({"ModOrganizer", "-platform"},
                                    environment({{"QT_QPA_PLATFORM", "offscreen"}}))
                  .isEmpty());
}
