#ifndef PROTONLAUNCHER_LINUX_H
#define PROTONLAUNCHER_LINUX_H

#include "../vfsconnector.h"

#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <stdexcept>

// the launch of the programs started by Run on Linux: Windows programs through umu-run
// in the Proton prefix of their Steam game, native programs directly
namespace proton
{
// why a program can't be started; the message is for the user
class LaunchError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

struct LaunchParameters
{
  QFileInfo binary;
  QString arguments;         // one string, split like a command line
  QString workingDirectory;  // empty or "." for the directory of the binary
  QString steamAppId;        // of the executable; empty for the game's
  QString gameSteamAppId;    // of the managed game
  QString proton;            // internal or display name; empty for Steam's choice
  QStringList environment;   // NAME=value of the executable
  QString output;            // file for the output of the program, or empty
};

// .exe, .bat, .cmd and .com, ignoring case
bool isWindowsProgram(const QFileInfo& file);

// the NAME=value entries with a valid name ([A-Za-z_][A-Za-z0-9_]*); the others are
// logged and dropped
QStringList validEnvironment(const QStringList& environment);

// what to ask the helper to start: native programs directly, Windows programs through
// umu-run with the Steam and Proton environment; throws LaunchError
VfsSpawnRequest makeLaunch(const LaunchParameters& p);
}  // namespace proton

#endif  // PROTONLAUNCHER_LINUX_H
