// Linux implementations of the Win32-only functions of envshortcut.cpp (the rest of
// envshortcut.cpp is shared).
//
// A shortcut is a desktop entry
// (https://specifications.freedesktop.org/desktop-entry-spec/),
// "<name>.desktop", on the desktop (QStandardPaths::DesktopLocation) or in the
// applications menu ($XDG_DATA_HOME/applications, ~/.local/share/applications by
// default), with the permissions 0755 (some desktops only launch executable entries).
//
// The Exec key is the target followed by the arguments; the arguments are a command
// line, split like a shell would (QProcess::splitCommand()). Following the
// specification:
// - each argument is quoted, the target only when it contains a reserved character,
// - inside quotes, ", `, $ and \ are escaped with a backslash,
// - % is written %% (a single % starts a field code),
// - then, as in every string value, \ is escaped again (so a literal backslash in an
//   argument is written \\\\), and newlines and tabs are written \n and \t.

#include "../envshortcut.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <uibase/log.h>

using namespace MOBase;

namespace env
{

namespace
{

  // escapes for a string value
  //
  QString escapeValue(const QString& s)
  {
    QString out;
    out.reserve(s.size());

    for (const QChar c : s) {
      switch (c.unicode()) {
      case u'\\':
        out += "\\\\";
        break;

      case u'\n':
        out += "\\n";
        break;

      case u'\t':
        out += "\\t";
        break;

      case u'\r':
        out += "\\r";
        break;

      default:
        out += c;
        break;
      }
    }

    return out;
  }

  // whether the argument must be quoted in Exec
  //
  bool needsQuoting(const QString& arg)
  {
    static const QString reserved = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");

    if (arg.isEmpty()) {
      return true;
    }

    for (const QChar c : arg) {
      if (reserved.contains(c)) {
        return true;
      }
    }

    return false;
  }

  // one argument of Exec, before the string escapes
  //
  QString execArgument(QString arg, bool alwaysQuote)
  {
    const bool quote = alwaysQuote || needsQuoting(arg);

    arg.replace("%", "%%");

    if (!quote) {
      return arg;
    }

    QString out = "\"";

    for (const QChar c : arg) {
      if (c == u'"' || c == u'`' || c == u'$' || c == u'\\') {
        out += u'\\';
      }

      out += c;
    }

    return out + "\"";
  }

  QString execLine(const QString& target, const QString& arguments)
  {
    QStringList parts;
    parts.push_back(execArgument(target, false));

    for (auto&& arg : QProcess::splitCommand(arguments)) {
      parts.push_back(execArgument(arg, true));
    }

    return escapeValue(parts.join(" "));
  }

  QString desktopEntry(const QString& name, const QString& target,
                       const QString& arguments, const QString& description,
                       const QString& icon, const QString& workingDirectory)
  {
    QStringList lines;

    lines.push_back("[Desktop Entry]");
    lines.push_back("Type=Application");
    lines.push_back("Name=" + escapeValue(name));

    if (!description.isEmpty()) {
      lines.push_back("Comment=" + escapeValue(description));
    }

    lines.push_back("Exec=" + execLine(target, arguments));

    if (!workingDirectory.isEmpty()) {
      lines.push_back("Path=" + escapeValue(workingDirectory));
    }

    if (!icon.isEmpty()) {
      lines.push_back("Icon=" + escapeValue(icon));
    }

    lines.push_back("Terminal=false");

    return lines.join("\n") + "\n";
  }

}  // namespace

bool Shortcut::add(Locations loc)
{
  log::debug("adding shortcut to {}:\n"
             "  . name: '{}'\n"
             "  . target: '{}'\n"
             "  . arguments: '{}'\n"
             "  . description: '{}'\n"
             "  . icon: '{}'\n"
             "  . working directory: '{}'",
             toString(loc), m_name, m_target, m_arguments, m_description, m_icon,
             m_workingDirectory);

  if (m_target.isEmpty()) {
    log::error("shortcut: target is empty");
    return false;
  }

  const auto path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  log::debug("shortcut file will be saved at '{}'", path);

  if (!QDir().mkpath(shortcutDirectory(loc))) {
    log::error("can't create directory '{}'\nshortcut file was not saved",
               shortcutDirectory(loc));
    return false;
  }

  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    log::error("can't open '{}', {}\nshortcut file was not saved", path,
               f.errorString());
    return false;
  }

  f.write(desktopEntry(m_name, m_target, m_arguments, m_description, m_icon,
                       m_workingDirectory)
              .toUtf8());

  if (!f.commit()) {
    log::error("can't write '{}', {}\nshortcut file was not saved", path,
               f.errorString());
    return false;
  }

  // 0755
  const auto perms = QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                     QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                     QFileDevice::ExeGroup | QFileDevice::ReadOther |
                     QFileDevice::ExeOther;

  if (!QFile::setPermissions(path, perms)) {
    log::warn("can't make shortcut '{}' executable", path);
  }

  return true;
}

bool Shortcut::remove(Locations loc)
{
  log::debug("removing shortcut for '{}' from {}", m_name, toString(loc));

  const auto path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  log::debug("path to shortcut file is '{}'", path);

  if (!QFile::exists(path)) {
    log::error("can't remove shortcut '{}', file not found", path);
    return false;
  }

  QFile f(path);
  if (!f.remove()) {
    log::error("failed to remove shortcut '{}', {}", path, f.errorString());
    return false;
  }

  return true;
}

QString Shortcut::shortcutDirectory(Locations loc) const
{
  switch (loc) {
  case Desktop:
    return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);

  case StartMenu:
    return QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);

  case None:
  default:
    log::error("shortcut: bad location {}", static_cast<int>(loc));
    return {};
  }
}

QString Shortcut::shortcutFilename() const
{
  if (m_name.isEmpty()) {
    log::error("shortcut name is empty");
    return {};
  }

  return m_name + ".desktop";
}

}  // namespace env
