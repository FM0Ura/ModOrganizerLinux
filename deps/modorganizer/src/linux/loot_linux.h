#ifndef MODORGANIZER_LOOT_LINUX_H
#define MODORGANIZER_LOOT_LINUX_H

// Linux parts of loot.cpp (src/linux/loot_linux.cpp; the members of Loot are in
// src/linux/lootrun_linux.cpp): where lootcli is, whether LOOT can be used, where the
// files of a run go, how mo2-vfs starts lootcli and the file that receives its output
// (spec of Phase 4b, section 3.4).

#include "../envmodule.h"
#include "../vfsconnector.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <string>

class QAbstractButton;

// the lootcli executable, <bin>/loot/lootcli
//
QString
lootcliPath(const QString& binDirectory = QCoreApplication::applicationDirPath());

// why LOOT can't be used, or an empty string if it can: it can when an executable
// lootcli is in the loot directory
//
QString lootUnavailableReason(
    const QString& binDirectory = QCoreApplication::applicationDirPath());

// disables the given button, with the reason as its tooltip, if LOOT can't be used
//
void disableIfLootUnavailable(QAbstractButton* button);

// the directory of the files of a LOOT run (the output of lootcli, its report and the
// sorted load order), created if needed: the logs of the instance, <dataPath>/logs,
// which the host sees too when Mod Organizer runs in a container (whose /tmp may not be
// the host's); the temporary directory without a data path (the tests)
//
QString
lootFilesDirectory(const QString& dataPath = qApp->property("dataPath").toString());

// a run of lootcli
//
struct LootcliRun
{
  QString lootcli;               // the executable, lootcliPath()
  QString game;                  // IPluginGame::lootGameName()
  QString gamePath;              // the game directory, where mo2-vfs mounts
  QString pluginListPath;        // <profile>/loadorder.txt
  QString logLevel;              // lootcli::logLevelToString()
  QString reportPath;            // lootreport.json
  QString sortedPluginListPath;  // the sorted load order
  QString outputPath;            // the output of lootcli, read by AsyncPipe
  QString language;
  bool skipUpdateMasterlist = false;
};

// how mo2-vfs starts lootcli: the arguments of the Windows command line as a list
// (without its quotes), every path as the host sees it (vfsbridge::hostCanonicalPath(),
// the directory of a file, which may not exist yet), the loot directory as working
// directory, the output to outputPath, no Wine prefix
//
VfsSpawnRequest lootcliSpawnRequest(const LootcliRun& run,
                                    const QString& rootDir = "/");

// the last `max` lines of the output of lootcli that are not messages of its protocol
// (lootcli/lootcli.h): what the dynamic loader or a crash wrote; shown when it fails
//
QStringList lootcliOtherOutput(const QString& outputPath, int max = 20);

// receives the output of lootcli; on Windows, a named pipe in loot.cpp; here the file
// mo2-vfs writes it to (lootcli runs where mo2-vfs runs, maybe on the host: there is no
// pipe between them)
//
class AsyncPipe
{
public:
  explicit AsyncPipe(QString path = lootFilesDirectory() + "/lootcli.log");
  ~AsyncPipe();

  AsyncPipe(const AsyncPipe&)            = delete;
  AsyncPipe& operator=(const AsyncPipe&) = delete;

  // creates (or empties) the file and opens it for reading; the HANDLE is the file
  // descriptor, which this pipe owns (env::HandlePtr doesn't close anything on Linux);
  // empty on failure
  //
  env::HandlePtr create();

  // what has been written to the file since the last call, without blocking
  //
  std::string read();

  // the file
  //
  const QString& path() const { return m_path; }

private:
  QString m_path;
  int m_fd = -1;
};

#endif  // MODORGANIZER_LOOT_LINUX_H
