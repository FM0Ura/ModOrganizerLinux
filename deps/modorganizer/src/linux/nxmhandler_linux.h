#ifndef NXMHANDLER_LINUX_H
#define NXMHANDLER_LINUX_H

#include "vfsbridge_linux.h"

#include <QString>
#include <QStringList>

#include <functional>

// Mod Organizer as the handler of nxm:// links on Linux: a desktop file that runs it
// with the link, made the default handler of x-scheme-handler/nxm with xdg-mime, run on
// the host through vfsbridge when Mod Organizer runs in a container (spec of Phase 4a,
// section 4). Replaces nxmhandler.exe of Windows.
namespace nxmhandler
{
// the name of the desktop file, which is also how xdg-mime names the handler; not
// modorganizer2-nxm-handler.desktop, the file the Wine installer of Mod Organizer 2
// writes
inline constexpr char DesktopFileName[] = "org.modorganizer.ModOrganizer2.nxm.desktop";
inline constexpr char MimeType[]        = "x-scheme-handler/nxm";

// what decides how the desktop file starts Mod Organizer
struct Environment
{
  // $APPIMAGE (Phase 5)
  QString appImage;
  // $CONTAINER_ID, set by distrobox
  QString containerId;
  // vfsbridge::mode() is Host
  bool inContainer = false;
  // QCoreApplication::applicationFilePath()
  QString executable;
};

Environment currentEnvironment();

// one argument of an Exec line, quoted by the rules of the Desktop Entry Specification
// (in double quotes when it has a reserved character, with ", `, $ and \ escaped; %
// doubled); the string escapes of the file are applied by desktopFileContent()
QString quoteExecArgument(const QString& arg);

// the program and arguments that start Mod Organizer, without the link: the AppImage,
// `distrobox enter <container> -- <executable>` in a container, or the executable;
// throws std::runtime_error in a container that is not a distrobox (no CONTAINER_ID)
QStringList launcher(const Environment& env);

// the whole desktop file; throws like launcher()
QString desktopFileContent(const Environment& env);

enum class Action
{
  None,
  WriteFile,
  WriteFileAndSetDefault
};

// what to do given the current default handler (the output of `xdg-mime query
// default`, trimmed here): table of section 4.2 of the spec
Action decide(const QString& currentDefault, bool force);

// $XDG_DATA_HOME/applications and the desktop file in it
QString applicationsDir();
QString desktopFilePath();

// `xdg-mime query default x-scheme-handler/nxm`, `xdg-mime default <file>
// x-scheme-handler/nxm` and `update-desktop-database <dir>`, through
// vfsbridge::helperCommand() (on the host in a container; throws like it)
vfsbridge::Command queryCommand();
vfsbridge::Command setDefaultCommand();
vfsbridge::Command updateDatabaseCommand(const QString& dir);

// ok is false only on an error, with its cause in message; nothing changed (the
// handler of another program, without force) is a success
using Done = std::function<void(bool ok, const QString& message)>;

// registers Mod Organizer for nxm:// links (decide(), then the desktop file, `xdg-mime
// default` and `update-desktop-database`, whose failure is only logged). Asynchronous:
// `done` is always called later from the event loop of the application, never from
// inside this call.
void registerHandler(bool force, Done done);

// logs the current handler and the Exec line of our desktop file (debug), for
// NexusSettings::dump(); waits up to 5 seconds for xdg-mime
void dump();
}  // namespace nxmhandler

#endif  // NXMHANDLER_LINUX_H
