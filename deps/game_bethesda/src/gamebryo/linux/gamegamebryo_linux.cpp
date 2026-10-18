// Linux implementations of the GameGamebryo functions that are in the _WIN32 blocks of
// gamegamebryo.cpp: there is no registry (every lookup is empty, the game detection
// relies on the Steam libraries, see identifyGamePath()), the known folders are the
// XDG folders of MOBase::getOptionalKnownFolder(), except the documents and local
// application data of the game, which are in its Proton prefix (protonUserFolder()),
// and the PE header of executables is read directly.

#include "gamegamebryo.h"

#include <uibase/linux/steam.h>
#include <uibase/log.h>
#include <uibase/utility.h>

#include <QDir>
#include <QFile>
#include <QtEndian>

#include <cstring>

WORD GameGamebryo::getArch(QString const& program) const
{
  // IMAGE_FILE_HEADER::Machine of the PE file, 0 if the file is not a PE file
  QFile file(gameDirectory().absoluteFilePath(program));
  if (!file.open(QIODevice::ReadOnly)) {
    return 0;
  }

  // IMAGE_DOS_HEADER: "MZ", e_lfanew at 0x3c
  const QByteArray dos = file.read(0x40);
  if (dos.size() != 0x40 || !dos.startsWith("MZ")) {
    return 0;
  }

  const auto peOffset = qFromLittleEndian<quint32>(dos.constData() + 0x3c);

  // "PE\0\0" followed by IMAGE_FILE_HEADER, which starts with Machine
  if (!file.seek(peOffset)) {
    return 0;
  }

  const QByteArray nt = file.read(6);
  if (nt.size() != 6 || std::memcmp(nt.constData(), "PE\0\0", 4) != 0) {
    return 0;
  }

  return qFromLittleEndian<quint16>(nt.constData() + 4);
}

std::unique_ptr<BYTE[]> GameGamebryo::getRegValue(HKEY, LPCWSTR, LPCWSTR, DWORD,
                                                  LPDWORD)
{
  // no registry
  return {};
}

QString GameGamebryo::findInRegistry(HKEY, LPCWSTR, LPCWSTR)
{
  // no registry
  return {};
}

QString GameGamebryo::getKnownFolderPath(REFKNOWNFOLDERID folderId, bool)
{
  // known folders are not redirected on Linux, the default is the folder itself; empty
  // for the folders without equivalent (FOLDERID_ProgramData)
  const QString path = MOBase::getOptionalKnownFolder(folderId);
  return path.isEmpty() ? QString() : QDir::fromNativeSeparators(path);
}

QString GameGamebryo::getSpecialPath(const QString&)
{
  // the "User Shell Folders" of the registry
  return {};
}

QString GameGamebryo::protonUserFolder() const
{
  const auto app = MOBase::steam::findApp(steamAPPId());
  if (!app || app->compatData.isEmpty()) {
    MOBase::log::warn("{}: no Proton prefix for Steam app {}, using the XDG folders",
                      gameName(), steamAPPId());
    return {};
  }
  return app->compatData + "/pfx/drive_c/users/steamuser";
}
