// Logins of Mod Organizer in the Secret Service, or in memory when there is none
// (secretstore_linux.h, spec of Phase 4a, section 3).

#include "secretstore_linux.h"

#include <QMutexLocker>
#include <utility>

#include <uibase/log.h>

using namespace MOBase;
using secretservice::Reply;
using secretservice::Status;

namespace secretstore
{
namespace
{
#ifdef MO2_WITH_LIBSECRET
  class LibsecretBackend : public Backend
  {
  public:
    Reply lookup(const QString& name) override
    {
      return secretservice::lookup(name.toStdString());
    }

    Reply store(const QString& name, const QString& value) override
    {
      return secretservice::store(name.toStdString(),
                                  ("Mod Organizer 2: " + name).toStdString(),
                                  value.toStdString());
    }

    Reply clear(const QString& name) override
    {
      return secretservice::clear(name.toStdString());
    }
  };
#endif
}  // namespace

std::unique_ptr<Backend> libsecretBackend()
{
#ifdef MO2_WITH_LIBSECRET
  return std::make_unique<LibsecretBackend>();
#else
  return nullptr;
#endif
}

Store::Store(std::unique_ptr<Backend> backend)
    : m_backend(std::move(backend)), m_memory(m_backend == nullptr)
{
  if (m_memory) {
    log::warn("built without a secret store: logins are kept in memory until Mod "
              "Organizer exits");
  }
}

bool Store::fallBack(const Reply& r)
{
  if (r.status != Status::Unavailable) {
    return false;
  }

  m_memory = true;
  log::warn("the Secret Service is not available ({}): logins are kept in memory "
            "until Mod Organizer exits",
            r.error);

  return true;
}

QString Store::get(const QString& name)
{
  QMutexLocker lock(&m_mutex);

  if (!m_memory) {
    const auto r = m_backend->lookup(name);
    switch (r.status) {
    case Status::Ok:
      return QString::fromStdString(r.value);
    case Status::NotFound:
      return {};
    case Status::Failed:
      log::error("cannot read {} from the Secret Service: {}", name, r.error);
      return {};
    case Status::Unavailable:
      fallBack(r);
      break;
    }
  }

  const auto it = m_values.find(name);
  return it == m_values.end() ? QString() : it->second;
}

bool Store::set(const QString& name, const QString& value)
{
  QMutexLocker lock(&m_mutex);

  if (!m_memory) {
    const auto r = m_backend->store(name, value);
    if (r.status == Status::Ok) {
      log::debug("stored {} in the Secret Service", name);
      return true;
    }
    if (!fallBack(r)) {
      log::error("cannot store {} in the Secret Service: {}", name, r.error);
      return false;
    }
  }

  m_values[name] = value;
  log::debug("set credential {} (kept in memory until Mod Organizer exits)", name);

  if (!std::exchange(m_warningGiven, true)) {
    m_warningPending = true;
  }

  return true;
}

bool Store::remove(const QString& name)
{
  QMutexLocker lock(&m_mutex);

  if (!m_memory) {
    const auto r = m_backend->clear(name);
    if (r.status == Status::Ok || r.status == Status::NotFound) {
      if (r.status == Status::Ok) {
        log::debug("deleted {} from the Secret Service", name);
      }
      return true;
    }
    if (!fallBack(r)) {
      log::error("cannot delete {} from the Secret Service: {}", name, r.error);
      return false;
    }
  }

  m_values.erase(name);
  return true;
}

bool Store::inMemory() const
{
  QMutexLocker lock(&m_mutex);
  return m_memory;
}

bool Store::takeMemoryWarning()
{
  QMutexLocker lock(&m_mutex);
  return std::exchange(m_warningPending, false);
}

Store& instance()
{
  static Store store(libsecretBackend());
  return store;
}
}  // namespace secretstore
