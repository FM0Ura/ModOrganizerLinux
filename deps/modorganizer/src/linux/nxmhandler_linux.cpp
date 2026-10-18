// Mod Organizer as the handler of nxm:// links (nxmhandler_linux.h, spec of Phase 4a,
// section 4).

#include "nxmhandler_linux.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <stdexcept>

namespace nxmhandler
{
Environment currentEnvironment()
{
  Environment e;
  e.appImage    = qEnvironmentVariable("APPIMAGE");
  e.containerId = qEnvironmentVariable("CONTAINER_ID");
  e.inContainer = vfsbridge::mode() == vfsbridge::Mode::Host;
  e.executable  = QCoreApplication::applicationFilePath();
  return e;
}

QString quoteExecArgument(const QString& arg)
{
  static const QString reserved = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");

  QString escaped;
  bool quote = arg.isEmpty();

  for (const QChar c : arg) {
    if (reserved.contains(c)) {
      quote = true;
    }
    if (c == '"' || c == '`' || c == '$' || c == '\\') {
      escaped += '\\';
    }
    escaped += c;
  }

  escaped.replace("%", "%%");

  return quote ? '"' + escaped + '"' : escaped;
}

QStringList launcher(const Environment& env)
{
  if (!env.appImage.isEmpty()) {
    return {env.appImage};
  }

  if (env.inContainer) {
    if (env.containerId.isEmpty()) {
      throw std::runtime_error(
          "cannot register the nxm handler from this container: CONTAINER_ID is not "
          "set (only distrobox is supported)");
    }
    return {"distrobox", "enter", env.containerId, "--", env.executable};
  }

  return {env.executable};
}

QString desktopFileContent(const Environment& env)
{
  QStringList quoted;
  for (const auto& a : launcher(env)) {
    quoted.append(quoteExecArgument(a));
  }

  // string escapes of the file (applied before the quoting rules when read)
  QString exec = quoted.join(' ') + " %u";
  exec.replace("\\", "\\\\")
      .replace("\n", "\\n")
      .replace("\t", "\\t")
      .replace("\r", "\\r");

  return QStringLiteral("[Desktop Entry]\n"
                        "Type=Application\n"
                        "Name=Mod Organizer 2 (nxm links)\n"
                        "Comment=Download Nexus Mods links with Mod Organizer 2\n"
                        "Exec=%1\n"
                        "Terminal=false\n"
                        "NoDisplay=true\n"
                        "MimeType=%2;\n")
      .arg(exec, QString::fromLatin1(MimeType));
}

Action decide(const QString& currentDefault, bool force)
{
  if (force) {
    return Action::WriteFileAndSetDefault;
  }

  const auto current = currentDefault.trimmed();

  if (current.isEmpty()) {
    return Action::WriteFileAndSetDefault;
  }

  if (current == QLatin1String(DesktopFileName)) {
    return Action::WriteFile;
  }

  return Action::None;
}

QString applicationsDir()
{
  return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
         "/applications";
}

QString desktopFilePath()
{
  return applicationsDir() + "/" + QLatin1String(DesktopFileName);
}

vfsbridge::Command queryCommand()
{
  return vfsbridge::helperCommand("xdg-mime",
                                  {"query", "default", QString::fromLatin1(MimeType)});
}

vfsbridge::Command setDefaultCommand()
{
  return vfsbridge::helperCommand(
      "xdg-mime",
      {"default", QString::fromLatin1(DesktopFileName), QString::fromLatin1(MimeType)});
}

vfsbridge::Command updateDatabaseCommand(const QString& dir)
{
  return vfsbridge::helperCommand("update-desktop-database",
                                  {vfsbridge::hostCanonicalPath(dir)});
}
}  // namespace nxmhandler
