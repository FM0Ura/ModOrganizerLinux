// the update of the AppImage (selfupdate_linux.h, spec of Phase 5, section 3.4)

#include "selfupdate_linux.h"

#include <log.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace MOBase;

namespace selfupdate
{
namespace
{
  std::optional<Asset> findAsset(const QJsonObject& release,
                                 const std::function<bool(const QString&)>& match)
  {
    for (const auto& v : release["assets"].toArray()) {
      const QJsonObject asset = v.toObject();
      const QString name      = asset["name"].toString();
      if (match(name)) {
        return Asset{name, QUrl(asset["browser_download_url"].toString())};
      }
    }
    return std::nullopt;
  }

  // the SHA-256 of a file as lowercase hex, empty if it can't be read
  QByteArray fileSha256(const QString& path)
  {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) {
      return {};
    }
    return hash.result().toHex();
  }

  // flushes a file or directory to the disk; false on error
  bool syncPath(const QByteArray& path, int flags)
  {
    const int fd = ::open(path.constData(), flags | O_CLOEXEC);
    if (fd == -1) {
      return false;
    }
    const bool ok = (::fsync(fd) == 0);
    ::close(fd);
    return ok;
  }

  // logs, removes the downloaded file (the target was not touched) and returns e
  ReplaceError fail(const QString& downloaded, ReplaceError e, const QString& what)
  {
    log::error("update: can't replace the AppImage: {} ({})", what, toString(e));
    QFile::remove(downloaded);
    return e;
  }

  QString errnoString(int e)
  {
    return QString::fromLocal8Bit(std::strerror(e));
  }

  struct PendingReexec
  {
    QString path;
    QStringList args;
  };

  std::optional<PendingReexec> g_pending;
}  // namespace

UpdateMode updateMode(const QString& appImage)
{
  if (appImage.isEmpty()) {
    return UpdateMode::None;
  }

  // the new file is renamed over the old one: the folder must be writable
  const QString dir = QFileInfo(appImage).absolutePath();
  if (::access(QFile::encodeName(dir).constData(), W_OK) != 0) {
    return UpdateMode::NotifyOnly;
  }

  return UpdateMode::Install;
}

std::optional<Asset> appImageAsset(const QJsonObject& release)
{
  static const QRegularExpression re(
      QRegularExpression::anchoredPattern(R"(ModOrganizer-.+-x86_64\.AppImage)"));

  return findAsset(release, [](const QString& name) {
    return re.match(name).hasMatch();
  });
}

std::optional<Asset> checksumAsset(const QJsonObject& release)
{
  return findAsset(release, [](const QString& name) {
    return name == "SHA256SUMS";
  });
}

std::optional<QByteArray> expectedSha256(const QByteArray& sums,
                                         const QString& fileName)
{
  // "<hex>  <name>" (text mode) or "<hex> *<name>" (binary mode)
  static const QRegularExpression line(
      QRegularExpression::anchoredPattern(R"(([0-9a-fA-F]{64}) [ *](.+))"));

  for (const QByteArray& raw : sums.split('\n')) {
    QString s = QString::fromUtf8(raw);
    if (s.endsWith('\r')) {
      s.chop(1);
    }

    const auto m = line.match(s);
    if (m.hasMatch() && m.captured(2) == fileName) {
      return m.captured(1).toLower().toLatin1();
    }
  }

  return std::nullopt;
}

QString toString(ReplaceError e)
{
  switch (e) {
  case ReplaceError::None:
    return QCoreApplication::translate("SelfUpdater", "no error");
  case ReplaceError::NotWritable:
    return QCoreApplication::translate("SelfUpdater",
                                       "the folder of the AppImage is not writable");
  case ReplaceError::ChecksumMismatch:
    return QCoreApplication::translate(
        "SelfUpdater", "the downloaded file does not match the SHA256SUMS of the release");
  case ReplaceError::IoError:
  default:
    return QCoreApplication::translate("SelfUpdater",
                                       "the AppImage could not be replaced");
  }
}

ReplaceError replaceAppImage(const QString& downloaded, const QString& target,
                             const QByteArray& expectedSha256)
{
  const QFileInfo targetInfo(target);
  const QFileInfo newInfo(downloaded);
  const QString dir = targetInfo.absolutePath();

  // rename() is only atomic within a file system; the same directory makes sure of it
  if (newInfo.absolutePath() != dir) {
    return fail(downloaded, ReplaceError::IoError,
                QString("%1 is not in the folder of %2").arg(downloaded, target));
  }

  const QByteArray dirPath    = QFile::encodeName(dir);
  const QByteArray targetPath = QFile::encodeName(targetInfo.absoluteFilePath());
  const QByteArray newPath    = QFile::encodeName(newInfo.absoluteFilePath());

  if (::access(dirPath.constData(), W_OK) != 0) {
    return fail(downloaded, ReplaceError::NotWritable,
                QString("%1: %2").arg(dir, errnoString(errno)));
  }

  struct stat st = {};
  if (::stat(targetPath.constData(), &st) != 0) {
    return fail(downloaded, ReplaceError::IoError,
                QString("%1: %2").arg(target, errnoString(errno)));
  }

  const QByteArray actual = fileSha256(downloaded);
  if (actual.isEmpty() || actual != expectedSha256.toLower()) {
    return fail(downloaded, ReplaceError::ChecksumMismatch,
                QString("%1 has the SHA-256 '%2', expected '%3'")
                    .arg(downloaded, QString::fromLatin1(actual),
                         QString::fromLatin1(expectedSha256)));
  }

  if (::chmod(newPath.constData(), st.st_mode & 07777) != 0) {
    return fail(downloaded, ReplaceError::IoError,
                QString("chmod %1: %2").arg(downloaded, errnoString(errno)));
  }

  // the new file is complete on the disk before it takes the place of the old one
  if (!syncPath(newPath, O_RDONLY)) {
    return fail(downloaded, ReplaceError::IoError,
                QString("fsync %1: %2").arg(downloaded, errnoString(errno)));
  }

  if (::rename(newPath.constData(), targetPath.constData()) != 0) {
    const int e = errno;
    return fail(downloaded,
                (e == EACCES || e == EPERM || e == EROFS) ? ReplaceError::NotWritable
                                                          : ReplaceError::IoError,
                QString("rename %1 to %2: %3").arg(downloaded, target, errnoString(e)));
  }

  // the rename itself; not fatal, the file is already replaced
  if (!syncPath(dirPath, O_RDONLY | O_DIRECTORY)) {
    log::warn("update: fsync of {} failed: {}", dir, errnoString(errno));
  }

  log::info("update: replaced {}", target);
  return ReplaceError::None;
}

QString downloadPathFor(const QString& target)
{
  const QFileInfo fi(target);
  return QString("%1/.%2.update-%3")
      .arg(fi.absolutePath(), fi.fileName())
      .arg(QCoreApplication::applicationPid());
}

int removeStaleDownloads(const QString& target)
{
  const QFileInfo fi(target);
  const QString prefix = QString(".%1.update-").arg(fi.fileName());
  const qint64 self    = QCoreApplication::applicationPid();

  int removed = 0;
  const QDir dir(fi.absolutePath());
  for (const QString& name :
       dir.entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name)) {
    if (!name.startsWith(prefix)) {
      continue;
    }

    bool ok          = false;
    const qint64 pid = name.mid(prefix.size()).toLongLong(&ok, 10);
    if (!ok || pid <= 0 || pid == self) {
      continue;
    }

    // a process that still runs (or that this user can't signal) may be downloading
    if (pid <= std::numeric_limits<pid_t>::max() &&
        (::kill(static_cast<pid_t>(pid), 0) == 0 || errno != ESRCH)) {
      continue;
    }

    if (QFile::remove(dir.filePath(name))) {
      log::debug("update: removed the leftover download {}", dir.filePath(name));
      ++removed;
    }
  }

  return removed;
}

QStringList restartArguments(const QStringList& args, const QString& instance)
{
  QStringList result;
  for (const QString& a : args) {
    if (a == "--logs" || a == "--multiple") {
      result.append(a);
    }
  }

  if (!instance.isEmpty()) {
    result.append({"-i", instance});
  }

  return result;
}

void scheduleReexec(const QString& appImage, const QStringList& args)
{
  g_pending = PendingReexec{appImage, args};
}

void cancelReexec()
{
  g_pending.reset();
}

bool runPendingReexec()
{
  if (!g_pending) {
    return false;
  }

  const PendingReexec p = *g_pending;
  g_pending.reset();

  std::vector<std::string> strings;
  strings.push_back(QFile::encodeName(p.path).toStdString());
  for (const auto& a : p.args) {
    strings.push_back(a.toLocal8Bit().toStdString());
  }

  std::vector<char*> argv;
  for (auto& s : strings) {
    argv.push_back(s.data());
  }
  argv.push_back(nullptr);

  log::info("update: restarting into {}", p.path);
  ::execv(argv[0], argv.data());

  log::error("update: can't restart into {}: {}", p.path, errnoString(errno));
  return true;
}
}  // namespace selfupdate
