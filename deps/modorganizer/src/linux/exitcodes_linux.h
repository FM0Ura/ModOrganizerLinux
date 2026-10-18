#ifndef EXITCODES_LINUX_H
#define EXITCODES_LINUX_H

#include <QtGlobal>
#include <chrono>
#include <optional>

// the exit codes of the programs started through mo2-vfs (VfsConnector::spawn()): they
// are not children of Mod Organizer, so waitpid() can't get them; the connector records
// them from the exited events of the helper
namespace exitcodes
{
// the exit code of a program started through the helper (its exited event)
void record(qint64 pid, int code);

// the recorded exit code of pid, removed; waits up to `timeout` for it, running the
// event loop of this thread (the event arrives on it); nullopt if it doesn't come
std::optional<int> take(qint64 pid, std::chrono::milliseconds timeout);
}  // namespace exitcodes

#endif  // EXITCODES_LINUX_H
