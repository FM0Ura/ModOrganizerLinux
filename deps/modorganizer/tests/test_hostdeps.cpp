#include <gtest/gtest.h>

#include "linux/hostdeps_linux.h"

using namespace hostdeps;

namespace
{
QString findAll(const QString& name)
{
  return "/usr/bin/" + name;
}
}  // namespace

TEST(HostDeps, NothingMissing)
{
  EXPECT_TRUE(missingHostTools(findAll).empty());
  EXPECT_TRUE(missingHostToolsMessage({}).isEmpty());
}

TEST(HostDeps, UmuRunMissing)
{
  const auto missing = missingHostTools(
      [](const QString& name) { return name == "umu-run" ? QString() : findAll(name); });

  ASSERT_EQ(missing.size(), 1u);
  EXPECT_EQ(missing[0].name, "umu-run");
  EXPECT_FALSE(missing[0].purpose.isEmpty());
}

TEST(HostDeps, MessageNamesEveryTool)
{
  const auto missing = missingHostTools([](const QString&) { return QString(); });
  ASSERT_EQ(missing.size(), 2u);

  const auto text = missingHostToolsMessage(missing);
  EXPECT_TRUE(text.contains("umu-run")) << text.toStdString();
  EXPECT_TRUE(text.contains("fusermount3")) << text.toStdString();
  EXPECT_TRUE(text.contains("https://github.com/FM0Ura/ModOrganizerLinux/blob/main/"
                            "docs/user/en/install.md#host-dependencies"))
      << text.toStdString();
}

TEST(HostDeps, FindsThingsInThePath)
{
  // the real lookup, in Mod Organizer's own PATH (Direct) or the host's
  EXPECT_FALSE(findOnHost("sh").isEmpty());
  EXPECT_TRUE(findOnHost("mo2-no-such-program").isEmpty());
}
