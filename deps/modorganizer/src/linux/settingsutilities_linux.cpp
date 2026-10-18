// Linux implementation of the credential functions of settingsutilities.cpp (the
// Windows ones use the Windows Credential Manager): the Secret Service of the desktop
// (KWallet, GNOME Keyring), or memory when there is none, see secretstore_linux.h and
// the spec of Phase 4a, section 3.

#include "../settingsutilities.h"
#include "secretstore_linux.h"

#include <QApplication>
#include <QMessageBox>
#include <QMetaObject>
#include <QThread>

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

  const auto show = [] {
    QMessageBox::warning(
        nullptr, QObject::tr("Login not saved"),
        QObject::tr("No secret store (KWallet, GNOME Keyring) is available: Mod "
                    "Organizer will forget this login when it closes."));
  };

  if (QThread::currentThread() == qApp->thread()) {
    show();
  } else {
    QMetaObject::invokeMethod(qApp, show, Qt::QueuedConnection);
  }
}
}  // namespace

bool deleteWindowsCredential(const QString& key)
{
  return secretstore::instance().remove(credentialName(key));
}

bool addWindowsCredential(const QString& key, const QString& data)
{
  auto& store   = secretstore::instance();
  const auto ok = store.set(credentialName(key), data);

  if (store.takeMemoryWarning()) {
    warnLoginNotKept();
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
