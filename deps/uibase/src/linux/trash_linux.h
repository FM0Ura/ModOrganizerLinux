// The user's trash on Linux (freedesktop.org Trash specification 1.0), used by
// shellDelete(..., recycle=true).

#ifndef UIBASE_LINUX_TRASH_H
#define UIBASE_LINUX_TRASH_H

#include <QString>

namespace MOBase::details
{

// moves the file or directory `path` (a symbolic link itself, not its target) to the
// home trash, $XDG_DATA_HOME/Trash, writing its .trashinfo file; name collisions in
// Trash/files get a ".N" suffix, like GLib does
//
// only the home trash is supported: a file on another file system than
// $XDG_DATA_HOME is not moved (the per-mount $topdir/.Trash-$uid directories are not
// implemented) and the call fails with EXDEV instead of deleting it permanently, so the
// caller can decide what to do
//
// returns false and sets errno on failure
bool moveToTrash(const QString& path);

}  // namespace MOBase::details

#endif  // UIBASE_LINUX_TRASH_H
