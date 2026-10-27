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

  QMimeType mimeTypeOf(QStringView extension)
  {
    return QMimeDatabase().mimeTypeForFile(QStringLiteral("x.") + extension,
                                           QMimeDatabase::MatchExtension);
  }

  const Entry g_entries[] = {
      {"esp esm esl", "plugins", ":/MO/gui/plugins"},
      {"bsa ba2", "application-x-archive", ":/MO/gui/package"},
      {"nif kf hkx", "application-x-blender", ":/MO/gui/filetype_model"},
      {"pex psc", "text-x-script", ":/MO/gui/filetype_script"},
      {"dds", "image-x-generic", ":/MO/gui/filetype_image"},
      {"ini", "configure settings-configure", ":/MO/gui/filetype_text"},
      {"dll asi", "application-x-sharedlib", ":/MO/gui/filetype_library"},
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
  const QMimeType type = mimeTypeOf(extension);

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

QString mimeFallbackResource(QStringView extension)
{
  const QMimeType type = mimeTypeOf(extension);

  if (type.isDefault()) {
    return {};
  }

  const QString generic = type.genericIconName();

  if (generic == u"image-x-generic" || type.name().startsWith(u"image/")) {
    return QStringLiteral(":/MO/gui/filetype_image");
  }

  if (generic == u"text-x-generic" || type.name().startsWith(u"text/")) {
    return QStringLiteral(":/MO/gui/filetype_text");
  }

  static const char* const archives[] = {
      "application/x-archive",      "application/zip", "application/x-7z-compressed",
      "application/x-rar",          "application/vnd.rar", "application/gzip",
      "application/x-tar"};

  bool archive = generic == u"package-x-generic";
  for (const char* name : archives) {
    archive = archive || type.inherits(QString::fromLatin1(name));
  }

  if (archive) {
    return QStringLiteral(":/MO/gui/filetype_archive");
  }

  return {};
}

}  // namespace fileicons
