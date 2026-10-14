// Linux implementations of the Win32-backed functions of utility.h that are not shell
// operations (see shell_linux.cpp): known folders, executable icons and versions,
// system error messages and SYSTEMTIME formatting.

#include "log.h"
#include "peversion_linux.h"
#include "utility.h"

#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QStandardPaths>
#include <QTime>

#include <cstring>
#include <format>
#include <optional>
#include <stdexcept>

namespace MOBase
{

// defined in utility.cpp
std::wstring formatMessage(DWORD id, const std::wstring& message);

namespace
{

  QString guidToString(const GUID& id)
  {
    return QString::fromStdString(std::format(
        "{{{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}}}",
        id.Data1, id.Data2, id.Data3, id.Data4[0], id.Data4[1], id.Data4[2],
        id.Data4[3], id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]));
  }

  QString writable(QStandardPaths::StandardLocation location)
  {
    return QStandardPaths::writableLocation(location);
  }

  // the Linux equivalent of a known folder; nullopt for folders that do not exist on
  // Linux (Program Files, Windows, OneDrive, unknown ids)
  //
  // Documents, AppData, etc. of the games are inside their Proton prefix; that
  // redirection is done by the game plugins, not here
  std::optional<QString> knownFolderPath(const KNOWNFOLDERID& id)
  {
    if (id == FOLDERID_Desktop) {
      return writable(QStandardPaths::DesktopLocation);
    } else if (id == FOLDERID_Documents) {
      return writable(QStandardPaths::DocumentsLocation);
    } else if (id == FOLDERID_Downloads) {
      return writable(QStandardPaths::DownloadLocation);
    } else if (id == FOLDERID_LocalAppData) {
      // $XDG_DATA_HOME, ~/.local/share by default
      return writable(QStandardPaths::GenericDataLocation);
    } else if (id == FOLDERID_RoamingAppData) {
      // $XDG_CONFIG_HOME, ~/.config by default
      return writable(QStandardPaths::GenericConfigLocation);
    } else if (id == FOLDERID_StartMenu) {
      // where desktop entries live; deliberately not $XDG_DATA_HOME based, like the
      // Windows start menu it is a fixed per-user location
      return QDir::homePath() + "/.local/share/applications";
    }

    return std::nullopt;
  }

}  // namespace

QString getOptionalKnownFolder(KNOWNFOLDERID id)
{
  const auto path = knownFolderPath(id);
  return path ? *path : QString();
}

QDir getKnownFolder(KNOWNFOLDERID id, const QString& what)
{
  const auto path = knownFolderPath(id);

  if (!path || path->isEmpty()) {
    log::error("failed to get known folder '{}', no equivalent on this system",
               what.isEmpty() ? guidToString(id) : what);

    throw std::runtime_error("couldn't get known folder path");
  }

  return QDir(*path);
}

QString ToString(const SYSTEMTIME& time)
{
  // GetDateFormat/GetTimeFormat with the user locale, short formats
  const QDate date(time.wYear, time.wMonth, time.wDay);
  const QTime t(time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
  const QLocale locale;

  return locale.toString(date, QLocale::ShortFormat) + " " +
         locale.toString(t, QLocale::LongFormat);
}

QIcon iconForExecutable(const QString&)
{
  // icons are not extracted from PE resources on Linux
  return QIcon::fromTheme("application-x-executable");
}

QString getFileVersion(QString const& filepath)
{
  const auto info = details::readPeVersionInfo(filepath);
  if (!info || !info->fixed) {
    return "";
  }

  const auto& f = *info->fixed;
  return QString("%1.%2.%3.%4")
      .arg(f.fileVersionMS >> 16)
      .arg(f.fileVersionMS & 0xffff)
      .arg(f.fileVersionLS >> 16)
      .arg(f.fileVersionLS & 0xffff);
}

QString getProductVersion(QString const& filepath)
{
  const auto info = details::readPeVersionInfo(filepath);
  if (!info) {
    return "";
  }

  return info->productVersion;
}

std::wstring formatSystemMessage(DWORD id)
{
  // errors are errno values on Linux
  return formatMessage(
      id, QString::fromLocal8Bit(std::strerror(static_cast<int>(id))).toStdWString());
}

std::wstring formatNtMessage(NTSTATUS s)
{
  // no message tables for NTSTATUS values
  return formatMessage(static_cast<DWORD>(s), {});
}

QString windowsErrorString(DWORD errorCode)
{
  return QString::fromStdWString(formatSystemMessage(errorCode));
}

}  // namespace MOBase
