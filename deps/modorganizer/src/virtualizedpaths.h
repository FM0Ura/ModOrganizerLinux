#ifndef MODORGANIZER_VIRTUALIZEDPATHS_H
#define MODORGANIZER_VIRTUALIZEDPATHS_H

#include <QString>

#include <optional>

// a program whose binary or working directory is inside the mods directory runs from
// the matching place of the data directory, where the virtual file system shows the
// files of its mod (mods/FNIS/tools/x.exe => <game>/Data/tools/x.exe); on Windows
// through the "launch" command of another instance, hooked by usvfs, on Linux directly
// (every process sees the mount)
struct VirtualizedPaths
{
  // in the data directory if they were in the mods directory, unchanged otherwise
  QString binary;
  QString workingDirectory;

  // the mod of the binary, empty if the binary is not in the mods directory
  QString mod;
};

// absolute paths with '/' separators; the mods directory is compared ignoring case, as
// Windows always did; std::nullopt if neither the binary nor the working directory is
// inside the mods directory
std::optional<VirtualizedPaths> virtualizedPaths(const QString& modsPath,
                                                 const QString& dataPath,
                                                 const QString& binary,
                                                 const QString& workingDirectory);

#endif  // MODORGANIZER_VIRTUALIZEDPATHS_H
