#include "mo2listzip.h"

#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <stdexcept>

namespace modlists
{

namespace
{

QZipReader* reader(const std::shared_ptr<void>& p)
{
  return static_cast<QZipReader*>(p.get());
}

[[noreturn]] void invalid(const QString& message)
{
  throw PlanError(PlanError::Kind::Invalid, message);
}

bool unsafeEntry(const QString& name)
{
  if (name.isEmpty() || name.startsWith('/') || name.startsWith('\\')) {
    return true;
  }
  // drive letter, with either slash or none after it
  if (name.size() >= 2 && name[1] == ':' && name[0].isLetter()) {
    return true;
  }
  if (name.contains(':')) {
    return true;
  }
  const auto parts = name.split(QRegularExpression("[/\\\\]"));
  return parts.contains("..");
}

bool hasMagic(const QByteArray& data)
{
  return data.startsWith("MZ") || data.startsWith("\x7f"
                                                  "ELF");
}

const QSet<QString>& nativeExtensions()
{
  static const QSet<QString> set = {"dll", "exe", "bat", "cmd", "sh",
                                    "ps1", "py",  "so",  "jar", "vbs"};
  return set;
}

bool hasMarker(const QString& value)
{
  return value.contains("%INSTANCE%") || value.contains("%GAME%") ||
         value.contains("%PROTON_PREFIX%") || value.contains("%HOME%");
}

}  // namespace

QString bundledDirName(const QString& modName)
{
  QString safe;
  for (const QChar c : modName) {
    if (QString("/\\:*?\"<>|").contains(c) || c.unicode() < 0x20 ||
        c.unicode() == 0x7f) {
      safe += '_';
    } else {
      safe += c;
    }
  }

  int start = 0, end = safe.size();
  auto trimmed = [](QChar c) {
    return c == ' ' || c == '.';
  };
  while (start < end && trimmed(safe[start])) {
    ++start;
  }
  while (end > start && trimmed(safe[end - 1])) {
    --end;
  }
  safe = safe.mid(start, end - start);

  const auto hash =
      QCryptographicHash::hash(modName.toUtf8(), QCryptographicHash::Sha1).toHex();
  return safe + "-" + QString::fromLatin1(hash.left(8));
}

void writeMo2List(const QString& path, const PackageInput& input)
{
  const QString part = path + ".part";
  QFile::remove(part);

  {
    QZipWriter zip(part);
    if (!zip.isWritable()) {
      throw std::runtime_error("Cannot write " + part.toStdString());
    }

    zip.addFile("manifest.json",
                QJsonDocument(toJson(input.plan)).toJson(QJsonDocument::Indented));
    if (!input.readme.isEmpty()) {
      zip.addFile("README.md", input.readme.toUtf8());
    }
    for (const auto& [name, data] : input.profileFiles) {
      zip.addFile("profile/" + name, data);
    }
    for (const auto& [name, disk] : input.bundledFiles) {
      QFile file(disk);
      if (!file.open(QIODevice::ReadOnly)) {
        zip.close();
        QFile::remove(part);
        throw std::runtime_error("Cannot read " + disk.toStdString());
      }
      zip.addFile("bundled/" + name, &file);
    }
    zip.close();

    if (zip.status() != QZipWriter::NoError) {
      QFile::remove(part);
      throw std::runtime_error("Cannot write " + part.toStdString());
    }
  }

  QFile::remove(path);
  if (!QFile::rename(part, path)) {
    QFile::remove(part);
    throw std::runtime_error("Cannot create " + path.toStdString());
  }
}

Mo2ListPackage Mo2ListPackage::open(const QString& path)
{
  auto zip = std::shared_ptr<QZipReader>(new QZipReader(path));
  if (!zip->isReadable() || zip->status() != QZipReader::NoError) {
    invalid("Cannot read " + path);
  }

  Mo2ListPackage package;
  bool hasManifest = false;

  for (const auto& info : zip->fileInfoList()) {
    if (unsafeEntry(info.filePath)) {
      invalid("Unsafe entry in the modlist: " + info.filePath);
    }
    if (info.isSymLink) {
      invalid("Symbolic link in the modlist: " + info.filePath);
    }
    if (info.isDir) {
      continue;
    }
    package.m_files.append(info.filePath);
    if (info.filePath == "manifest.json") {
      hasManifest = true;
    }
  }

  if (!hasManifest) {
    invalid("The file has no manifest.json");
  }

  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(zip->fileData("manifest.json"), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    invalid("manifest.json is not valid: " + error.errorString());
  }

  package.m_plan   = fromJson(doc.object());
  package.m_reader = zip;
  return package;
}

QByteArray Mo2ListPackage::entryData(const QString& entry) const
{
  if (!m_files.contains(entry)) {
    return {};
  }
  return reader(m_reader)->fileData(entry);
}

QString Mo2ListPackage::readme() const
{
  return QString::fromUtf8(entryData("README.md"));
}

QByteArray Mo2ListPackage::profileFile(const QString& name) const
{
  return entryData("profile/" + name);
}

QStringList Mo2ListPackage::bundledEntries(const QString& bundledDir) const
{
  const QString prefix = "bundled/" + bundledDir + "/";
  QStringList out;
  for (const auto& file : m_files) {
    if (file.startsWith(prefix) && file.size() > prefix.size()) {
      out.append(file.mid(prefix.size()));
    }
  }
  return out;
}

void Mo2ListPackage::extractBundled(const QString& bundledDir,
                                    const QString& destDir) const
{
  const QDir root(destDir);
  if (!root.mkpath(".")) {
    throw std::runtime_error("Cannot create " + destDir.toStdString());
  }
  const QString rootPath = QDir::cleanPath(root.absolutePath());

  for (const auto& rel : bundledEntries(bundledDir)) {
    const QString target = QDir::cleanPath(rootPath + "/" + rel);
    if (!target.startsWith(rootPath + "/")) {
      throw std::runtime_error("Entry outside the destination: " + rel.toStdString());
    }
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
      throw std::runtime_error("Cannot create the folder of " + target.toStdString());
    }

    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      throw std::runtime_error("Cannot write " + target.toStdString());
    }
    const QByteArray data = entryData("bundled/" + bundledDir + "/" + rel);
    if (file.write(data) != data.size()) {
      throw std::runtime_error("Cannot write " + target.toStdString());
    }
  }
}

std::vector<ExecFinding> findExecutableContent(const Mo2ListPackage& package)
{
  std::vector<ExecFinding> out;

  for (const auto& entry : package.entries()) {
    if (!entry.startsWith("bundled/")) {
      continue;
    }
    const QString ext = QFileInfo(entry).suffix().toLower();
    if (nativeExtensions().contains(ext)) {
      out.push_back({ExecFinding::Kind::NativeFile, entry, "." + ext});
    } else if (hasMagic(package.entryData(entry).left(4))) {
      out.push_back({ExecFinding::Kind::NativeFile, entry, "executable header"});
    }
  }

  for (const auto& e : package.plan().executables) {
    QStringList detail = {e.binary};
    if (!e.arguments.isEmpty()) {
      detail << e.arguments;
    }
    if (!e.environment.isEmpty()) {
      detail << "env: " + e.environment.join(' ');
    }
    out.push_back({ExecFinding::Kind::Executable, e.title, detail.join(' ')});
  }

  for (const auto& entry : package.entries()) {
    if (!entry.startsWith("profile/") || !entry.endsWith(".ini", Qt::CaseInsensitive)) {
      continue;
    }
    const auto lines =
        QString::fromUtf8(package.entryData(entry)).split('\n');
    for (const auto& raw : lines) {
      const QString line = raw.trimmed();
      const int eq       = line.indexOf('=');
      if (eq < 0) {
        continue;
      }
      const QString value = line.mid(eq + 1);
      if (value.contains(".dll", Qt::CaseInsensitive) || hasMarker(value)) {
        out.push_back({ExecFinding::Kind::IniLoadsCode, entry, line});
      }
    }
  }

  return out;
}

}  // namespace modlists
