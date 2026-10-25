// Mod Organizer inside an AppImage (appimage_linux.h, spec of Phase 5, section 3.3).

#include "appimage_linux.h"

#include <cstdlib>

extern char** environ;

namespace appimage
{
namespace
{
  Info g_info;
}  // namespace

const Info& info()
{
  return g_info;
}

void setInfoForTesting(const Info& i)
{
  g_info = i;
}

std::map<std::string, std::optional<std::string>>
hostEnvironment(const std::map<std::string, std::string>& current)
{
  std::map<std::string, std::optional<std::string>> result;

  const std::string prefix = OrigPrefix;
  bool inside              = current.count("APPIMAGE") > 0;

  for (const auto& [name, value] : current) {
    if (name.rfind(prefix, 0) != 0 || name.size() == prefix.size()) {
      continue;
    }

    inside = true;

    const std::string var = name.substr(prefix.size());
    if (value == UnsetMarker) {
      result[var] = std::nullopt;
    } else {
      result[var] = value;
    }
    result[name] = std::nullopt;
  }

  if (!inside) {
    return {};
  }

  for (const char* var : {"APPIMAGE", "APPDIR", "ARGV0", "OWD"}) {
    if (current.count(var)) {
      result[var] = std::nullopt;
    }
  }

  return result;
}

std::map<std::string, std::string> currentEnvironment()
{
  std::map<std::string, std::string> env;

  for (char** p = environ; p && *p; ++p) {
    const std::string entry = *p;
    const auto eq           = entry.find('=');
    if (eq == std::string::npos || eq == 0) {
      continue;
    }
    env.emplace(entry.substr(0, eq), entry.substr(eq + 1));
  }

  return env;
}

void captureAndRestore()
{
  const auto env = currentEnvironment();

  // an extracted AppImage (squashfs-root/AppRun) has AppRun's markers but no APPIMAGE:
  // its environment is restored all the same, but it is not an AppImage (info())
  if (const auto appImage = env.find("APPIMAGE"); appImage != env.end()) {
    Info i;
    i.appImage = QString::fromLocal8Bit(appImage->second.c_str());
    if (const auto dir = env.find("APPDIR"); dir != env.end()) {
      i.appDir = QString::fromLocal8Bit(dir->second.c_str());
    }
    g_info = i;
  }

  for (const auto& [name, value] : hostEnvironment(env)) {
    if (value) {
      ::setenv(name.c_str(), value->c_str(), 1);
    } else {
      ::unsetenv(name.c_str());
    }
  }
}
}  // namespace appimage
