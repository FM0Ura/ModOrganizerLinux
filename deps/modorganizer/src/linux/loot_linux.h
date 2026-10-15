#ifndef MODORGANIZER_LOOT_LINUX_H
#define MODORGANIZER_LOOT_LINUX_H

// Linux parts of loot.cpp (src/linux/loot_linux.cpp): where lootcli is, whether LOOT
// can be used, and the pipe that receives the output of lootcli.

#include "../envmodule.h"

#include <QCoreApplication>
#include <QString>

#include <string>

class QAbstractButton;

// the lootcli executable, <bin>/loot/lootcli
//
QString
lootcliPath(const QString& binDirectory = QCoreApplication::applicationDirPath());

// why LOOT can't be used, or an empty string if it can: lootcli is not part of the
// Linux builds yet, LOOT is available when an executable lootcli is in the loot
// directory
//
QString lootUnavailableReason(
    const QString& binDirectory = QCoreApplication::applicationDirPath());

// disables the given button, with the reason as its tooltip, if LOOT can't be used
//
void disableIfLootUnavailable(QAbstractButton* button);

// receives the output of lootcli; on Windows, a named pipe in loot.cpp, here a pipe
// whose write end is given to lootcli as its stdout
//
class AsyncPipe
{
public:
  AsyncPipe() = default;
  ~AsyncPipe();

  AsyncPipe(const AsyncPipe&)            = delete;
  AsyncPipe& operator=(const AsyncPipe&) = delete;

  // creates the pipe and returns its write end; the HANDLE is the file descriptor,
  // which this pipe owns (env::HandlePtr doesn't close anything on Linux); empty on
  // failure
  //
  env::HandlePtr create();

  // what has been written to the pipe since the last call, without blocking
  //
  std::string read();

private:
  int m_read  = -1;
  int m_write = -1;
};

#endif  // MODORGANIZER_LOOT_LINUX_H
