#ifndef APPIMAGE_LINUX_H
#define APPIMAGE_LINUX_H

#include <QString>

#include <map>
#include <optional>
#include <string>

// Mod Organizer inside an AppImage (spec of Phase 5, section 3.3). The AppRun script
// changes the environment so that the bundled libraries are found (LD_LIBRARY_PATH,
// PATH, ...) and exports the previous value of each variable it changes as
// MO2_APPIMAGE_ORIG_<VAR> (or MO2_APPIMAGE_UNSET when the variable did not exist).
// captureAndRestore() undoes that for the whole process at the start of main(), so
// that the programs started by Qt (xdg-open), by Python plugins and by the launcher
// see the environment of the host.
namespace appimage
{
inline constexpr char OrigPrefix[]  = "MO2_APPIMAGE_ORIG_";
inline constexpr char UnsetMarker[] = "MO2_APPIMAGE_UNSET";

struct Info
{
  // $APPIMAGE, the path of the .AppImage file
  QString appImage;
  // $APPDIR, where the AppImage is mounted or extracted
  QString appDir;

  bool active() const { return !appImage.isEmpty(); }
};

// what captureAndRestore() found; empty outside an AppImage
const Info& info();

// the variables to change to get the environment of the host: the original value of
// each variable that has a MO2_APPIMAGE_ORIG_ marker, std::nullopt for the ones that
// did not exist; std::nullopt for APPIMAGE, APPDIR, ARGV0, OWD and the markers
// themselves. Variables without a marker are not in the result. Empty outside an
// AppImage (no APPIMAGE and no markers).
std::map<std::string, std::optional<std::string>>
hostEnvironment(const std::map<std::string, std::string>& current);

// reads the environment, fills info() and applies hostEnvironment() to the process;
// does nothing without $APPIMAGE
void captureAndRestore();

// environ as a map
std::map<std::string, std::string> currentEnvironment();

// for tests only: replaces what info() returns
void setInfoForTesting(const Info& info);
}  // namespace appimage

#endif  // APPIMAGE_LINUX_H
