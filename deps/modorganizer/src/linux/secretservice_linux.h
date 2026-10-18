#ifndef SECRETSERVICE_LINUX_H
#define SECRETSERVICE_LINUX_H

#include <string>

// The Secret Service of the desktop (KWallet, GNOME Keyring) through the synchronous
// API of libsecret (spec of Phase 4a, section 3.1). This header has neither Qt nor
// GLib: the implementation is built without the precompiled header of the application,
// since GLib's headers do not compile under Qt's `signals` macro. Values are UTF-8.
namespace secretservice
{
enum class Status
{
  Ok,
  // lookup: no such item; clear: nothing was removed
  NotFound,
  // no session bus, or no Secret Service on it
  Unavailable,
  // any other error (locked collection, dismissed prompt, error of the service)
  Failed
};

struct Reply
{
  Status status = Status::Failed;
  // lookup: the secret
  std::string value;
  // the message of the error, for Unavailable and Failed
  std::string error;
};

// items are found by the attribute `key` = name in the schema
// org.modorganizer.ModOrganizer2
Reply lookup(const std::string& name);

// in the default collection, replacing an existing item
Reply store(const std::string& name, const std::string& label,
            const std::string& value);

Reply clear(const std::string& name);
}  // namespace secretservice

#endif  // SECRETSERVICE_LINUX_H
