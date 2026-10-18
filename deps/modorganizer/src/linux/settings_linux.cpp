// Linux implementations of the functions of settings.cpp that run nxmhandler.exe on
// Windows: Mod Organizer registers itself for nxm:// links with a desktop file and
// xdg-mime (nxmhandler_linux.h, spec of Phase 4a, section 4).

#include "../settings.h"
#include "nxmhandler_linux.h"

#include <QMessageBox>

#include <log.h>

using namespace MOBase;

void Settings::registerAsMODLHandler(bool force, bool includeNxm)
{
  // there is no modl:// handler on Linux (it only exists in nxmhandler.exe)
  if (includeNxm) {
    nexus().registerAsNXMHandler(force);
  }
}

void NexusSettings::registerAsNXMHandler(bool force)
{
  // force: the button of the Nexus settings, the user waits for an answer; otherwise
  // the registration at startup, which only logs
  nxmhandler::registerHandler(force, [force](bool ok, const QString& message) {
    if (ok) {
      if (force) {
        QMessageBox::information(
            nullptr, QObject::tr("nxm links"),
            QObject::tr("Mod Organizer now handles nxm:// links."));
      }
      return;
    }

    if (force) {
      QMessageBox::critical(
          nullptr, QObject::tr("Failed"),
          QObject::tr("Failed to register Mod Organizer for nxm:// links: %1")
              .arg(message));
    } else {
      log::warn("cannot register Mod Organizer for nxm:// links: {}", message);
    }
  });
}

void NexusSettings::dump() const
{
  nxmhandler::dump();
}
