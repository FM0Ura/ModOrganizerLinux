// Reader of INI files with the semantics of Windows' GetPrivateProfileString(), Linux
// only. The writer is MOBase::WriteRegistryValue() (registry.h).
//
// The rules:
// - section and key names are matched case-insensitively, after trimming whitespace,
//   the first section or key of a name wins (keys of a repeated section are added to
//   the first one),
// - values are trimmed, a pair of quotes ("" or '') around a value is removed,
// - lines starting with ';' are comments ('#' is not one, as on Windows),
// - a line without '=' in a section is a key with an empty value,
// - keys before the first section are ignored,
// - files are UTF-8 (with or without a BOM), UTF-16LE with a BOM, or Latin-1 when they
//   are not valid UTF-8; CRLF and LF line ends.

#ifndef UIBASE_LINUX_INIFILE_H
#define UIBASE_LINUX_INIFILE_H

#include <uibase/dllimport.h>
#include <uibase/win32compat.h>

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace MOBase::linux
{

class QDLLEXPORT IniFile
{
public:
  // nullopt if the file can't be read (errno is set)
  static std::optional<IniFile> load(const QString& path);

  // parses the content of a file
  static IniFile parse(const QByteArray& data);

  // the section names in order of appearance
  QStringList sectionNames() const;

  // the key names of a section (empty if it does not exist)
  QStringList keyNames(const QString& section) const;

  // the value of a key, nullopt if the section or key does not exist
  std::optional<QString> value(const QString& section, const QString& key) const;

private:
  struct Key
  {
    QString name;
    QString value;
  };

  struct Section
  {
    QString name;
    std::vector<Key> keys;
  };

  const Section* find(const QString& section) const;

  std::vector<Section> m_sections;
};

// GetPrivateProfileStringW() on top of IniFile, with its buffer semantics:
// - a null appName lists the section names, a null keyName the keys of the section, as
//   null-terminated strings followed by an extra null,
// - a missing file, section or key gives the default value (empty if null, with the
//   trailing blanks removed) and sets errno to ENOENT,
// - returns the number of characters written, without the terminating null; a
//   truncated result returns size - 1 (size - 2 for lists)
QDLLEXPORT DWORD getPrivateProfileString(LPCWSTR appName, LPCWSTR keyName,
                                         LPCWSTR defaultValue, wchar_t* returnedString,
                                         DWORD size, LPCWSTR fileName);

}  // namespace MOBase::linux

#endif  // UIBASE_LINUX_INIFILE_H
