// Linux implementation of the credential functions of settingsutilities.cpp (the
// Windows ones use the Windows Credential Manager): the Secret Service of the desktop
// (KWallet, GNOME Keyring), or memory when there is none, see secretstore_linux.h and
// the spec of Phase 4a, section 3.

#include "../settingsutilities.h"
#include "secretstore_linux.h"

#include <QApplication>
#include <QMessageBox>
#include <QMetaObject>

#include <cerrno>

// in settingsutilities.cpp
QString credentialName(const QString& key);

namespace
{
// once per session, after the first login kept in memory only
void warnLoginNotKept()
{
  if (!qApp) {
    return;
  }

  // always queued: the callers (NXMAccessManager::saveRefreshedTokens, ...) are in the
  // middle of an update and must not run a nested event loop
  QMetaObject::invokeMethod(
      qApp,
      [] {
        QMessageBox::warning(
            nullptr, QObject::tr("Login not saved"),
            QObject::tr("No secret store (KWallet, GNOME Keyring) is available: Mod "
                        "Organizer will forget this login when it closes."));
      },
      Qt::QueuedConnection);
}
}  // namespace

bool deleteWindowsCredential(const QString& key)
{
  if (!secretstore::instance().remove(credentialName(key))) {
    errno = EIO;
    return false;
  }

  return true;
}

bool addWindowsCredential(const QString& key, const QString& data)
{
  auto& store   = secretstore::instance();
  const auto ok = store.set(credentialName(key), data);

  if (store.takeMemoryWarning()) {
    warnLoginNotKept();
  }

  if (!ok) {
    errno = EIO;
  }

  return ok;
}

QString getWindowsCredential(const QString& key)
{
  const auto value = secretstore::instance().get(credentialName(key));

  if (value.isEmpty()) {
    errno = ENOENT;
  }

  return value;
}
