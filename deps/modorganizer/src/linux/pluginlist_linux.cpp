// Linux implementation of PluginList::saveLoadOrder() of pluginlist.cpp (the Windows
// one sets the file times with CreateFile()/SetFileTime()).
//
// For the games whose load order is the order of the modification times of the
// plugins, each plugin gets the same time as on Windows (a day per priority, from
// 1999-01-01), set with utimensat(); the access time is left alone. There is no
// mandatory file locking on Linux, so a running game never makes this fail.

#include "../pluginlist.h"
#include "../shared/directoryentry.h"
#include "../shared/fileentry.h"
#include "../shared/filesorigin.h"
#include "../shared/util.h"
#include "../shared/windows_error.h"

#include <uibase/iplugingame.h>
#include <uibase/log.h>

#include <QDir>
#include <QFile>
#include <QObject>

#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>

using namespace MOBase;
using namespace MOShared;

namespace
{

// seconds between 1601-01-01 (FILETIME) and 1970-01-01 (Unix time)
constexpr std::int64_t FileTimeEpochOffset = 11'644'473'600;

timespec toTimespec(FILETIME ft)
{
  const auto v = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) |
                 static_cast<std::uint64_t>(ft.dwLowDateTime);

  timespec t;
  t.tv_sec  = static_cast<time_t>(static_cast<std::int64_t>(v / 10'000'000) -
                                  FileTimeEpochOffset);
  t.tv_nsec = static_cast<long>((v % 10'000'000) * 100);

  return t;
}

}  // namespace

bool PluginList::saveLoadOrder(DirectoryEntry& directoryStructure)
{
  if (m_GamePlugin->loadOrderMechanism() != IPluginGame::LoadOrderMechanism::FileTime) {
    // nothing to do
    return true;
  }

  log::debug("setting file times on esps");

  for (ESPInfo& esp : m_ESPs) {
    std::wstring espName         = ToWString(esp.name);
    const FileEntryPtr fileEntry = directoryStructure.findFile(espName);
    if (fileEntry.get() != nullptr) {
      bool archive       = false;
      const int originid = fileEntry->getOrigin(archive);

      const QString fileName =
          QDir(ToQString(directoryStructure.getOriginByID(originid).getPath()))
              .filePath(esp.name);

      // same times as on Windows
      const std::uint64_t temp =
          (145731ULL + esp.priority) * 24 * 60 * 60 * 10000000ULL;

      FILETIME newWriteTime;
      newWriteTime.dwLowDateTime  = static_cast<DWORD>(temp & 0xFFFFFFFF);
      newWriteTime.dwHighDateTime = static_cast<DWORD>(temp >> 32);

      const timespec times[2] = {{0, UTIME_OMIT}, toTimespec(newWriteTime)};

      if (::utimensat(AT_FDCWD, QFile::encodeName(fileName).constData(), times, 0) !=
          0) {
        throw windows_error(QObject::tr("failed to set file time %1")
                                .arg(fileName)
                                .toUtf8()
                                .constData());
      }

      esp.time = newWriteTime;
      fileEntry->setFileTime(newWriteTime);
    }
  }

  return true;
}
