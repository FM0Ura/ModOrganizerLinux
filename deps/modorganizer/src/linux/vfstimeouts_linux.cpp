#include "vfstimeouts_linux.h"

#include <QtGlobal>
#include <algorithm>

namespace
{
// the value of the variable `name` when it is a positive number of milliseconds below
// `max`; `max` otherwise
std::chrono::milliseconds shortenedBy(const char* name, std::chrono::milliseconds max)
{
  bool ok             = false;
  const auto variable = qEnvironmentVariable(name).toLongLong(&ok);
  return ok && variable > 0 ? std::min(std::chrono::milliseconds(variable), max) : max;
}
}  // namespace

std::chrono::milliseconds vfsMountTimeout()
{
  return shortenedBy("MO2_VFS_TIMEOUT_MS", std::chrono::seconds(30));
}

std::chrono::milliseconds vfsRecheckInterval()
{
  return shortenedBy("MO2_VFS_RECHECK_MS", std::chrono::seconds(5));
}
