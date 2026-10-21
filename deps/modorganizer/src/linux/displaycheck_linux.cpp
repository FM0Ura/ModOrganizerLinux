#include "displaycheck_linux.h"

namespace
{
// the Qt platform asked for: the last -platform/--platform argument with a value, else
// QT_QPA_PLATFORM
QString platform(const QStringList& args, const QProcessEnvironment& env)
{
  QString fromArgs;
  for (qsizetype i = 1; i + 1 < args.size(); ++i) {
    if (args[i] == "-platform" || args[i] == "--platform") {
      fromArgs = args[i + 1];
    }
  }

  return (fromArgs.isEmpty() ? env.value("QT_QPA_PLATFORM") : fromArgs).trimmed();
}
}  // namespace

QString missingDisplayMessage(const QStringList& args, const QProcessEnvironment& env)
{
  const auto p = platform(args, env);

  const bool needsDisplay =
      p.isEmpty() || p.startsWith("wayland") || p.startsWith("xcb");

  if (!needsDisplay || !env.value("WAYLAND_DISPLAY").isEmpty() ||
      !env.value("DISPLAY").isEmpty()) {
    return {};
  }

  return "Mod Organizer needs a graphical session, but neither WAYLAND_DISPLAY nor "
         "DISPLAY is set. Run it from the desktop, or set WAYLAND_DISPLAY, "
         "XDG_RUNTIME_DIR, DISPLAY and XAUTHORITY to those of the desktop session.";
}
