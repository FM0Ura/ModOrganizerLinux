#include "virtualizedpaths.h"

#include <utility>

namespace
{

// "<mods>/<mod>/rest" => {"<data>/rest", "<mod>"}, "<mods>/<mod>" => {"<data>",
// "<mod>"}; trailedMods ends with '/'
std::optional<std::pair<QString, QString>> inDataDirectory(const QString& trailedMods,
                                                           const QString& dataPath,
                                                           const QString& path)
{
  if (!path.startsWith(trailedMods, Qt::CaseInsensitive)) {
    return std::nullopt;
  }

  const qsizetype start = trailedMods.length();
  const qsizetype slash = path.indexOf('/', start);
  if (slash < 0) {
    return std::make_pair(dataPath, path.mid(start));
  }

  return std::make_pair(dataPath + path.mid(slash), path.mid(start, slash - start));
}

}  // namespace

std::optional<VirtualizedPaths> virtualizedPaths(const QString& modsPath,
                                                 const QString& dataPath,
                                                 const QString& binary,
                                                 const QString& workingDirectory)
{
  QString trailedMods = modsPath;
  if (!trailedMods.endsWith('/')) {
    trailedMods += '/';
  }

  const auto bin = inDataDirectory(trailedMods, dataPath, binary);
  const auto cwd = inDataDirectory(trailedMods, dataPath, workingDirectory);
  if (!bin && !cwd) {
    return std::nullopt;
  }

  return VirtualizedPaths{bin ? bin->first : binary,
                          cwd ? cwd->first : workingDirectory,
                          bin ? bin->second : QString()};
}
