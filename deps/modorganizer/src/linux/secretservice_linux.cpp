// The Secret Service through libsecret (secretservice_linux.h). Built on its own,
// without the precompiled header and without Qt (see src/CMakeLists.txt).

#include "secretservice_linux.h"

#include <libsecret/secret.h>

namespace secretservice
{
namespace
{
  const SecretSchema& schema()
  {
    static const SecretSchema s = [] {
      SecretSchema r{};
      r.name          = "org.modorganizer.ModOrganizer2";
      r.flags         = SECRET_SCHEMA_NONE;
      r.attributes[0] = {"key", SECRET_SCHEMA_ATTRIBUTE_STRING};
      // attributes[1] stays zeroed: the end of the list
      return r;
    }();
    return s;
  }

  // Unavailable for the errors of reaching the service (no bus, nobody owns
  // org.freedesktop.secrets): the caller falls back to memory for the session. Failed
  // for everything else, which can go away (a locked wallet, a dismissed prompt, an
  // error the service returned).
  Status classify(const GError* e)
  {
    if (e->domain == G_IO_ERROR) {
      switch (e->code) {
      case G_IO_ERROR_CANCELLED:
      case G_IO_ERROR_DBUS_ERROR:  // an unregistered error returned by the service
      case G_IO_ERROR_TIMED_OUT:
      case G_IO_ERROR_PERMISSION_DENIED:
        return Status::Failed;
      default:
        return Status::Unavailable;
      }
    }

    if (e->domain == G_SPAWN_ERROR) {
      return Status::Unavailable;
    }

    if (e->domain == G_DBUS_ERROR) {
      switch (e->code) {
      case G_DBUS_ERROR_SERVICE_UNKNOWN:
      case G_DBUS_ERROR_NAME_HAS_NO_OWNER:
      case G_DBUS_ERROR_NO_SERVER:
      case G_DBUS_ERROR_DISCONNECTED:
      case G_DBUS_ERROR_FILE_NOT_FOUND:
      case G_DBUS_ERROR_SPAWN_EXEC_FAILED:
      case G_DBUS_ERROR_SPAWN_FORK_FAILED:
      case G_DBUS_ERROR_SPAWN_CHILD_EXITED:
      case G_DBUS_ERROR_SPAWN_CHILD_SIGNALED:
      case G_DBUS_ERROR_SPAWN_FAILED:
      case G_DBUS_ERROR_SPAWN_SERVICE_INVALID:
      case G_DBUS_ERROR_SPAWN_SERVICE_NOT_FOUND:
      case G_DBUS_ERROR_SPAWN_FILE_INVALID:
        return Status::Unavailable;
      default:
        return Status::Failed;
      }
    }

    return Status::Failed;
  }

  // consumes `e`
  Reply fromError(GError* e)
  {
    Reply r;
    r.status = classify(e);
    r.error  = e->message ? e->message : "unknown error";
    g_error_free(e);
    return r;
  }
}  // namespace

Reply lookup(const std::string& name)
{
  GError* e = nullptr;
  gchar* v =
      secret_password_lookup_sync(&schema(), nullptr, &e, "key", name.c_str(), nullptr);

  if (e) {
    if (v) {
      secret_password_free(v);
    }
    return fromError(e);
  }

  if (!v) {
    return {Status::NotFound, {}, {}};
  }

  Reply r{Status::Ok, v, {}};
  secret_password_free(v);
  return r;
}

Reply store(const std::string& name, const std::string& label, const std::string& value)
{
  GError* e     = nullptr;
  const auto ok = secret_password_store_sync(&schema(), SECRET_COLLECTION_DEFAULT,
                                             label.c_str(), value.c_str(), nullptr, &e,
                                             "key", name.c_str(), nullptr);

  if (e) {
    return fromError(e);
  }

  if (!ok) {
    return {Status::Failed, {}, "the Secret Service did not store the item"};
  }

  return {Status::Ok, {}, {}};
}

Reply clear(const std::string& name)
{
  GError* e = nullptr;
  const auto removed =
      secret_password_clear_sync(&schema(), nullptr, &e, "key", name.c_str(), nullptr);

  if (e) {
    return fromError(e);
  }

  return {removed ? Status::Ok : Status::NotFound, {}, {}};
}
}  // namespace secretservice
