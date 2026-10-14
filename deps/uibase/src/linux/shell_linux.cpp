// Linux implementations of the shell operations of utility.h: shellCopy(),
// shellMove(), shellRename(), shellDelete() and the shell namespace.
//
// The Windows versions use SHFileOperation/ShellExecuteEx; here:
// - failures return false (or a failed Result) with an errno value (::GetLastError()
//   in the Windows documentation is errno),
// - there are no confirmation dialogs: existing files are overwritten, as if the user
//   had answered "Yes to all" (dialog parameters are ignored),
// - wildcards (* and ?) are supported in the last component of source paths,
// - a destination that is an existing directory receives the sources (like cp/mv), a
//   single destination for several sources or a wildcard is created as a directory,
// - moves are rename(2), with a recursive copy and delete across file systems,
// - shellDelete(recycle=true) moves to the freedesktop.org trash (trash_linux.h).

#include "log.h"
#include "trash_linux.h"
#include "utility.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>

#include <cerrno>
#include <filesystem>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

namespace MOBase
{

namespace
{

  fs::path toPath(const QString& path)
  {
    return fs::path(
        QFile::encodeName(QFileInfo(path).absoluteFilePath()).toStdString());
  }

  // sets errno from `ec` and returns false
  bool fail(const std::error_code& ec)
  {
    errno = ec.value() != 0 ? ec.value() : EIO;
    return false;
  }

  bool fail(int e)
  {
    errno = e;
    return false;
  }

  bool pathExists(const fs::path& p)
  {
    std::error_code ec;
    return fs::exists(fs::symlink_status(p, ec));
  }

  bool isDirectory(const fs::path& p)
  {
    std::error_code ec;
    return fs::is_directory(fs::status(p, ec));
  }

  bool isRealDirectory(const fs::path& p)
  {
    std::error_code ec;
    return fs::is_directory(fs::symlink_status(p, ec));
  }

  bool hasWildcard(const QString& path)
  {
    const QString name = QFileInfo(path).fileName();
    return name.contains('*') || name.contains('?');
  }

  // sources with wildcards in their last component expanded (case-insensitively,
  // like on Windows); a wildcard that matches nothing expands to nothing
  QStringList expandSources(const QStringList& sources, bool* hadWildcard)
  {
    QStringList expanded;
    *hadWildcard = false;

    for (const QString& source : sources) {
      if (!hasWildcard(source)) {
        expanded.append(QFileInfo(source).absoluteFilePath());
        continue;
      }

      *hadWildcard = true;
      const QFileInfo info(source);
      const QDir dir = info.absoluteDir();
      const auto matches =
          dir.entryInfoList({info.fileName()}, QDir::AllEntries | QDir::NoDotAndDotDot |
                                                   QDir::Hidden | QDir::System);

      for (const QFileInfo& match : matches) {
        expanded.append(match.absoluteFilePath());
      }
    }

    return expanded;
  }

  // copies a file, symbolic link or directory tree, merging into existing directories
  // and overwriting existing files
  bool copyEntry(const fs::path& from, const fs::path& to)
  {
    std::error_code ec;
    fs::copy(from, to,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing |
                 fs::copy_options::copy_symlinks,
             ec);

    return ec ? fail(ec) : true;
  }

  // moves a file, symbolic link or directory tree; an existing directory target is
  // merged into, existing files are replaced
  bool moveEntry(const fs::path& from, const fs::path& to)
  {
    if (::rename(from.c_str(), to.c_str()) == 0) {
      return true;
    }

    const int e = errno;

    if (e == EXDEV) {
      // different file systems: copy, then delete the source
      if (!copyEntry(from, to)) {
        return false;
      }

      std::error_code ec;
      fs::remove_all(from, ec);
      return ec ? fail(ec) : true;
    }

    if ((e == ENOTEMPTY || e == EEXIST) && isRealDirectory(from) &&
        isRealDirectory(to)) {
      // merge the source directory into the existing one
      std::error_code ec;
      for (const auto& child : fs::directory_iterator(from, ec)) {
        if (!moveEntry(child.path(), to / child.path().filename())) {
          return false;
        }
      }

      if (ec) {
        return fail(ec);
      }

      return ::rmdir(from.c_str()) == 0;
    }

    return fail(e);
  }

  enum class Operation
  {
    Copy,
    Move
  };

  bool copyOrMove(const QStringList& sourceNames, const QStringList& destinationNames,
                  Operation op)
  {
    bool hadWildcard    = false;
    const auto sources  = expandSources(sourceNames, &hadWildcard);
    const bool pairwise = !hadWildcard && destinationNames.size() == sourceNames.size();

    if (!pairwise && destinationNames.size() != 1) {
      return fail(EINVAL);
    }

    if (!pairwise) {
      // a single destination for several sources is a directory
      std::error_code ec;
      fs::create_directories(toPath(destinationNames[0]), ec);
      if (ec) {
        return fail(ec);
      }
    }

    for (qsizetype i = 0; i < sources.size(); ++i) {
      const fs::path from = toPath(sources[i]);
      fs::path to = toPath(pairwise ? destinationNames[i] : destinationNames[0]);

      if (!pathExists(from)) {
        return fail(ENOENT);
      }

      if (isDirectory(to)) {
        to /= from.filename();
      }

      std::error_code ec;
      if (pathExists(to) && fs::equivalent(from, to, ec)) {
        return fail(EINVAL);
      }

      const bool ok = op == Operation::Copy ? copyEntry(from, to) : moveEntry(from, to);
      if (!ok) {
        return false;
      }
    }

    return true;
  }

  // renames `from` to `to` (never into it), fails with EEXIST if `to` exists
  bool renameEntry(const fs::path& from, const fs::path& to, bool copyAllowed)
  {
    std::error_code ec;
    if (pathExists(to) && !fs::equivalent(from, to, ec)) {
      return fail(EEXIST);
    }

    if (::rename(from.c_str(), to.c_str()) == 0) {
      return true;
    }

    if (errno != EXDEV || !copyAllowed) {
      return false;
    }

    return moveEntry(from, to);
  }

  shell::Result failure(int e)
  {
    return shell::Result::makeFailure(static_cast<DWORD>(e));
  }

  // opens a URL with the desktop's handler (xdg-open without a GUI application)
  shell::Result openUrl(const QUrl& url)
  {
    bool ok = false;

    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
      ok = QDesktopServices::openUrl(url);
    } else {
      ok = QProcess::startDetached("xdg-open", {url.toString(QUrl::FullyEncoded)});
    }

    if (!ok) {
      log::error("failed to open '{}'", url.toString());
      return shell::Result::makeFailure(
          ENOENT, QObject::tr("No application could open '%1'.").arg(url.toString()));
    }

    return shell::Result::makeSuccess();
  }

}  // namespace

bool shellCopy(const QStringList& sourceNames, const QStringList& destinationNames,
               QWidget*)
{
  return copyOrMove(sourceNames, destinationNames, Operation::Copy);
}

bool shellCopy(const QString& sourceNames, const QString& destinationNames, bool,
               QWidget*)
{
  return copyOrMove({sourceNames}, {destinationNames}, Operation::Copy);
}

bool shellMove(const QStringList& sourceNames, const QStringList& destinationNames,
               QWidget*)
{
  return copyOrMove(sourceNames, destinationNames, Operation::Move);
}

bool shellMove(const QString& sourceNames, const QString& destinationNames, bool,
               QWidget*)
{
  return copyOrMove({sourceNames}, {destinationNames}, Operation::Move);
}

bool shellRename(const QString& oldName, const QString& newName, bool, QWidget*)
{
  if (!pathExists(toPath(oldName))) {
    return fail(ENOENT);
  }

  // like SHFileOperation(FO_RENAME), works across file systems
  return renameEntry(toPath(oldName), toPath(newName), true);
}

bool shellDelete(const QStringList& fileNames, bool recycle, QWidget*)
{
  bool hadWildcard   = false;
  const auto sources = expandSources(fileNames, &hadWildcard);

  for (const QString& source : sources) {
    if (recycle) {
      if (!details::moveToTrash(source)) {
        log::error("cannot move '{}' to the trash: {}", source,
                   formatSystemMessage(static_cast<DWORD>(errno)));
        return false;
      }
      continue;
    }

    const fs::path path = toPath(source);
    if (!pathExists(path)) {
      return fail(ENOENT);
    }

    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) {
      return fail(ec);
    }
  }

  return true;
}

namespace shell
{

  QString formatError(int i)
  {
    // there are no ShellExecute error codes on Linux, errors are errno values
    return QString::fromStdWString(formatSystemMessage(static_cast<DWORD>(i)));
  }

  Result Explore(const QFileInfo& info)
  {
    // file managers have no standard way of selecting a file, open its directory
    if (info.isDir()) {
      return openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
    }

    const QDir parent = info.absoluteDir();
    if (info.isFile() || parent.exists()) {
      return openUrl(QUrl::fromLocalFile(parent.absolutePath()));
    }

    return failure(ENOENT);
  }

  Result Open(const QString& path)
  {
    return openUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
  }

  Result Open(const QUrl& url)
  {
    log::debug("opening url '{}'", url.toString());
    return openUrl(url);
  }

  Result Execute(const QString& program, const QString& params)
  {
    const QFileInfo info(program);

    // ShellExecute opens documents with their handler
    if (info.isFile() && !info.isExecutable() && params.isEmpty()) {
      return Open(program);
    }

    if (QProcess::startDetached(program, QProcess::splitCommand(params))) {
      // the process is detached, there is no handle to return
      return Result::makeSuccess();
    }

    const bool found =
        info.exists() || !QStandardPaths::findExecutable(program).isEmpty();
    const int e = found ? EACCES : ENOENT;

    log::error("failed to invoke '{} {}': {}", program, params,
               formatSystemMessage(static_cast<DWORD>(e)));
    return failure(e);
  }

  void SetUrlHandler(const QString& cmd)
  {
    if (!cmd.isEmpty()) {
      log::warn("custom browser commands are not supported on Linux yet, links are "
                "opened with the system handler (ignoring '{}')",
                cmd);
    }
  }

  Result Delete(const QFileInfo& path)
  {
    if (::unlink(QFile::encodeName(path.absoluteFilePath()).constData()) != 0) {
      return failure(errno);
    }

    return Result::makeSuccess();
  }

  Result Rename(const QFileInfo& src, const QFileInfo& dest)
  {
    return Rename(src, dest, true);
  }

  Result Rename(const QFileInfo& src, const QFileInfo& dest, bool copyAllowed)
  {
    const fs::path from = toPath(src.absoluteFilePath());
    if (!pathExists(from)) {
      return failure(ENOENT);
    }

    if (!renameEntry(from, toPath(dest.absoluteFilePath()), copyAllowed)) {
      return failure(errno);
    }

    return Result::makeSuccess();
  }

  Result CreateDirectories(const QDir& dir)
  {
    std::error_code ec;
    fs::create_directories(toPath(dir.path()), ec);
    if (ec) {
      return failure(ec.value());
    }

    return Result::makeSuccess();
  }

  Result DeleteDirectoryRecursive(const QDir& dir)
  {
    if (!shellDelete({dir.path()}, false)) {
      return failure(errno);
    }

    return Result::makeSuccess();
  }

}  // namespace shell

}  // namespace MOBase
