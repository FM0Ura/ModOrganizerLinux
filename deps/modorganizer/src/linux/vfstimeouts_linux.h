#ifndef VFSTIMEOUTS_LINUX_H
#define VFSTIMEOUTS_LINUX_H

#include <chrono>

// the timeout of the mount and update requests of mo2-vfs: 30 s; MO2_VFS_TIMEOUT_MS
// (tests) can only shorten it
std::chrono::milliseconds vfsMountTimeout();

// how often a mount that could not be unmounted is checked again: 5 s;
// MO2_VFS_RECHECK_MS (tests) can only shorten it
std::chrono::milliseconds vfsRecheckInterval();

#endif  // VFSTIMEOUTS_LINUX_H
