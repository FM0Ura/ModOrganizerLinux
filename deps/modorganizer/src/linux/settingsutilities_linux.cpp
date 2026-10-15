// Linux implementation of the credential functions of settingsutilities.cpp (the
// Windows ones use the Windows Credential Manager).
//
// There is no secret store on Linux yet: credentials (Nexus API key and OAuth tokens,
// Steam password) are kept in memory for the lifetime of the process only, never
// written to disk in clear text; they have to be entered again after a restart.

#include "../settingsutilities.h"

#include <log.h>

#include <QMutex>
#include <QMutexLocker>

#include <cerrno>
#include <map>

using namespace MOBase;

// in settingsutilities.cpp
QString credentialName(const QString& key);

namespace
{

QMutex g_mutex;

std::map<QString, QString>& credentials()
{
  static std::map<QString, QString> map;
  return map;
}

}  // namespace

bool deleteWindowsCredential(const QString& key)
{
  const auto credName = credentialName(key);

  QMutexLocker lock(&g_mutex);

  // not an error if the key doesn't exist, like on Windows
  if (credentials().erase(credName) > 0) {
    log::debug("deleted credential {}", credName);
  }

  return true;
}

bool addWindowsCredential(const QString& key, const QString& data)
{
  const auto credName = credentialName(key);

  QMutexLocker lock(&g_mutex);
  credentials()[credName] = data;

  log::debug("set credential {} (kept in memory until Mod Organizer exits)", credName);

  return true;
}

QString getWindowsCredential(const QString& key)
{
  const auto credName = credentialName(key);

  QMutexLocker lock(&g_mutex);

  const auto itor = credentials().find(credName);
  if (itor == credentials().end()) {
    errno = ENOENT;
    return {};
  }

  return itor->second;
}
