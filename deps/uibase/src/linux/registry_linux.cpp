// Linux implementation of WriteRegistryValue(): WritePrivateProfileStringW semantics
// on a plain INI file.
//
// - section and key names are matched case-insensitively, after trimming whitespace,
//   and keep their existing spelling,
// - a missing key is added after the last non-blank line of its section, a missing
//   section is added at the end of the file, a missing file is created,
// - a null key removes the whole section, a null value removes the key,
// - every other byte of the file is preserved; new lines use the file's line ending
//   (CRLF for new files and files without line breaks, these are Windows INI files),
// - new text is written as UTF-8; UTF-16 files are refused (EILSEQ),
// - a file without write permission for its owner is the equivalent of the read-only
//   attribute: with a GUI, the same dialog as on Windows offers to clear it,
// - failures return false with errno set.

#include "log.h"
#include "registry.h"
#include "report.h"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QString>

#include <cctype>
#include <cerrno>
#include <fcntl.h>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace MOBase
{

namespace
{

  struct Line
  {
    QByteArray text;        // without the line ending
    QByteArray terminator;  // "\r\n", "\n" or "" (last line without line ending)
  };

  struct IniFile
  {
    QByteArray bom;
    std::vector<Line> lines;
    QByteArray eol;
  };

  // reads the whole file; a missing file is empty
  std::optional<QByteArray> readFile(const QByteArray& path)
  {
    const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      if (errno == ENOENT) {
        return QByteArray();
      }
      return std::nullopt;
    }

    QByteArray data;
    char buffer[65536];

    for (;;) {
      const ssize_t n = ::read(fd, buffer, sizeof(buffer));
      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }
        const int e = errno;
        ::close(fd);
        errno = e;
        return std::nullopt;
      }

      if (n == 0) {
        break;
      }

      data.append(buffer, n);
    }

    ::close(fd);
    return data;
  }

  bool writeFile(const QByteArray& path, const QByteArray& data)
  {
    const int fd =
        ::open(path.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
      return false;
    }

    const char* p   = data.constData();
    std::size_t len = static_cast<std::size_t>(data.size());

    while (len > 0) {
      const ssize_t n = ::write(fd, p, len);
      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }
        const int e = errno;
        ::close(fd);
        errno = e;
        return false;
      }

      p += n;
      len -= static_cast<std::size_t>(n);
    }

    return ::close(fd) == 0;
  }

  std::optional<IniFile> parse(QByteArray data)
  {
    IniFile ini;

    if (data.startsWith("\xff\xfe") || data.startsWith("\xfe\xff")) {
      errno = EILSEQ;
      return std::nullopt;
    }

    if (data.startsWith("\xef\xbb\xbf")) {
      ini.bom = data.left(3);
      data.remove(0, 3);
    }

    ini.eol = (data.contains("\r\n") || !data.contains('\n')) ? "\r\n" : "\n";

    qsizetype start = 0;
    while (start < data.size()) {
      const qsizetype nl = data.indexOf('\n', start);
      if (nl < 0) {
        ini.lines.push_back({data.mid(start), {}});
        break;
      }

      const bool cr = nl > start && data[nl - 1] == '\r';
      ini.lines.push_back(
          {data.mid(start, nl - start - (cr ? 1 : 0)), cr ? "\r\n" : "\n"});
      start = nl + 1;
    }

    return ini;
  }

  QByteArray serialize(const IniFile& ini)
  {
    QByteArray out = ini.bom;
    for (const auto& line : ini.lines) {
      out += line.text + line.terminator;
    }
    return out;
  }

  bool sameName(const QByteArray& a, const QString& b)
  {
    return QString::fromUtf8(a.trimmed()).compare(b.trimmed(), Qt::CaseInsensitive) ==
           0;
  }

  // section name of a "[name]" line
  std::optional<QByteArray> sectionName(const QByteArray& line)
  {
    const QByteArray t = line.trimmed();
    if (!t.startsWith('[')) {
      return std::nullopt;
    }

    const qsizetype end = t.indexOf(']');
    if (end < 0) {
      return std::nullopt;
    }

    return t.mid(1, end - 1);
  }

  // position of '=' in a "key=value" line, -1 for other lines
  qsizetype keySeparator(const QByteArray& line)
  {
    const QByteArray t = line.trimmed();
    if (t.isEmpty() || t.startsWith(';') || t.startsWith('[')) {
      return -1;
    }

    return line.indexOf('=');
  }

  void insertLine(IniFile& ini, std::size_t index, const QByteArray& text)
  {
    // the line before the new one may be the last one of the file, without ending
    if (index > 0 && ini.lines[index - 1].terminator.isEmpty()) {
      ini.lines[index - 1].terminator = ini.eol;
    }

    ini.lines.insert(ini.lines.begin() + static_cast<std::ptrdiff_t>(index),
                     Line{text, ini.eol});
  }

  void apply(IniFile& ini, const QString& app, const QString* key, const QString* value)
  {
    // [first, last) lines of the section, header included
    std::optional<std::size_t> first;
    std::size_t last = ini.lines.size();

    for (std::size_t i = 0; i < ini.lines.size(); ++i) {
      const auto name = sectionName(ini.lines[i].text);
      if (!name) {
        continue;
      }

      if (first) {
        last = i;
        break;
      }

      if (sameName(*name, app)) {
        first = i;
      }
    }

    if (!key) {
      // remove the section
      if (first) {
        ini.lines.erase(ini.lines.begin() + static_cast<std::ptrdiff_t>(*first),
                        ini.lines.begin() + static_cast<std::ptrdiff_t>(last));
      }
      return;
    }

    if (!first) {
      if (value) {
        insertLine(ini, ini.lines.size(), "[" + app.toUtf8() + "]");
        insertLine(ini, ini.lines.size(), key->toUtf8() + "=" + value->toUtf8());
      }
      return;
    }

    std::size_t lastContent = *first;
    for (std::size_t i = *first + 1; i < last; ++i) {
      auto& line = ini.lines[i];

      if (!line.text.trimmed().isEmpty()) {
        lastContent = i;
      }

      const qsizetype sep = keySeparator(line.text);
      if (sep < 0 || !sameName(line.text.left(sep), *key)) {
        continue;
      }

      if (!value) {
        ini.lines.erase(ini.lines.begin() + static_cast<std::ptrdiff_t>(i));
        return;
      }

      // keep the indentation and the existing spelling of the key
      QByteArray existing = line.text.left(sep);
      while (!existing.isEmpty() &&
             std::isspace(static_cast<unsigned char>(existing[existing.size() - 1]))) {
        existing.chop(1);
      }

      line.text = existing + "=" + value->toUtf8();
      return;
    }

    if (value) {
      insertLine(ini, lastContent + 1, key->toUtf8() + "=" + value->toUtf8());
    }
  }

  bool writeValue(const QByteArray& path, const QString& app, const QString* key,
                  const QString* value)
  {
    auto data = readFile(path);
    if (!data) {
      return false;
    }

    auto ini = parse(std::move(*data));
    if (!ini) {
      return false;
    }

    apply(*ini, app, key, value);
    return writeFile(path, serialize(*ini));
  }

  // whether a write failure is due to the equivalent of the read-only attribute, and
  // the file's permissions
  std::optional<mode_t> readOnlyMode(const QByteArray& path)
  {
    struct stat st;
    if (::stat(path.constData(), &st) != 0) {
      return std::nullopt;
    }

    if (!S_ISREG(st.st_mode) || st.st_uid != ::geteuid() || (st.st_mode & S_IWUSR)) {
      return std::nullopt;
    }

    return st.st_mode & 07777;
  }

  // the dialog of registry.cpp, only with a GUI
  bool askToClearReadOnly(const QString& fileName, QMessageBox::StandardButton* result)
  {
    if (!qobject_cast<QApplication*>(QCoreApplication::instance())) {
      return false;
    }

    QFileInfo fileInfo(fileName);

    *result = MOBase::TaskDialog(qApp->activeModalWidget(),
                                 QObject::tr("INI file is read-only"))
                  .main(QObject::tr("INI file is read-only"))
                  .content(QObject::tr("Mod Organizer is attempting to write to \"%1\" "
                                       "which is currently set to read-only.")
                               .arg(fileInfo.fileName()))
                  .icon(QMessageBox::Warning)
                  .button({QObject::tr("Clear the read-only flag"), QMessageBox::Yes})
                  .button({QObject::tr("Allow the write once"),
                           QObject::tr("The file will be set to read-only again."),
                           QMessageBox::Ignore})
                  .button({QObject::tr("Skip this file"), QMessageBox::No})
                  .remember("clearReadOnly", fileInfo.fileName())
                  .exec();

    return true;
  }

}  // namespace

bool WriteRegistryValue(LPCWSTR appName, LPCWSTR keyName, LPCWSTR value,
                        LPCWSTR fileName)
{
  if (!appName || !fileName) {
    errno = EINVAL;
    return false;
  }

  const QString file = QString::fromWCharArray(fileName);
  const QString app  = QString::fromWCharArray(appName);
  const QString key  = keyName ? QString::fromWCharArray(keyName) : QString();
  const QString val  = value ? QString::fromWCharArray(value) : QString();

  const QByteArray path   = QFile::encodeName(file);
  const QString* keyPtr   = keyName ? &key : nullptr;
  const QString* valuePtr = value ? &val : nullptr;

  if (writeValue(path, app, keyPtr, valuePtr)) {
    return true;
  }

  if (errno != EACCES) {
    return false;
  }

  const auto mode                    = readOnlyMode(path);
  QMessageBox::StandardButton result = QMessageBox::No;
  if (!mode || !askToClearReadOnly(file, &result)) {
    errno = EACCES;
    return false;
  }

  bool success = false;

  // clear the read-only flag if requested
  if (result & (QMessageBox::Yes | QMessageBox::Ignore)) {
    if (::chmod(path.constData(), *mode | S_IWUSR) == 0) {
      success = writeValue(path, app, keyPtr, valuePtr);
    }
  }

  // set the read-only flag if requested
  if (result == QMessageBox::Ignore) {
    const int e = errno;
    ::chmod(path.constData(), *mode);
    errno = e;
  }

  if (!success && result == QMessageBox::No) {
    errno = EACCES;
  }

  return success;
}

}  // namespace MOBase
