// Linux implementations of the Win32-backed uibase utilities (src/linux). Only built
// on Linux, see tests/CMakeLists.txt.

#include <gtest/gtest.h>

#include <cerrno>
#include <cstring>
#include <optional>

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QSize>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

#include <uibase/errorcodes.h>
#include <uibase/linux/peicon.h>
#include <uibase/log.h>
#include <uibase/registry.h>
#include <uibase/steamutility.h>
#include <uibase/utility.h>

using namespace MOBase;

namespace
{

// the functions under test log errors, which requires the default logger (created by
// the application at startup)
class LoggerEnvironment : public testing::Environment
{
public:
  void SetUp() override
  {
    log::LoggerConfiguration conf;
    conf.name     = "uibase-tests";
    conf.maxLevel = log::Debug;
    conf.pattern  = "%^[%Y-%m-%d %H:%M:%S.%e %L] %v%$";
    log::createDefault(conf);
  }
};

[[maybe_unused]] testing::Environment* const g_loggerEnvironment =
    testing::AddGlobalTestEnvironment(new LoggerEnvironment);

// sets an environment variable for the lifetime of the object, restores the previous
// value (or unsets it) afterwards
class ScopedEnv
{
public:
  ScopedEnv(const char* name, const QString& value) : m_name(name)
  {
    if (qEnvironmentVariableIsSet(name)) {
      m_old = qgetenv(name);
    }
    qputenv(name, value.toLocal8Bit());
  }

  ~ScopedEnv()
  {
    if (m_old) {
      qputenv(m_name, *m_old);
    } else {
      qunsetenv(m_name);
    }
  }

  ScopedEnv(const ScopedEnv&)            = delete;
  ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
  const char* m_name;
  std::optional<QByteArray> m_old;
};

void writeFile(const QString& path, const QByteArray& content)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  ASSERT_TRUE(f.open(QIODevice::WriteOnly)) << path.toStdString();
  ASSERT_EQ(f.write(content), content.size());
}

QByteArray readFile(const QString& path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  return f.readAll();
}

QString fixture(const char* name)
{
  return QString::fromUtf8(UIBASE_TEST_DATA_DIR) + "/" + name;
}

}  // namespace

// --- known folders -------------------------------------------------------------

TEST(KnownFolder, DocumentsMatchesQt)
{
  EXPECT_EQ(getKnownFolder(FOLDERID_Documents).path(),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
}

TEST(KnownFolder, ProgramFilesOptionalEmpty)
{
  EXPECT_TRUE(getOptionalKnownFolder(FOLDERID_ProgramFiles).isEmpty());
  EXPECT_TRUE(getOptionalKnownFolder(FOLDERID_ProgramFilesX86).isEmpty());
  EXPECT_TRUE(getOptionalKnownFolder(FOLDERID_Windows).isEmpty());
  EXPECT_TRUE(getOptionalKnownFolder(FOLDERID_OneDrive).isEmpty());
  EXPECT_THROW(getKnownFolder(FOLDERID_ProgramFiles), std::runtime_error);
}

TEST(KnownFolder, XdgDirectories)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ScopedEnv data("XDG_DATA_HOME", tmp.filePath("data"));
  ScopedEnv config("XDG_CONFIG_HOME", tmp.filePath("config"));

  EXPECT_EQ(getKnownFolder(FOLDERID_LocalAppData).path(), tmp.filePath("data"));
  EXPECT_EQ(getKnownFolder(FOLDERID_RoamingAppData).path(), tmp.filePath("config"));
  EXPECT_EQ(getStartMenuDirectory(), QDir::homePath() + "/.local/share/applications");
  EXPECT_EQ(getOptionalKnownFolder(FOLDERID_Desktop),
            QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
}

// --- shell operations ----------------------------------------------------------

TEST(ShellDelete, RecycleMovesToTrash)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ScopedEnv data("XDG_DATA_HOME", tmp.filePath("data"));

  const QString f = tmp.filePath("work/some file.txt");
  writeFile(f, "trash me");

  ASSERT_TRUE(shellDelete({f}, true)) << std::strerror(errno);

  EXPECT_FALSE(QFileInfo::exists(f));
  const QString trashed = tmp.filePath("data/Trash/files/some file.txt");
  EXPECT_EQ(readFile(trashed), "trash me");

  const QString info =
      readFile(tmp.filePath("data/Trash/info/some file.txt.trashinfo"));
  EXPECT_TRUE(info.startsWith("[Trash Info]\n")) << info.toStdString();
  // the path is percent-encoded (the space), '/' is kept
  const QString encoded =
      QString::fromUtf8(QUrl::toPercentEncoding(QFileInfo(f).absoluteFilePath(), "/"));
  EXPECT_TRUE(info.contains("\nPath=" + encoded + "\n")) << info.toStdString();
  EXPECT_TRUE(info.contains(
      QRegularExpression("\nDeletionDate=\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d\n")))
      << info.toStdString();
}

TEST(ShellDelete, RecycleKeepsBothOnNameCollision)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ScopedEnv data("XDG_DATA_HOME", tmp.filePath("data"));

  const QString a   = tmp.filePath("a/name.txt");
  const QString b   = tmp.filePath("b/name.txt");
  const QString dir = tmp.filePath("c/name.txt");  // a directory with the same name
  writeFile(a, "first");
  writeFile(b, "second");
  writeFile(dir + "/inner.txt", "third");

  ASSERT_TRUE(shellDelete({a, b, dir}, true)) << std::strerror(errno);

  const QDir files(tmp.filePath("data/Trash/files"));
  const QDir infos(tmp.filePath("data/Trash/info"));
  EXPECT_EQ(files.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(), 3);
  EXPECT_EQ(infos.entryList(QDir::Files).size(), 3);
  EXPECT_EQ(readFile(files.filePath("name.txt")), "first");
  EXPECT_EQ(readFile(files.filePath("name.txt.2")), "second");
  EXPECT_EQ(readFile(files.filePath("name.txt.3/inner.txt")), "third");
  EXPECT_TRUE(readFile(infos.filePath("name.txt.2.trashinfo"))
                  .contains("Path=" + QFile::encodeName(b) + "\n"));
}

TEST(ShellDelete, RecycleMissingFileFails)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ScopedEnv data("XDG_DATA_HOME", tmp.filePath("data"));

  errno = 0;
  EXPECT_FALSE(shellDelete({tmp.filePath("missing")}, true));
  EXPECT_EQ(errno, ENOENT);
}

TEST(ShellDelete, RecycleWithTrailingSlash)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ScopedEnv data("XDG_DATA_HOME", tmp.filePath("data"));

  writeFile(tmp.filePath("work/folder/inner.txt"), "inner");

  ASSERT_TRUE(shellDelete({tmp.filePath("work/folder/")}, true))
      << std::strerror(errno);

  EXPECT_FALSE(QFileInfo::exists(tmp.filePath("work/folder")));
  EXPECT_EQ(readFile(tmp.filePath("data/Trash/files/folder/inner.txt")), "inner");
  EXPECT_TRUE(
      readFile(tmp.filePath("data/Trash/info/folder.trashinfo"))
          .contains("Path=" + QFile::encodeName(tmp.filePath("work/folder")) + "\n"));
}

TEST(ShellDelete, PermanentRemovesTree)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ScopedEnv data("XDG_DATA_HOME", tmp.filePath("data"));

  writeFile(tmp.filePath("tree/a/b/c.txt"), "x");
  writeFile(tmp.filePath("single.txt"), "y");

  ASSERT_TRUE(shellDelete({tmp.filePath("tree"), tmp.filePath("single.txt")}, false));
  EXPECT_FALSE(QFileInfo::exists(tmp.filePath("tree")));
  EXPECT_FALSE(QFileInfo::exists(tmp.filePath("single.txt")));
  EXPECT_FALSE(QFileInfo::exists(tmp.filePath("data/Trash")));
}

TEST(ShellMove, CrossDirectory)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  const QString src = tmp.filePath("from/mod");
  writeFile(src + "/top.txt", "top");
  writeFile(src + "/meshes/level1.nif", "one");
  writeFile(src + "/meshes/deeper/level2.nif", "two");

  const QString dst = tmp.filePath("to/elsewhere/mod");
  QDir().mkpath(tmp.filePath("to/elsewhere"));

  ASSERT_TRUE(shellMove(src, dst)) << std::strerror(errno);

  EXPECT_FALSE(QFileInfo::exists(src));
  EXPECT_EQ(readFile(dst + "/top.txt"), "top");
  EXPECT_EQ(readFile(dst + "/meshes/level1.nif"), "one");
  EXPECT_EQ(readFile(dst + "/meshes/deeper/level2.nif"), "two");
}

TEST(ShellMove, IntoExistingDirectory)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  writeFile(tmp.filePath("a.txt"), "a");
  writeFile(tmp.filePath("b.txt"), "b");
  QDir().mkpath(tmp.filePath("target"));

  ASSERT_TRUE(shellMove(QStringList{tmp.filePath("a.txt"), tmp.filePath("b.txt")},
                        QStringList{tmp.filePath("target")}));
  EXPECT_EQ(readFile(tmp.filePath("target/a.txt")), "a");
  EXPECT_EQ(readFile(tmp.filePath("target/b.txt")), "b");
  EXPECT_FALSE(QFileInfo::exists(tmp.filePath("a.txt")));
}

TEST(ShellMove, CreatesMissingParentDirectories)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  writeFile(tmp.filePath("file.txt"), "file");
  writeFile(tmp.filePath("dir/inner.txt"), "inner");

  ASSERT_TRUE(shellMove(tmp.filePath("file.txt"), tmp.filePath("new/parents/file.txt")))
      << std::strerror(errno);
  EXPECT_EQ(readFile(tmp.filePath("new/parents/file.txt")), "file");
  EXPECT_FALSE(QFileInfo::exists(tmp.filePath("file.txt")));

  ASSERT_TRUE(shellMove(QStringList{tmp.filePath("dir")},
                        QStringList{tmp.filePath("other/parents/dir")}))
      << std::strerror(errno);
  EXPECT_EQ(readFile(tmp.filePath("other/parents/dir/inner.txt")), "inner");
  EXPECT_FALSE(QFileInfo::exists(tmp.filePath("dir")));
}

TEST(ShellCopy, CreatesMissingParentDirectories)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  writeFile(tmp.filePath("file.txt"), "file");
  writeFile(tmp.filePath("dir/inner.txt"), "inner");

  ASSERT_TRUE(shellCopy(tmp.filePath("file.txt"), tmp.filePath("a/b/c/file.txt")))
      << std::strerror(errno);
  EXPECT_EQ(readFile(tmp.filePath("a/b/c/file.txt")), "file");
  EXPECT_TRUE(QFileInfo::exists(tmp.filePath("file.txt")));

  ASSERT_TRUE(shellCopy(tmp.filePath("dir"), tmp.filePath("x/y/dir")))
      << std::strerror(errno);
  EXPECT_EQ(readFile(tmp.filePath("x/y/dir/inner.txt")), "inner");
}

TEST(ShellCopy, CopiesTreeAndWildcard)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  writeFile(tmp.filePath("src/x.txt"), "x");
  writeFile(tmp.filePath("src/sub/y.txt"), "y");

  ASSERT_TRUE(shellCopy(tmp.filePath("src"), tmp.filePath("copy")));
  EXPECT_EQ(readFile(tmp.filePath("copy/x.txt")), "x");
  EXPECT_EQ(readFile(tmp.filePath("copy/sub/y.txt")), "y");
  EXPECT_TRUE(QFileInfo::exists(tmp.filePath("src/sub/y.txt")));

  QDir().mkpath(tmp.filePath("flat"));
  ASSERT_TRUE(shellCopy(tmp.filePath("src/*"), tmp.filePath("flat"), true));
  EXPECT_EQ(readFile(tmp.filePath("flat/x.txt")), "x");
  EXPECT_EQ(readFile(tmp.filePath("flat/sub/y.txt")), "y");
}

TEST(ShellRename, RenamesAndReportsErrno)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());

  writeFile(tmp.filePath("old.txt"), "content");
  ASSERT_TRUE(shellRename(tmp.filePath("old.txt"), tmp.filePath("new.txt")));
  EXPECT_EQ(readFile(tmp.filePath("new.txt")), "content");

  errno = 0;
  EXPECT_FALSE(shellRename(tmp.filePath("old.txt"), tmp.filePath("other.txt")));
  EXPECT_EQ(errno, ENOENT);

  const auto r = shell::Rename(QFileInfo(tmp.filePath("old.txt")),
                               QFileInfo(tmp.filePath("other.txt")));
  EXPECT_FALSE(r.success());
  EXPECT_EQ(r.message(), QString::fromStdWString(formatSystemMessage(ENOENT)));
}

// --- INI files (WritePrivateProfileString) ---------------------------------------

TEST(Registry, WritesIni)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString path = tmp.filePath("test.ini");
  const auto wpath   = path.toStdWString();

  writeFile(path, "[General]\r\nfoo=1\r\n");
  ASSERT_TRUE(WriteRegistryValue(L"general", L"FOO", L"2", wpath.c_str()));
  EXPECT_EQ(readFile(path), "[General]\r\nfoo=2\r\n");

  ASSERT_TRUE(WriteRegistryValue(L"Display", L"iSize W", L"1920", wpath.c_str()));
  EXPECT_EQ(readFile(path), "[General]\r\nfoo=2\r\n[Display]\r\niSize W=1920\r\n");

  // new key in an existing section goes after the section's last entry
  ASSERT_TRUE(WriteRegistryValue(L"GENERAL", L"bar", L"x y", wpath.c_str()));
  EXPECT_EQ(readFile(path),
            "[General]\r\nfoo=2\r\nbar=x y\r\n[Display]\r\niSize W=1920\r\n");
}

TEST(Registry, KeepsLfAndOtherLines)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString path = tmp.filePath("lf.ini");
  const auto wpath   = path.toStdWString();

  writeFile(path, "; comment\n[A]\n  key = old  \nother=1\n\n[B]\nkey=b\n");
  ASSERT_TRUE(WriteRegistryValue(L"a", L"KEY", L"new", wpath.c_str()));
  EXPECT_EQ(readFile(path), "; comment\n[A]\n  key=new\nother=1\n\n[B]\nkey=b\n");

  ASSERT_TRUE(WriteRegistryValue(L"A", L"third", L"3", wpath.c_str()));
  EXPECT_EQ(readFile(path),
            "; comment\n[A]\n  key=new\nother=1\nthird=3\n\n[B]\nkey=b\n");
}

TEST(Registry, CreatesMissingFile)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString path = tmp.filePath("new.ini");
  const auto wpath   = path.toStdWString();

  ASSERT_TRUE(
      WriteRegistryValue(L"Launcher", L"bEnableFileSelection", L"1", wpath.c_str()));
  EXPECT_EQ(readFile(path), "[Launcher]\r\nbEnableFileSelection=1\r\n");
}

TEST(Registry, FailsWithoutDirectory)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const auto wpath = tmp.filePath("missing/dir/x.ini").toStdWString();

  errno = 0;
  EXPECT_FALSE(WriteRegistryValue(L"A", L"b", L"c", wpath.c_str()));
  EXPECT_EQ(errno, ENOENT);
}

// --- PE version resources --------------------------------------------------------

TEST(FileVersion, ReadsPe)
{
  EXPECT_EQ(getFileVersion(fixture("version.exe")), "1.2.3.4");
  EXPECT_EQ(getProductVersion(fixture("version.exe")), "5.6.7-fixture");

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString text = tmp.filePath("text.exe");
  writeFile(text, "this is not a PE file\n");
  EXPECT_EQ(getFileVersion(text), "");
  EXPECT_EQ(getProductVersion(text), "");
  EXPECT_EQ(getFileVersion(tmp.filePath("missing.exe")), "");
}

TEST(FileVersion, MalformedPeIsRejected)
{
  const QByteArray pe = readFile(fixture("version.exe"));
  ASSERT_FALSE(pe.isEmpty());

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString path = tmp.filePath("bad.exe");

  // every truncation must be handled without reading past the end of the file
  for (qsizetype n = 0; n < pe.size(); n += 7) {
    writeFile(path, pe.left(n));
    const QString v = getFileVersion(path);
    EXPECT_TRUE(v.isEmpty() || v == "1.2.3.4") << n;
    getProductVersion(path);
  }

  // corrupt every 32-bit field with large values (offsets, sizes, counts)
  for (qsizetype i = 0; i + 4 <= pe.size(); i += 2) {
    QByteArray bad = pe;
    bad[i] = bad[i + 1] = bad[i + 2] = bad[i + 3] = char(0xff);
    writeFile(path, bad);
    getFileVersion(path);
    getProductVersion(path);
  }
}

// --- PE icons ----------------------------------------------------------------------

TEST(PeIcon, ReadsGroup)
{
  const auto ico = details::readPeIcon(fixture("icon.exe"));
  ASSERT_TRUE(ico.has_value());
  ASSERT_GE(ico->size(), 6);
  EXPECT_EQ(ico->left(6), QByteArray("\0\0\1\0\2\0", 6));

  if (!QImageReader::supportedImageFormats().contains("ico")) {
    GTEST_SKIP() << "the ico image format plugin is not available";
  }

  QByteArray bytes = *ico;
  QBuffer buffer(&bytes);
  ASSERT_TRUE(buffer.open(QIODevice::ReadOnly));
  QImageReader reader(&buffer, "ico");
  ASSERT_EQ(reader.imageCount(), 2);

  QList<QSize> sizes;
  for (int i = 0; i < reader.imageCount(); ++i) {
    ASSERT_TRUE(reader.jumpToImage(i));
    const QImage image = reader.read();
    ASSERT_FALSE(image.isNull());
    sizes.append(image.size());
  }
  EXPECT_TRUE(sizes.contains(QSize(16, 16)));
  EXPECT_TRUE(sizes.contains(QSize(32, 32)));
}

TEST(PeIcon, NoIconResource)
{
  EXPECT_FALSE(details::readPeIcon(fixture("version.exe")).has_value());

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString text = tmp.filePath("text.exe");
  writeFile(text, "this is not a PE file\n");
  EXPECT_FALSE(details::readPeIcon(text).has_value());
  EXPECT_FALSE(details::readPeIcon(tmp.filePath("missing.exe")).has_value());
}

TEST(PeIcon, MalformedIsRejected)
{
  const QByteArray pe = readFile(fixture("icon.exe"));
  ASSERT_FALSE(pe.isEmpty());

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString path = tmp.filePath("bad.exe");

  const auto check = [&](const QByteArray& data) {
    writeFile(path, data);
    const auto ico = details::readPeIcon(path);
    if (ico) {
      EXPECT_TRUE(ico->startsWith(QByteArray("\0\0\1\0", 4)));
    }
  };

  for (qsizetype n = 0; n < pe.size(); n += 7) {
    check(pe.left(n));
  }

  for (qsizetype i = 0; i + 4 <= pe.size(); i += 2) {
    QByteArray bad = pe;
    bad[i] = bad[i + 1] = bad[i + 2] = bad[i + 3] = char(0xff);
    check(bad);
  }
}

// --- Steam -----------------------------------------------------------------------

TEST(Steam, FindsGameInLibrary)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ScopedEnv home("HOME", tmp.filePath("home"));

  const QString steam   = tmp.filePath("home/.local/share/Steam");
  const QString library = tmp.filePath("games/Steam Library");
  const QString game    = library + "/steamapps/common/Skyrim Special Edition";

  writeFile(steam + "/steamapps/libraryfolders.vdf",
            QString("\"libraryfolders\"\n{\n"
                    "\t\"0\"\n\t{\n\t\t\"path\"\t\t\"%1\"\n\t\t\"label\"\t\t\"\"\n"
                    "\t\t\"apps\"\n\t\t{\n\t\t\t\"228980\"\t\t\"123\"\n\t\t}\n\t}\n"
                    "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"%2\"\n\t}\n}\n")
                .arg(steam, library)
                .toUtf8());
  writeFile(game + "/SkyrimSE.exe", "MZ");

  EXPECT_EQ(findSteam(), steam);
  EXPECT_EQ(findSteamGame("Skyrim Special Edition", "SkyrimSE.exe"), game);
  EXPECT_EQ(findSteamGame("Skyrim Special Edition", "Missing.exe"), "");
  EXPECT_EQ(findSteamGame("Fallout 4", ""), "");
}

TEST(Steam, NoSteamInstalled)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  ScopedEnv home("HOME", tmp.filePath("home"));

  EXPECT_EQ(findSteam(), "");
  EXPECT_EQ(findSteamGame("Skyrim Special Edition", "SkyrimSE.exe"), "");
}

// --- forEachLineInFile (inline in utility.h) ------------------------------------

TEST(Utility, ForEachLineInFile)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString path = tmp.filePath("plugins.txt");
  writeFile(path, "a.esp\r\n  # comment\n\n  *b c.esm  \nlast");

  QStringList lines;
  ASSERT_TRUE(forEachLineInFile(path, [&](const QString& line) {
    lines.append(line);
  }));
  EXPECT_EQ(lines, QStringList({"a.esp", "*b c.esm", "last"}));

  EXPECT_FALSE(forEachLineInFile(tmp.filePath("missing.txt"), [](const QString&) {}));
}

TEST(Utility, ForEachLineInFileRefusesDirectory)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  QDir().mkpath(tmp.filePath("folder"));

  bool called = false;
  EXPECT_FALSE(forEachLineInFile(tmp.filePath("folder"), [&](const QString&) {
    called = true;
  }));
  EXPECT_FALSE(called);
}

// --- errors ----------------------------------------------------------------------

TEST(Errors, UseErrno)
{
  const QString msg = QString::fromStdWString(formatSystemMessage(ENOENT));
  EXPECT_TRUE(msg.startsWith(QString::fromUtf8(std::strerror(ENOENT))))
      << msg.toStdString();
  EXPECT_EQ(windowsErrorString(ENOENT), msg);
  EXPECT_EQ(std::wstring(errorCodeName(ENOENT)), L"ENOENT");
  EXPECT_EQ(std::wstring(errorCodeName(EACCES)), L"EACCES");
}
