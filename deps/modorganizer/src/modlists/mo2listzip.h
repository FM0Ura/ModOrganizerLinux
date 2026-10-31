#ifndef MODLISTS_MO2LISTZIP_H
#define MODLISTS_MO2LISTZIP_H

#include "plan.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <utility>
#include <vector>

// The .mo2list file: a ZIP with manifest.json, profile/..., bundled/<mod>/... and an
// optional README.md. The file comes from someone else, so it is untrusted: open()
// refuses anything that could write outside the destination, and nothing here
// executes anything.
namespace modlists
{

// Safe folder name for bundled/<name>/: path characters and control characters become
// '_', spaces and dots at the ends are trimmed and "-<8 hex of the sha1 of the name>"
// is appended, so two names that only differ by the replaced characters do not collide.
// The original name stays in ModEntry::name.
QString bundledDirName(const QString& modName);

struct PackageInput
{
  Plan plan;
  // relative to profile/
  std::map<QString, QByteArray> profileFiles;
  // (path inside bundled/, absolute path on disk)
  std::vector<std::pair<QString, QString>> bundledFiles;
  QString readme;
};

// Writes to "<path>.part" and renames at the end. Throws std::runtime_error on I/O
// errors.
void writeMo2List(const QString& path, const PackageInput& input);

class Mo2ListPackage
{
public:
  // Throws PlanError(Invalid) for a missing or bad manifest.json and for an entry with
  // "..", a leading '/' or '\', a drive letter or a symbolic link (the message names
  // the entry); PlanError(NewerSchema) for a newer schemaVersion.
  static Mo2ListPackage open(const QString& path);

  const Plan& plan() const { return m_plan; }
  QString readme() const;
  // empty when the file does not exist
  QByteArray profileFile(const QString& name) const;
  // files under bundled/<bundledDir>/, relative to it
  QStringList bundledEntries(const QString& bundledDir) const;
  // Extracts bundled/<bundledDir>/ into destDir. Throws std::runtime_error on I/O
  // errors or if an entry would land outside destDir.
  void extractBundled(const QString& bundledDir, const QString& destDir) const;

  // every file entry of the ZIP
  QStringList entries() const { return m_files; }
  QByteArray entryData(const QString& entry) const;

private:
  Mo2ListPackage() = default;

  std::shared_ptr<void> m_reader;  // QZipReader
  Plan m_plan;
  QStringList m_files;
};

struct ExecFinding
{
  enum class Kind
  {
    NativeFile,
    Executable,
    IniLoadsCode
  };

  Kind kind;
  QString what;
  QString detail;
};

// What the package would run or load, for the user to see before importing:
// bundled native files (by extension or by magic), the executables of the plan and
// INI lines with ".dll" or a path marker.
std::vector<ExecFinding> findExecutableContent(const Mo2ListPackage& package);

}  // namespace modlists

#endif  // MODLISTS_MO2LISTZIP_H
