#include <gtest/gtest.h>

#include <QFile>
#include <QFileInfo>
#include <QString>

#include "linux/loot_linux.h"
#include "spawn.h"
#include "tempdir.h"

// starting programs is disabled on Linux until Phase 3; the message is shown by
// ProcessRunner::run() and spawn::startBinary()
TEST(Launch, UnavailableMessage)
{
  EXPECT_EQ(spawn::launchUnavailableMessage(),
            QString::fromUtf8("Executar programas ainda não está disponível no Linux. "
                              "Esta funcionalidade chega na Fase 3 do port."));
}

TEST(Launch, FileExecutionTypes)
{
  // the same types as on Windows, so the "Run" entries of the menus are the same, but
  // ProcessRunner::run() doesn't start anything
  EXPECT_EQ(spawn::getFileExecutionType(QFileInfo("/games/Skyrim/SkyrimSE.exe")),
            spawn::FileExecutionTypes::Executable);
  EXPECT_EQ(spawn::getFileExecutionType(QFileInfo("/games/Skyrim/tool.BAT")),
            spawn::FileExecutionTypes::Executable);
  EXPECT_EQ(spawn::getFileExecutionType(QFileInfo("/games/Skyrim/readme.txt")),
            spawn::FileExecutionTypes::Other);
}

TEST(Loot, LootcliIsInTheLootDirectory)
{
  EXPECT_EQ(lootcliPath("/opt/mo2/bin"), "/opt/mo2/bin/loot/lootcli");
}

TEST(Loot, UnavailableWithoutLootcli)
{
  TempDir tmp;
  const auto bin = QString::fromStdString(tmp.path().string());

  EXPECT_EQ(
      lootUnavailableReason(bin),
      QString::fromUtf8("LOOT ainda não está disponível nesta versão para Linux."));

  // not executable
  const auto lootcli = tmp.createFile("loot/lootcli");
  EXPECT_FALSE(lootUnavailableReason(bin).isEmpty());

  QFile::setPermissions(QString::fromStdString(lootcli.string()),
                        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  EXPECT_TRUE(lootUnavailableReason(bin).isEmpty());
}
