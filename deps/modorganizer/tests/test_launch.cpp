#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "linux/loot_linux.h"
#include "spawn.h"
#include "tempdir.h"

namespace fs = std::filesystem;

namespace
{
QString q(const fs::path& p)
{
  return QString::fromStdString(p.string());
}

fs::path writeFile(const fs::path& p, const std::string& content,
                   fs::perms perms = fs::perms::owner_read | fs::perms::owner_write)
{
  {
    std::ofstream out(p, std::ios::binary);
    out << content;
  }
  fs::permissions(p, perms);
  return p;
}

std::string readFile(const fs::path& p)
{
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
}  // namespace

// Windows programs are started by Run (ProcessRunner::run()); this is what
// spawn::startBinary() tells the other callers (LOOT, in Phase 4)
TEST(Launch, UnavailableMessage)
{
  EXPECT_EQ(spawn::launchUnavailableMessage(),
            QString("Windows programs are started with Run (or the executables list) "
                    "on Linux, which uses Proton."));
}

TEST(Launch, FileExecutionTypes)
{
  // the same types as on Windows, so the "Run" entries of the menus are the same
  EXPECT_EQ(spawn::getFileExecutionType(QFileInfo("/games/Skyrim/SkyrimSE.exe")),
            spawn::FileExecutionTypes::Executable);
  EXPECT_EQ(spawn::getFileExecutionType(QFileInfo("/games/Skyrim/tool.BAT")),
            spawn::FileExecutionTypes::Executable);
  EXPECT_EQ(spawn::getFileExecutionType(QFileInfo("/games/Skyrim/readme.txt")),
            spawn::FileExecutionTypes::Other);
}

// native programs: ELF executables and scripts, with the permission to execute them
TEST(Launch, NativeProgramDetection)
{
  TempDir tmp;
  const auto rwx = fs::perms::owner_all;

  const auto script = writeFile(tmp.path() / "tool.sh", "#!/bin/sh\nexit 0\n", rwx);
  EXPECT_TRUE(spawn::isNativeProgram(QFileInfo(q(script))));

  // the same script without +x
  const auto noExec = writeFile(tmp.path() / "noexec.sh", "#!/bin/sh\nexit 0\n");
  EXPECT_FALSE(spawn::isNativeProgram(QFileInfo(q(noExec))));

  // an ELF executable
  const auto elf = tmp.path() / "true";
  fs::copy_file("/bin/true", elf);
  fs::permissions(elf, rwx);
  EXPECT_TRUE(spawn::isNativeProgram(QFileInfo(q(elf))));

  // a Windows program, even with +x
  const auto exe = writeFile(tmp.path() / "a.exe", "MZ\x90\x00", rwx);
  EXPECT_FALSE(spawn::isNativeProgram(QFileInfo(q(exe))));

  // a directory, which is "executable"
  EXPECT_FALSE(spawn::isNativeProgram(QFileInfo(q(tmp.path()))));

  // nothing there
  EXPECT_FALSE(spawn::isNativeProgram(QFileInfo(q(tmp.path() / "missing"))));

  // native programs are executables for the menus and ProcessRunner::setFromFile(),
  // run as they are; the script without +x stays a document
  EXPECT_EQ(spawn::getFileExecutionType(QFileInfo(q(script))),
            spawn::FileExecutionTypes::Executable);
  const auto fec = spawn::getFileExecutionContext(nullptr, QFileInfo(q(script)));
  EXPECT_EQ(fec.type, spawn::FileExecutionTypes::Executable);
  EXPECT_EQ(fec.binary.absoluteFilePath(), q(script));
  EXPECT_TRUE(fec.arguments.isEmpty());
  EXPECT_EQ(spawn::getFileExecutionType(QFileInfo(q(noExec))),
            spawn::FileExecutionTypes::Other);
}

// the program runs in a process group of its own (the one mo2-vfs watches), in the
// working directory, with the arguments split like a command line
TEST(Launch, StartNativeInOwnProcessGroup)
{
  TempDir tmp;
  const auto out    = tmp.path() / "out.txt";
  const auto script = writeFile(tmp.path() / "tool.sh",
                                "#!/bin/sh\n"
                                "pwd -P > out.txt\n"
                                "echo \"$#\" >> out.txt\n"
                                "echo \"$1\" >> out.txt\n"
                                "sleep 0.5\n",
                                fs::perms::owner_all);

  spawn::SpawnParameters sp;
  sp.binary           = QFileInfo(q(script));
  sp.arguments        = "\"um arg\"";
  sp.currentDirectory = QDir(q(tmp.path()));

  const HANDLE h = spawn::startBinary(nullptr, sp);
  ASSERT_NE(h, INVALID_HANDLE_VALUE);

  const auto pid = static_cast<pid_t>(reinterpret_cast<std::intptr_t>(h));
  ASSERT_GT(pid, 0);

  // still sleeping: the leader of its own group, not in the group of the tests
  EXPECT_EQ(::getpgid(pid), pid);
  EXPECT_NE(::getpgid(pid), ::getpgid(0));

  int status = 0;
  ASSERT_EQ(::waitpid(pid, &status, 0), pid);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);

  EXPECT_EQ(readFile(out), fs::canonical(tmp.path()).string() + "\n1\num arg\n");
}

// .exe and the rest are not native programs: Run starts them with Proton
TEST(Launch, ExeIsNotNative)
{
  TempDir tmp;
  const auto exe =
      writeFile(tmp.path() / "SkyrimSE.exe", "MZ\x90\x00", fs::perms::owner_all);

  EXPECT_FALSE(spawn::isNativeProgram(QFileInfo(q(exe))));
  EXPECT_FALSE(spawn::isNativeProgram(QFileInfo("/games/Skyrim/SkyrimSE.exe")));
}

TEST(Launch, UnavailableMessageNoLongerMentionsPhase3)
{
  const auto m = spawn::launchUnavailableMessage();
  EXPECT_FALSE(m.contains("Fase 3"));
  EXPECT_FALSE(m.contains("Phase 3"));
  EXPECT_TRUE(m.contains("Run"));
}

TEST(Loot, LootcliIsInTheLootDirectory)
{
  EXPECT_EQ(lootcliPath("/opt/mo2/bin"), "/opt/mo2/bin/loot/lootcli");
}

TEST(Loot, UnavailableWithoutLootcli)
{
  TempDir tmp;
  const auto bin = QString::fromStdString(tmp.path().string());

  EXPECT_EQ(lootUnavailableReason(bin),
            QString("lootcli was not found in %1/loot.").arg(bin));

  // not executable
  const auto lootcli = tmp.createFile("loot/lootcli");
  EXPECT_FALSE(lootUnavailableReason(bin).isEmpty());

  QFile::setPermissions(QString::fromStdString(lootcli.string()),
                        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  EXPECT_TRUE(lootUnavailableReason(bin).isEmpty());
}
