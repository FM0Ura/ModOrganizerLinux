#ifndef LOOTCLI_POSIX_SUPPORT_H
#define LOOTCLI_POSIX_SUPPORT_H

// Linux parts of lootcli whose Windows versions are in lootthread.cpp
// (GetLOOTAppData(), LOOTWorker::GetFile()): the LOOT data folder and the download of
// the masterlist (spec of Phase 4b, section 3.3). No Qt, so that the tests need only
// this.

#include <filesystem>
#include <optional>
#include <string>

namespace lootcli::posix
{

// the LOOT data folder (settings.toml, games/<folder>/masterlist.yaml and
// userlist.yaml): the one of the Flatpak LOOT,
// <home>/.var/app/io.github.loot.loot/data/LOOT, if it is a folder, so that the rules
// the user made in LOOT apply, like %LOCALAPPDATA%\LOOT on Windows; else
// <xdgDataHome>/LOOT when xdgDataHome is absolute, else <home>/.local/share/LOOT;
// empty without a home
std::filesystem::path lootDataPath(const std::string& home,
                                   const std::string& xdgDataHome);

// lootDataPath() of HOME and XDG_DATA_HOME
std::filesystem::path lootDataPath();

// downloads `url` (http, https or file) into `file`: written to <file>.download first,
// which replaces `file` only when the download worked (HTTP 200, or a file that could
// be read), so that a failure keeps the old masterlist; follows redirections, gives up
// after 60 s; the error, or nullopt
std::optional<std::string> downloadFile(const std::string& url,
                                        const std::filesystem::path& file);

}  // namespace lootcli::posix

#endif  // LOOTCLI_POSIX_SUPPORT_H
