#ifndef DISPLAYCHECK_LINUX_H
#define DISPLAYCHECK_LINUX_H

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// why Mod Organizer can't show its windows, checked before its QApplication exists
// (which aborts without a display): the Qt platform is the one of `-platform <p>` or
// `--platform <p>` in `args` (the command line, the program first), else
// QT_QPA_PLATFORM; with none, or one that starts with "wayland" or "xcb",
// WAYLAND_DISPLAY or DISPLAY must be set. Returns the message to show, or an empty
// string.
QString missingDisplayMessage(const QStringList& args, const QProcessEnvironment& env);

#endif  // DISPLAYCHECK_LINUX_H
