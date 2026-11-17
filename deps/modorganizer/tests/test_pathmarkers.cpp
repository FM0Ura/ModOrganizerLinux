#include <gtest/gtest.h>

#include "modlists/pathmarkers.h"

using namespace modlists;

namespace
{

PathMarkers sample()
{
  PathMarkers m;
  m.home     = "/home/u";
  m.game     = "/home/u/Games/Skyrim";
  m.instance = "/home/u/.local/share/ModOrganizer/SSE";
  return m;
}

}  // namespace

TEST(PathMarkers, LongestFirstAndBoundaries)
{
  EXPECT_EQ(toMarkers("/home/u/Games/Skyrim/Data;/home/u2/x;"
                      "/home/u/.local/share/ModOrganizer/SSE/mods",
                      sample()),
            "%GAME%/Data;/home/u2/x;%INSTANCE%/mods");
  EXPECT_EQ(toMarkers("\"/home/u\" '/home/u' /home/u", sample()),
            "\"%HOME%\" '%HOME%' %HOME%");
}

TEST(PathMarkers, WineForm)
{
  EXPECT_EQ(toMarkers("Z:\\home\\u\\Games\\Skyrim\\SkyrimSE.exe", sample()),
            "Z:%GAME%\\SkyrimSE.exe");
  EXPECT_EQ(toMarkers("z:\\home\\u\\x", sample()), "Z:%HOME%\\x");
  EXPECT_EQ(toMarkers("a=Z:\\home\\u\\Games\\Skyrim", sample()), "a=Z:%GAME%");
  EXPECT_EQ(toMarkers("\"Z:\\home\\u\\Games\\Skyrim\"", sample()), "\"Z:%GAME%\"");
  EXPECT_EQ(toMarkers("Z:\\HOME\\u\\x", sample()), "Z:\\HOME\\u\\x");
}

TEST(PathMarkers, EmptyFieldsSkipped)
{
  PathMarkers m;
  m.home = "/home/u";
  EXPECT_EQ(toMarkers("/home/u/a", m), "%HOME%/a");
}

TEST(PathMarkers, RoundTrip)
{
  PathMarkers m2;
  m2.home     = "/home/v";
  m2.game     = "/mnt/g/Skyrim";
  m2.instance = "/home/v/inst";
  const auto t = QStringLiteral(
      "/home/u/Games/Skyrim/Data;/home/u/.local/share/ModOrganizer/SSE/mods;/home/u/x");
  EXPECT_EQ(fromMarkers(toMarkers(t, sample()), m2),
            "/mnt/g/Skyrim/Data;/home/v/inst/mods;/home/v/x");
  EXPECT_EQ(fromMarkers(toMarkers("Z:\\home\\u\\Games\\Skyrim\\a.exe", sample()), m2),
            "Z:\\mnt\\g\\Skyrim\\a.exe");
}

TEST(PathMarkers, FindLeaks)
{
  const QString ini = "[a]\nx=1\nsPath=/home/fabio/x\ny=2\n";
  auto leaks        = findLeaks("f.ini", ini, "", "/home/fabio");
  ASSERT_EQ(leaks.size(), 1u);
  EXPECT_EQ(leaks[0].file, "f.ini");
  EXPECT_EQ(leaks[0].line, 3);
  EXPECT_EQ(leaks[0].excerpt, "sPath=/home/fabio/x");

  leaks = findLeaks("f.ini", "name=Fabio's mod\n", "fabio", "");
  ASSERT_EQ(leaks.size(), 1u);
  EXPECT_EQ(leaks[0].line, 1);

  EXPECT_TRUE(findLeaks("f.ini", "name=Fabiola\n", "fabio", "").empty());
  EXPECT_TRUE(findLeaks("f.ini", "name=ab\n", "ab", "").empty());
}

TEST(PathMarkers, WineRoundTripAtEveryBoundary)
{
  PathMarkers m2;
  m2.home     = "/home/v";
  m2.game     = "/mnt/g/Skyrim";
  m2.instance = "/home/v/inst";
  const char* in[]  = {"Path=Z:\\home\\u\\Games\\Skyrim",
                       "Path=\"Z:\\home\\u\\Games\\Skyrim\" x",
                       "Path=Z:\\home\\u\\Games\\Skyrim;other"};
  const char* out[] = {"Path=Z:\\mnt\\g\\Skyrim",
                       "Path=\"Z:\\mnt\\g\\Skyrim\" x",
                       "Path=Z:\\mnt\\g\\Skyrim;other"};
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(fromMarkers(toMarkers(in[i], sample()), m2), out[i]) << i;
  }
  EXPECT_EQ(fromMarkers("z:%GAME%", m2), "Z:\\mnt\\g\\Skyrim");
}

TEST(PathMarkers, LinuxPathBeforeBackslashStaysLinux)
{
  PathMarkers m2;
  m2.game = "/mnt/g/Skyrim";
  EXPECT_EQ(fromMarkers("%GAME%\\Data", m2), "/mnt/g/Skyrim\\Data");
}

TEST(PathMarkers, LeftBoundary)
{
  EXPECT_EQ(toMarkers("/mnt/home/u/x", sample()), "/mnt/home/u/x");
  EXPECT_EQ(toMarkers("abcZ:\\home\\u\\x", sample()), "abcZ:\\home\\u\\x");
  EXPECT_EQ(toMarkers("x=/home/u/x", sample()), "x=%HOME%/x");
}
