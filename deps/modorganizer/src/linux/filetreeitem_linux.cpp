// Linux implementation of the file type names of filetreeitem.cpp (the Windows one
// asks the shell with SHGetFileInfoW()).
//
// The names are the descriptions of the MIME types from the shared MIME database,
// guessed from the extension only, like Windows does for files that are not on the
// filesystem; unknown extensions get "<EXT> File", like on Windows.

#include <QMimeDatabase>
#include <QMimeType>
#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace
{

QString typeOfExtension(const QString& dotExtension)
{
  static const QMimeDatabase db;

  const auto mime =
      db.mimeTypeForFile("file" + dotExtension, QMimeDatabase::MatchExtension);

  if (mime.isValid() && !mime.isDefault() && !mime.comment().isEmpty()) {
    return mime.comment();
  }

  return QObject::tr("%1 File").arg(dotExtension.mid(1).toUpper());
}

const QString& cachedFileTypeNoExtension()
{
  static const QString name = QObject::tr("File");
  return name;
}

}  // namespace

const QString& directoryFileType()
{
  static const QString name = [] {
    const auto mime = QMimeDatabase().mimeTypeForName("inode/directory");

    if (mime.isValid() && !mime.comment().isEmpty()) {
      return mime.comment();
    }

    return QObject::tr("File folder");
  }();

  return name;
}

const QString& cachedFileType(const std::wstring& file, bool)
{
  static std::map<std::wstring, QString, std::less<>> map;
  static std::mutex mutex;

  const auto dot = file.find_last_of(L'.');
  if (dot == std::wstring::npos) {
    return cachedFileTypeNoExtension();
  }

  std::scoped_lock lock(mutex);
  const auto sv = std::wstring_view(file.c_str() + dot, file.size() - dot);

  auto itor = map.find(sv);
  if (itor != map.end()) {
    return itor->second;
  }

  const QString s = typeOfExtension(QString::fromStdWString(std::wstring(sv)));
  return map.emplace(sv, s).first->second;
}
