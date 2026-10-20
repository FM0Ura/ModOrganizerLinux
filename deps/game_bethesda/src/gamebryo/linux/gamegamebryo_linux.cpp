// Linux implementations of the GameGamebryo functions that are in the _WIN32 blocks of
// gamegamebryo.cpp: there is no registry (every lookup is empty, the game detection
// relies on the Steam libraries, see identifyGamePath()), the known folders are the
// XDG folders of MOBase::getOptionalKnownFolder(), except the documents and local
// application data of the game, which are in its Proton prefix (protonUserFolder()),
// and the PE header of executables is read directly. Also the Linux-only parts of the
// profile INIs: file names found ignoring case and the repair of empty profile INIs.

#include "gamegamebryo.h"

#include <uibase/iprofile.h>
#include <uibase/linux/steam.h>
#include <uibase/log.h>
#include <uibase/registry.h>
#include <uibase/utility.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
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
  // not a Steam game (GOG, Epic, ...)
  const QString appId = steamAPPId();
  if (appId.isEmpty()) {
    return {};
  }

  // detectGame() runs for every game plugin, installed or not
  const auto app = MOBase::steam::findApp(appId);
  if (!app) {
    MOBase::log::debug("{}: Steam app {} is not installed, using the XDG folders",
                       gameName(), appId);
    return {};
  }

  if (app->compatData.isEmpty()) {
    MOBase::log::warn("{}: Steam app {} has no Proton prefix, run the game once from "
                      "Steam to create it; using the XDG folders for now",
                      gameName(), appId);
    return {};
  }

  return app->compatData + "/pfx/drive_c/users/steamuser";
}

QString GameGamebryo::resolveIgnoringCase(const QString& filePath)
{
  const QFileInfo info(filePath);
  if (info.exists()) {
    return filePath;
  }

  // only the file name: the folders (My Games/<game>, the game folder) have their case
  const QDir dir = info.dir();
  for (const QString& name : dir.entryList(QDir::AllEntries | QDir::Hidden |
                                           QDir::System | QDir::NoDotAndDotDot)) {
    if (name.compare(info.fileName(), Qt::CaseInsensitive) == 0) {
      return dir.filePath(name);
    }
  }

  return filePath;
}

void GameGamebryo::repairProfileInis(const MOBase::IProfile* profile) const
{
  // the profile INIs are only used with profile-specific INIs
  if (profile == nullptr || !profile->localSettingsEnabled()) {
    return;
  }

  initializeProfile(QDir(profile->absolutePath()), IPluginGame::CONFIGURATION);
}

namespace
{

enum class EmptyIni
{
  No,
  Empty,     // no content
  Launcher,  // only [Launcher] bEnableFileSelection=1
};

// whether the profile INI has no content other than what prepareIni() writes; spaces,
// line ends and the case are ignored
EmptyIni emptyProfileIni(const QString& path)
{
  QFile file(path);
  // what prepareIni() writes is 36 bytes
  if (file.size() > 1024 || !file.open(QIODevice::ReadOnly)) {
    return EmptyIni::No;
  }

  QByteArray content = file.readAll();
  if (content.startsWith("\xEF\xBB\xBF")) {
    content.remove(0, 3);
  }

  QList<QByteArray> lines;
  for (QByteArray line : content.split('\n')) {
    line = line.simplified().replace(" ", "").toLower();
    if (!line.isEmpty()) {
      lines.append(line);
    }
  }

  if (lines.isEmpty()) {
    return EmptyIni::Empty;
  }
  if (lines == QList<QByteArray>{"[launcher]", "benablefileselection=1"}) {
    return EmptyIni::Launcher;
  }
  return EmptyIni::No;
}

}  // namespace

void GameGamebryo::repairProfileIni(const QString& source, const QString& profileIni)
{
  // only INIs: an empty plugins.txt is a profile without plugins
  if (!profileIni.endsWith(".ini", Qt::CaseInsensitive)) {
    return;
  }

  const EmptyIni empty = emptyProfileIni(profileIni);
  if (empty == EmptyIni::No) {
    return;
  }

  QFile in(source);
  if (!in.open(QIODevice::ReadOnly)) {
    return;
  }
  const QByteArray content = in.readAll();
  if (content.isEmpty()) {
    return;
  }

  QSaveFile out(profileIni);
  if (!out.open(QIODevice::WriteOnly) || out.write(content) != content.size() ||
      !out.commit()) {
    MOBase::log::warn("cannot restore the empty profile INI '{}' from '{}': {}",
                      profileIni, source, out.errorString());
    return;
  }

  if (empty == EmptyIni::Launcher) {
    MOBase::WriteRegistryValue(L"Launcher", L"bEnableFileSelection", L"1",
                               profileIni.toStdWString().c_str());
  }

  MOBase::log::info("restored the empty profile INI '{}' from '{}'", profileIni,
                    source);
}
