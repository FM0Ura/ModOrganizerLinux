// Linux implementations of the Win32-only functions of envwindows.cpp (the rest of
// envwindows.cpp is shared); WindowsInfo keeps its name and describes the system:
//
// - the reported and real versions are the version of the kernel (there is no
//   compatibility mode),
// - the BuildLab entry of the release is the name of the distribution
//   (QSysInfo::prettyProductName()),
// - the process is elevated when it runs as root.

#include "../envwindows.h"

#include <unistd.h>

#include <QRegularExpression>
#include <QSysInfo>

namespace env
{

namespace
{

  // "6.18.44-fc-v114" -> 6.18.44
  //
  WindowsInfo::Version kernelVersion()
  {
    WindowsInfo::Version v;

    static const QRegularExpression re(R"(^(\d+)(?:\.(\d+))?(?:\.(\d+))?)");
    const auto m = re.match(QSysInfo::kernelVersion());

    if (m.hasMatch()) {
      v.major = m.captured(1).toUInt();
      v.minor = m.captured(2).toUInt();
      v.build = m.captured(3).toUInt();
    }

    return v;
  }

}  // namespace

WindowsInfo::WindowsInfo()
{
  m_reported = kernelVersion();
  m_real     = m_reported;
  m_release  = getRelease();
  m_elevated = getElevated();
}

WindowsInfo::Release WindowsInfo::getRelease() const
{
  Release r;
  r.buildLab = QSysInfo::prettyProductName();
  return r;
}

std::optional<bool> WindowsInfo::getElevated() const
{
  return (::geteuid() == 0);
}

}  // namespace env
