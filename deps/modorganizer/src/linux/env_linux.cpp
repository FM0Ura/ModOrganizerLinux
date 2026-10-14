// Linux implementations of the Win32-only functions of env.cpp (the rest of env.cpp is
// shared; the crash dumps of envdump.h are in envdump_linux.cpp).
//
// - the standard streams are already those of the terminal, Console does nothing,
// - there is no notification when a library is loaded: onModuleLoaded() returns null,
// - environment variables are the ones of the process (qgetenv()/qputenv()),
// - there are no services, no registry and no file associations of the shell (files
//   are opened with MOBase::shell::Open()): getService() returns an invalid service,
//   getAssociation() an empty association, the registry functions do nothing.

#include "../env.h"

#include <filesystem>

#include <QDateTime>
#include <QTimeZone>

#include <uibase/log.h>

using namespace MOBase;

namespace env
{

Console::Console() : m_hasConsole(false), m_in(nullptr), m_out(nullptr), m_err(nullptr)
{}

Console::~Console() = default;

ModuleNotification::~ModuleNotification() = default;

QString Environment::timezone() const
{
  const auto now = QDateTime::currentDateTime();
  const auto tz  = QTimeZone::systemTimeZone();

  // minutes
  const int o = tz.offsetFromUtc(now) / 60;

  const auto offset =
      QString("%1%2:%3")
          .arg(o < 0 ? "-" : "+")
          .arg(QString::number(std::abs(o) / 60), 2, QChar::fromLatin1('0'))
          .arg(QString::number(std::abs(o) % 60), 2, QChar::fromLatin1('0'));

  auto s = QString("%1 (%2), %3")
               .arg(QString::fromUtf8(tz.id()))
               .arg(tz.abbreviation(now))
               .arg(offset);

  if (tz.isDaylightTime(now)) {
    s += " (dst is active)";
  }

  return s;
}

std::unique_ptr<ModuleNotification>
Environment::onModuleLoaded(QObject*, std::function<void(Module)>)
{
  log::debug("notifications of loaded modules are not available on this system");
  return {};
}

QString get(const QString& name)
{
  return qEnvironmentVariable(name.toLocal8Bit().constData());
}

void set(const QString& n, const QString& v)
{
  qputenv(n.toLocal8Bit().constData(), v.toLocal8Bit());
}

Service getService(const QString& name)
{
  return Service(name);
}

Association getAssociation(const QFileInfo& targetInfo)
{
  log::debug("file associations are not available on this system, for '{}'",
             targetInfo.absoluteFilePath());

  return {};
}

bool registryValueExists(const QString&, const QString&)
{
  return false;
}

void deleteRegistryKeyIfEmpty(const QString&)
{
  // no registry
}

std::filesystem::path thisProcessPath()
{
  std::error_code ec;
  auto p = std::filesystem::read_symlink("/proc/self/exe", ec);

  if (ec) {
    log::error("can't get the path of this process, {}", ec.message());
    return {};
  }

  return p;
}

}  // namespace env
