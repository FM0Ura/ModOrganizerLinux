// Reader of INI files with the semantics of GetPrivateProfileString(), see
// uibase/linux/inifile.h.

#include <uibase/linux/inifile.h>

#include <QFile>
#include <QStringDecoder>

#include <algorithm>
#include <cerrno>
#include <string>

namespace MOBase::linux
{

namespace
{

  QString decodeIni(const QByteArray& data)
  {
    if (data.startsWith("\xff\xfe")) {
      return QStringDecoder(QStringDecoder::Utf16LE).decode(data.mid(2));
    }

    if (data.startsWith("\xef\xbb\xbf")) {
      return QString::fromUtf8(data.mid(3));
    }

    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString s = utf8.decode(data);

    if (utf8.hasError()) {
      return QString::fromLatin1(data);
    }

    return s;
  }

  // removes one pair of matching quotes around a trimmed value
  QString unquote(const QString& value)
  {
    if (value.size() >= 2 && ((value.front() == u'"' && value.back() == u'"') ||
                              (value.front() == u'\'' && value.back() == u'\''))) {
      return value.mid(1, value.size() - 2);
    }

    return value;
  }

  // copies `s` and its null into `out`, truncated to `size`; returns the number of
  // characters copied without the null, size - 1 if truncated
  DWORD copyString(const QString& s, wchar_t* out, DWORD size)
  {
    if (!out || size == 0) {
      return 0;
    }

    const std::wstring ws = s.toStdWString();
    const auto n          = std::min<std::size_t>(ws.size(), size - 1);

    std::copy_n(ws.data(), n, out);
    out[n] = L'\0';

    return static_cast<DWORD>(n);
  }

  // copies the strings, each followed by a null, then an extra null, truncated to
  // `size`; returns the number of characters copied without the last null, size - 2
  // if truncated
  DWORD copyList(const QStringList& list, wchar_t* out, DWORD size)
  {
    if (!out || size == 0) {
      return 0;
    }

    if (size == 1) {
      out[0] = L'\0';
      return 0;
    }

    std::wstring all;
    for (const auto& s : list) {
      all += s.toStdWString();
      all += L'\0';
    }

    if (all.size() + 1 <= size) {
      std::copy_n(all.data(), all.size(), out);
      out[all.size()] = L'\0';
      return static_cast<DWORD>(all.size());
    }

    // truncated: the last string is cut, and the list still ends with two nulls
    std::copy_n(all.data(), size - 2, out);
    out[size - 2] = L'\0';
    out[size - 1] = L'\0';

    return size - 2;
  }

  QString defaultString(LPCWSTR defaultValue)
  {
    if (!defaultValue) {
      return {};
    }

    // Windows removes the trailing blanks of the default value
    QString s = QString::fromWCharArray(defaultValue);
    while (!s.isEmpty() && s.back().isSpace()) {
      s.chop(1);
    }

    return s;
  }

}  // namespace

std::optional<IniFile> IniFile::load(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    errno = ENOENT;
    return std::nullopt;
  }

  return parse(file.readAll());
}

IniFile IniFile::parse(const QByteArray& data)
{
  IniFile ini;
  Section* current = nullptr;

  const QString text = decodeIni(data);

  for (const auto& rawLine : text.split(u'\n')) {
    const QString line = rawLine.trimmed();

    if (line.isEmpty() || line.startsWith(u';')) {
      continue;
    }

    if (line.startsWith(u'[')) {
      const auto close = line.indexOf(u']');
      const QString name =
          (close == -1 ? line.mid(1) : line.mid(1, close - 1)).trimmed();

      auto itor = std::find_if(ini.m_sections.begin(), ini.m_sections.end(), [&](auto&& s) {
        return s.name.compare(name, Qt::CaseInsensitive) == 0;
      });

      if (itor == ini.m_sections.end()) {
        ini.m_sections.push_back({name, {}});
        current = &ini.m_sections.back();
      } else {
        // keys of a repeated section are added to the first one
        current = &*itor;
      }

      continue;
    }

    if (!current) {
      // not in a section
      continue;
    }

    const auto equals  = line.indexOf(u'=');
    const QString name = (equals == -1 ? line : line.left(equals)).trimmed();
    const QString value =
        (equals == -1 ? QString() : unquote(line.mid(equals + 1).trimmed()));

    const bool exists =
        std::any_of(current->keys.begin(), current->keys.end(), [&](auto&& k) {
          return k.name.compare(name, Qt::CaseInsensitive) == 0;
        });

    if (!exists) {
      current->keys.push_back({name, value});
    }
  }

  return ini;
}

QStringList IniFile::sectionNames() const
{
  QStringList names;
  for (const auto& s : m_sections) {
    names.push_back(s.name);
  }

  return names;
}

const IniFile::Section* IniFile::find(const QString& section) const
{
  const QString name = section.trimmed();

  for (const auto& s : m_sections) {
    if (s.name.compare(name, Qt::CaseInsensitive) == 0) {
      return &s;
    }
  }

  return nullptr;
}

QStringList IniFile::keyNames(const QString& section) const
{
  QStringList names;

  if (const auto* s = find(section)) {
    for (const auto& k : s->keys) {
      names.push_back(k.name);
    }
  }

  return names;
}

std::optional<QString> IniFile::value(const QString& section, const QString& key) const
{
  if (const auto* s = find(section)) {
    const QString name = key.trimmed();

    for (const auto& k : s->keys) {
      if (k.name.compare(name, Qt::CaseInsensitive) == 0) {
        return k.value;
      }
    }
  }

  return std::nullopt;
}

DWORD getPrivateProfileString(LPCWSTR appName, LPCWSTR keyName, LPCWSTR defaultValue,
                              wchar_t* returnedString, DWORD size, LPCWSTR fileName)
{
  std::optional<IniFile> ini;

  if (!fileName) {
    errno = EINVAL;
  } else {
    ini = IniFile::load(QString::fromWCharArray(fileName));
  }

  if (!appName) {
    return copyList(ini ? ini->sectionNames() : QStringList(), returnedString, size);
  }

  const QString section = QString::fromWCharArray(appName);

  if (!keyName) {
    return copyList(ini ? ini->keyNames(section) : QStringList(), returnedString,
                    size);
  }

  if (ini) {
    if (const auto v = ini->value(section, QString::fromWCharArray(keyName))) {
      return copyString(*v, returnedString, size);
    }
  }

  errno = ENOENT;
  return copyString(defaultString(defaultValue), returnedString, size);
}

}  // namespace MOBase::linux
