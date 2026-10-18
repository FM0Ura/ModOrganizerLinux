#ifndef VFSBRIDGE_LINUX_H
#define VFSBRIDGE_LINUX_H

#include <QString>
#include <QStringList>

// How Mod Organizer starts the mo2-vfs helper: directly, or on the host through
// distrobox-host-exec when Mod Organizer runs in a container (the mounts and the
// programs are then those of the host: spec of Phase 3, section 6.1)
namespace vfsbridge
{
enum class Mode
{
  Direct,
  Host
};

// MO2_VFS_BRIDGE: "direct", "host", or empty/"auto" (anything else: a warning, then
// auto); auto is Host when <rootDir>/run/.containerenv or <rootDir>/.dockerenv exists
Mode mode(const QString& rootDir = "/");

struct Command
{
  QString program;
  QStringList arguments;
};

// how to run the helper `binary` with `args`: directly, or as
// `distrobox-host-exec <binary> <args>` in Host mode; throws std::runtime_error in Host
// mode when distrobox-host-exec is not in the PATH
Command helperCommand(const QString& binary, const QStringList& args,
                      const QString& rootDir = "/");
}  // namespace vfsbridge

#endif  // VFSBRIDGE_LINUX_H
