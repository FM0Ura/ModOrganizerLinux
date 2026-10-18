#ifndef SECRETSTORE_LINUX_H
#define SECRETSTORE_LINUX_H

#include "secretservice_linux.h"

#include <QMutex>
#include <QString>

#include <map>
#include <memory>

// Where the logins of Mod Organizer (Nexus API key and OAuth tokens, Steam password)
// are kept on Linux: the Secret Service of the desktop, or memory when there is none
// (spec of Phase 4a, section 3.2). Used by the credential functions of
// settingsutilities_linux.cpp.
namespace secretstore
{
// the operations of the Secret Service; a fake replaces it in the tests
class Backend
{
public:
  virtual ~Backend() = default;

  virtual secretservice::Reply lookup(const QString& name)                      = 0;
  virtual secretservice::Reply store(const QString& name, const QString& value) = 0;
  virtual secretservice::Reply clear(const QString& name)                       = 0;
};

// libsecret (secretservice_linux.h); nullptr when built without it
// (MO2_WITH_LIBSECRET off)
std::unique_ptr<Backend> libsecretBackend();

class Store
{
public:
  // a null backend: memory only
  explicit Store(std::unique_ptr<Backend> backend);

  // empty when there is no such value, or when the Secret Service failed (logged)
  QString get(const QString& name);

  // false when the Secret Service failed (logged); a value kept in memory is a success
  bool set(const QString& name, const QString& value);

  // removing what does not exist is a success
  bool remove(const QString& name);

  // true once the session uses memory: no backend, or the Secret Service answered
  // Unavailable; it is not tried again until Mod Organizer restarts
  bool inMemory() const;

  // true exactly once per session: after the first value stored in memory, when the
  // user has to be told that the login will not be kept
  bool takeMemoryWarning();

private:
  std::unique_ptr<Backend> m_backend;
  mutable QMutex m_mutex;
  bool m_memory;
  bool m_warningGiven   = false;
  bool m_warningPending = false;
  std::map<QString, QString> m_values;

  // switches to memory if `r` says the service is unavailable; true if it did
  bool fallBack(const secretservice::Reply& r);
};

// the store of the application, over libsecretBackend()
Store& instance();
}  // namespace secretstore

#endif  // SECRETSTORE_LINUX_H
