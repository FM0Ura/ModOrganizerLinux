// Home trash, see trash_linux.h.

#include "trash_linux.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <fcntl.h>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>

namespace MOBase::details
{

namespace
{

  // gives up instead of looping forever when thousands of files have the same name
  constexpr int MaxCollisions = 10000;

  // $XDG_DATA_HOME, or ~/.local/share when unset or not absolute (XDG base directory
  // specification)
  QString dataHome()
  {
    const QString env = qEnvironmentVariable("XDG_DATA_HOME");
    if (!env.isEmpty() && QDir::isAbsolutePath(env)) {
      return QDir::cleanPath(env);
    }

    return QDir::homePath() + "/.local/share";
  }

  // device of `path`, or of its closest existing parent
  std::optional<dev_t> deviceOf(QString path)
  {
    for (;;) {
      struct stat st;
      if (::stat(QFile::encodeName(path).constData(), &st) == 0) {
        return st.st_dev;
      }

      const QString parent = QFileInfo(path).absolutePath();
      if (parent == path) {
        return std::nullopt;
      }

      path = parent;
    }
  }

  // creates `path` and its missing parents with mode 0700
  bool makeDirectories(const QString& path)
  {
    const QString parent = QFileInfo(path).absolutePath();
    if (parent != path && !QFileInfo::exists(parent) && !makeDirectories(parent)) {
      return false;
    }

    if (::mkdir(QFile::encodeName(path).constData(), 0700) != 0 && errno != EEXIST) {
      return false;
    }

    return true;
  }

  bool writeAll(int fd, const QByteArray& data)
  {
    const char* p   = data.constData();
    std::size_t len = static_cast<std::size_t>(data.size());

    while (len > 0) {
      const ssize_t n = ::write(fd, p, len);
      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }
        return false;
      }

      p += n;
      len -= static_cast<std::size_t>(n);
    }

    return true;
  }

  // removes `path` without changing errno
  void unlinkQuietly(const QByteArray& path)
  {
    const int e = errno;
    ::unlink(path.constData());
    errno = e;
  }

}  // namespace

bool moveToTrash(const QString& path)
{
  // cleaned: "dir/" is "dir", whose name is not empty
  const QString absolute  = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  const QByteArray native = QFile::encodeName(absolute);
  const QByteArray name   = QFile::encodeName(QFileInfo(absolute).fileName());

  if (name.isEmpty() || name == "." || name == "..") {
    errno = EINVAL;
    return false;
  }

  struct stat st;
  if (::lstat(native.constData(), &st) != 0) {
    return false;
  }

  const QString trash = dataHome() + "/Trash";

  // rename() only works on the same file system; never fall back to a permanent
  // deletion
  const auto trashDevice = deviceOf(trash);
  if (!trashDevice || *trashDevice != st.st_dev) {
    errno = EXDEV;
    return false;
  }

  if (!makeDirectories(trash + "/files") || !makeDirectories(trash + "/info")) {
    return false;
  }

  const QByteArray files = QFile::encodeName(trash + "/files/");
  const QByteArray info  = QFile::encodeName(trash + "/info/");

  const QByteArray content =
      "[Trash Info]\nPath=" + native.toPercentEncoding("/") + "\nDeletionDate=" +
      QDateTime::currentDateTime().toString("yyyy-MM-ddTHH:mm:ss").toUtf8() + "\n";

  for (int i = 1; i <= MaxCollisions; ++i) {
    const QByteArray candidate = i == 1 ? name : name + "." + QByteArray::number(i);
    const QByteArray infoPath  = info + candidate + ".trashinfo";
    const QByteArray filePath  = files + candidate;

    // creating the .trashinfo file exclusively reserves the name
    const int fd =
        ::open(infoPath.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
      if (errno == EEXIST) {
        continue;
      }
      return false;
    }

    // a file without .trashinfo (left by another implementation) also takes the name
    struct stat existing;
    if (::lstat(filePath.constData(), &existing) == 0) {
      ::close(fd);
      unlinkQuietly(infoPath);
      continue;
    }

    const bool written = writeAll(fd, content);
    if (::close(fd) != 0 || !written) {
      unlinkQuietly(infoPath);
      return false;
    }

    if (::rename(native.constData(), filePath.constData()) != 0) {
      unlinkQuietly(infoPath);
      return false;
    }

    return true;
  }

  errno = EEXIST;
  return false;
}

}  // namespace MOBase::details
