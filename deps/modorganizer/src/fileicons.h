#ifndef MODORGANIZER_FILEICONS_H
#define MODORGANIZER_FILEICONS_H

#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

// icons of file types for the Data tab on Linux: the file types of games (plugins,
// archives, meshes...) get an icon of their own, the others the icon of their MIME type
namespace fileicons
{

struct GameFileIcon
{
  // names in the icon theme, in order of preference
  QStringList themeNames;

  // resource used when the theme has none of the names, empty if there is none
  QString fallbackResource;
};

// the icon of a file type of games, `extension` has no dot and its case is ignored
std::optional<GameFileIcon> gameFileIcon(QStringView extension);

// names in the icon theme of the MIME type of `extension` (no dot), the icon of the
// type and then its generic one; empty if the type is unknown
QStringList mimeIconNames(QStringView extension);

}  // namespace fileicons

#endif  // MODORGANIZER_FILEICONS_H
