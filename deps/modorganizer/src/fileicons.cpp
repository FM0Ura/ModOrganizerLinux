#include "fileicons.h"

#include <QMimeDatabase>
#include <QMimeType>

namespace fileicons
{

namespace
{

  struct Entry
  {
    const char* extensions;  // space separated
    const char* themeNames;  // space separated, in order of preference
    const char* fallbackResource;
  };

  const Entry g_entries[] = {
      {"esp esm esl", "plugins", ":/MO/gui/plugins"},
      {"bsa ba2", "application-x-archive", ":/MO/gui/package"},
      {"nif kf hkx", "application-x-blender", ""},
      {"pex psc", "text-x-script", ""},
      {"dds", "image-x-generic", ""},
      {"ini", "configure settings-configure", ""},
      {"dll asi", "application-x-sharedlib", ""},
      {"pak utoc ucas", "package-x-generic", ":/MO/gui/package"}};

}  // namespace

std::optional<GameFileIcon> gameFileIcon(QStringView extension)
{
  const QString lower = extension.toString().toLower();

  for (const Entry& e : g_entries) {
    const QStringList extensions =
        QString::fromLatin1(e.extensions).split(' ', Qt::SkipEmptyParts);

    if (extensions.contains(lower)) {
      return GameFileIcon{
          QString::fromLatin1(e.themeNames).split(' ', Qt::SkipEmptyParts),
          QString::fromLatin1(e.fallbackResource)};
    }
  }

  return std::nullopt;
}

QStringList mimeIconNames(QStringView extension)
{
  const QMimeType type = QMimeDatabase().mimeTypeForFile(
      QStringLiteral("x.") + extension, QMimeDatabase::MatchExtension);

  if (type.isDefault()) {
    return {};
  }

  QStringList names;
  for (const QString& name : {type.iconName(), type.genericIconName()}) {
    if (!name.isEmpty() && !names.contains(name)) {
      names.push_back(name);
    }
  }

  return names;
}

}  // namespace fileicons
