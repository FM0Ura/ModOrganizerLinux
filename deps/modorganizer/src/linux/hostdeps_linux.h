#ifndef HOSTDEPS_LINUX_H
#define HOSTDEPS_LINUX_H

#include <QString>

#include <functional>
#include <vector>

// The programs of the host that Mod Organizer needs and that no AppImage can carry
// (spec of Phase 5, section 3.6): umu-run runs the Windows programs, fusermount3
// mounts the virtual file system of the mods.
namespace hostdeps
{
struct MissingHostTool
{
  // the program, as in the PATH
  QString name;
  // what it is for, as a phrase for the user
  QString purpose;
  // the anchor of its section in docs/user/en/install.md
  QString docAnchor;
};

// the tools that findExecutable() (the path of a program by name, empty when it is not
// found) does not find
std::vector<MissingHostTool>
missingHostTools(const std::function<QString(const QString&)>& findExecutable);

// the tools searched in the PATH of the host: Mod Organizer's own PATH, or, in a
// container (vfsbridge::mode() is Host), the PATH of the host, through
// distrobox-host-exec. Empty when the program is not found. When the host can't be
// asked (Host mode and no distrobox-host-exec), `name` itself: the check fails open,
// the tool counts as present and the launch reports what really fails
QString findOnHost(const QString& name);

// the message for the user: which tools are missing, what they are for, and where the
// documentation says how to install them; empty when nothing is missing
QString missingHostToolsMessage(const std::vector<MissingHostTool>& missing);

// the page of the documentation with the instructions
extern const char HostDependenciesUrl[];
}  // namespace hostdeps

#endif  // HOSTDEPS_LINUX_H
