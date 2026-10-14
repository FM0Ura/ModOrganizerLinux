#include <gtest/gtest.h>

#include <sys/stat.h>

#include <QFile>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include "envshortcut.h"
#include "tempdir.h"

namespace
{

// sets an environment variable for the duration of a test
//
class ScopedEnv
{
public:
  ScopedEnv(const char* name, const QByteArray& value)
      : m_name(name), m_had(qEnvironmentVariableIsSet(name)), m_old(qgetenv(name))
  {
    qputenv(m_name, value);
  }

  ~ScopedEnv()
  {
    if (m_had) {
      qputenv(m_name, m_old);
    } else {
      qunsetenv(m_name);
    }
  }

private:
  const char* m_name;
  bool m_had;
  QByteArray m_old;
};

QStringList readLines(const QString& path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }

  return QString::fromUtf8(f.readAll()).split('\n');
}

}  // namespace

TEST(EnvShortcut, WritesDesktopEntry)
{
  TempDir tmp;
  ScopedEnv xdg("XDG_DATA_HOME", QByteArray::fromStdString(tmp.path().string()));

  const QString file = QString::fromStdString(
      (tmp.path() / "applications" / "Skyrim SE.desktop").string());

  env::Shortcut s;
  s.name("Skyrim SE")
      .target("/opt/mo2/bin/ModOrganizer")
      .arguments("\"moshortcut://:SKSE\"")
      .workingDirectory("/opt/mo2/bin");

  EXPECT_FALSE(s.exists(env::Shortcut::StartMenu));
  ASSERT_TRUE(s.add(env::Shortcut::StartMenu));
  ASSERT_TRUE(QFile::exists(file));

  const auto lines = readLines(file);
  EXPECT_EQ(lines.value(0), "[Desktop Entry]");
  EXPECT_TRUE(lines.contains("Type=Application"));
  EXPECT_TRUE(lines.contains("Name=Skyrim SE"));
  EXPECT_TRUE(lines.contains("Exec=/opt/mo2/bin/ModOrganizer \"moshortcut://:SKSE\""));
  EXPECT_TRUE(lines.contains("Path=/opt/mo2/bin"));

  // 0755
  struct stat st = {};
  ASSERT_EQ(::stat(file.toLocal8Bit().constData(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0755u);

  EXPECT_TRUE(s.exists(env::Shortcut::StartMenu));

  ASSERT_TRUE(s.remove(env::Shortcut::StartMenu));
  EXPECT_FALSE(QFile::exists(file));
  EXPECT_FALSE(s.exists(env::Shortcut::StartMenu));

  // nothing to remove anymore
  EXPECT_FALSE(s.remove(env::Shortcut::StartMenu));
}

TEST(EnvShortcut, EscapesExecAccordingToTheSpec)
{
  TempDir tmp;
  ScopedEnv xdg("XDG_DATA_HOME", QByteArray::fromStdString(tmp.path().string()));

  // the arguments are a command line, split like a shell would; each argument is
  // quoted, the program only if it needs to be; inside quotes, ", `, $ and \ are
  // escaped with a backslash, % is doubled; then, as in every string value, each
  // backslash is escaped again
  env::Shortcut s;
  s.name("Odd")
      .target("/opt/My Apps/mo2")
      .arguments(R"("a b" c$d 50% back\slash `cmd`)")
      .description("Run\nit")
      .icon("/opt/mo2/icon.png");

  ASSERT_TRUE(s.add(env::Shortcut::StartMenu));

  const auto lines = readLines(
      QString::fromStdString((tmp.path() / "applications" / "Odd.desktop").string()));

  EXPECT_TRUE(lines.contains(
      R"(Exec="/opt/My Apps/mo2" "a b" "c\\$d" "50%%" "back\\\\slash" "\\`cmd\\`")"))
      << lines.join("\n").toStdString();

  EXPECT_TRUE(lines.contains(R"(Comment=Run\nit)"));
  EXPECT_TRUE(lines.contains("Icon=/opt/mo2/icon.png"));
}

TEST(EnvShortcut, DesktopLocation)
{
  TempDir tmp;
  ScopedEnv home("HOME", QByteArray::fromStdString(tmp.path().string()));
  ScopedEnv config("XDG_CONFIG_HOME",
                   QByteArray::fromStdString((tmp.path() / ".config").string()));

  // $HOME/Desktop without a user-dirs.dirs
  const auto dir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
  ASSERT_EQ(dir, QString::fromStdString((tmp.path() / "Desktop").string()));

  env::Shortcut s;
  s.name("Game").target("/usr/bin/true");

  ASSERT_TRUE(s.add(env::Shortcut::Desktop));
  EXPECT_TRUE(QFile::exists(dir + "/Game.desktop")) << dir.toStdString();
  EXPECT_TRUE(s.exists(env::Shortcut::Desktop));
  EXPECT_TRUE(s.remove(env::Shortcut::Desktop));
}
