#include <gtest/gtest.h>

#include "linux/hostdeps_linux.h"

#include <QFile>
#include <QTemporaryDir>

using namespace hostdeps;

namespace
{
struct ScopedEnv
{
  ScopedEnv(const char* name, const QByteArray& value)
      : m_name(name), m_had(qEnvironmentVariableIsSet(name)), m_old(qgetenv(name))
  {
    qputenv(name, value);
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
  // the real lookup, in Mod Organizer's own PATH; Direct, because the tests may run in
  // a container without distrobox-host-exec (the CI's docker run)
  ScopedEnv bridge("MO2_VFS_BRIDGE", "direct");
  EXPECT_FALSE(findOnHost("sh").isEmpty());
  EXPECT_TRUE(findOnHost("mo2-no-such-program").isEmpty());
}

TEST(HostDeps, UncheckableHostCountsAsPresent)
{
  // in a container (Host) without distrobox-host-exec, nothing can be looked up on the
  // host: the tool is assumed to be there (its name is returned), so that no false
  // "missing" message is shown; the launch itself reports what really fails
  QTemporaryDir emptyPath;
  ASSERT_TRUE(emptyPath.isValid());
  ScopedEnv bridge("MO2_VFS_BRIDGE", "host");
  ScopedEnv path("PATH", QFile::encodeName(emptyPath.path()));

  EXPECT_EQ(findOnHost("mo2-no-such-program"), "mo2-no-such-program");
  EXPECT_TRUE(missingHostTools(findOnHost).empty());
}
