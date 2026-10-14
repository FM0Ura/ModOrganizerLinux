// Minimal reader of the VS_VERSION_INFO resource of PE executables (what
// GetFileVersionInfo/VerQueryValue provide on Windows). The file is untrusted input:
// every offset and size is checked against the file and the enclosing structure.

#ifndef UIBASE_LINUX_PEVERSION_H
#define UIBASE_LINUX_PEVERSION_H

#include <QString>
#include <cstdint>
#include <optional>

namespace MOBase::details
{

struct PeFixedFileInfo
{
  std::uint32_t fileVersionMS    = 0;
  std::uint32_t fileVersionLS    = 0;
  std::uint32_t productVersionMS = 0;
  std::uint32_t productVersionLS = 0;
};

struct PeVersionInfo
{
  // VS_FIXEDFILEINFO, if present and valid
  std::optional<PeFixedFileInfo> fixed;

  // \StringFileInfo\<lang><codepage>\ProductVersion for the first translation of
  // \VarFileInfo\Translation, empty if any of them is missing (like VerQueryValue)
  QString productVersion;
};

// nullopt if the file cannot be read, is not a PE file or has no version resource
std::optional<PeVersionInfo> readPeVersionInfo(const QString& path);

}  // namespace MOBase::details

#endif  // UIBASE_LINUX_PEVERSION_H
