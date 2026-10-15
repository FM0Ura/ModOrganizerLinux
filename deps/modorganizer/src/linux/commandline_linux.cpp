// Linux implementation of LaunchCommand::SpawnWaitProcess() of commandline.cpp (the
// Windows one starts the program with CreateProcessW() and waits for it).
//
// Starting programs is disabled on Linux until the virtual file system exists (Phase
// 3): the internal "launch" command fails like a program that could not be started.

#include "../commandline.h"

#include <iostream>

namespace cl
{

int LaunchCommand::SpawnWaitProcess(LPCWSTR, LPCWSTR commandLine)
{
  std::wcerr << L"cannot start '" << (commandLine ? commandLine : L"")
             << L"': starting programs is not supported on Linux yet\n";

  return -1;
}

}  // namespace cl
