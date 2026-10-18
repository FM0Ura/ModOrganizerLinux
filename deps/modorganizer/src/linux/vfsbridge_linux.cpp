// How Mod Organizer starts mo2-vfs (vfsbridge_linux.h): directly, or on the host
// through distrobox-host-exec when it runs in a container, so that the mounts and the
// programs are those of the host (spec of Phase 3, section 6.1: in the mo2-run
// container, /home and /var/home are two mounts and a FUSE mount covers only one of
// them).

#include "vfsbridge_linux.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <stdexcept>
#include <utility>

#include <uibase/log.h>

using namespace MOBase;

namespace vfsbridge
{
Mode mode(const QString& rootDir)
{
  const auto v = qEnvironmentVariable("MO2_VFS_BRIDGE");
  if (v == "direct") {
    return Mode::Direct;
  }
  if (v == "host") {
    return Mode::Host;
  }
  if (!v.isEmpty() && v != "auto") {
    // mode() is called for every start of the helper: once is enough
    static bool warned = false;
    if (!std::exchange(warned, true)) {
      log::warn("MO2_VFS_BRIDGE={} is not direct, host or auto; detecting", v);
    }
  }

  const QString root   = rootDir.endsWith('/') ? rootDir : rootDir + "/";
  const bool container = QFileInfo::exists(root + "run/.containerenv") ||
                         QFileInfo::exists(root + ".dockerenv");
  return container ? Mode::Host : Mode::Direct;
}

Command helperCommand(const QString& binary, const QStringList& args,
                      const QString& rootDir)
{
  if (mode(rootDir) == Mode::Direct) {
    return {binary, args};
  }

  const auto hostExec = QStandardPaths::findExecutable("distrobox-host-exec");
  if (hostExec.isEmpty()) {
    throw std::runtime_error(
        "cannot run programs on the host: Mod Organizer runs in a container and "
        "distrobox-host-exec was not found (set MO2_VFS_BRIDGE=direct to use the "
        "container's own mounts)");
  }

  return {hostExec, QStringList{binary} + args};
}

QString hostCanonicalPath(const QString& path, const QString& rootDir)
{
  QString real;

  if (mode(rootDir) == Mode::Host) {
    const auto hostRoot = QDir::cleanPath(rootDir + "/run/host");
    if (QFileInfo(hostRoot).isDir()) {
      const auto prefix = QFileInfo(hostRoot).canonicalFilePath();
      const auto inHost = QFileInfo(hostRoot + path).canonicalFilePath();

      if (!prefix.isEmpty() && inHost.startsWith(prefix)) {
        real = inHost.mid(prefix.size());
        if (real.isEmpty()) {
          real = "/";
        }
      }
    }
  }

  if (real.isEmpty()) {
    real = QFileInfo(path).canonicalFilePath();
  }

  return real.isEmpty() ? path : real;
}
}  // namespace vfsbridge
