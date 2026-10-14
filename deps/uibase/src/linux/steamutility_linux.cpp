// Linux implementation of steamutility.h: the native Steam client (or its Flatpak) and
// the libraries listed in steamapps/libraryfolders.vdf.
//
// The VDF reader below only extracts library paths; a complete Steam module (app
// manifests, Proton prefixes) comes later. The file is untrusted input: its size and
// nesting depth are bounded and malformed content ends the parse.

#include "steamutility.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <vector>

namespace MOBase
{

namespace
{

  constexpr qint64 MaxVdfSize    = 16 * 1024 * 1024;
  constexpr std::size_t MaxDepth = 64;

  class VdfTokenizer
  {
  public:
    enum class Kind
    {
      String,
      Open,
      Close,
      End,
      Error
    };

    struct Token
    {
      Kind kind;
      QByteArray text;
    };

    explicit VdfTokenizer(const QByteArray& data) : m_data(data) {}

    Token next()
    {
      skipSpaceAndComments();

      if (m_pos >= m_data.size()) {
        return {Kind::End, {}};
      }

      const char c = m_data[m_pos];

      if (c == '{') {
        ++m_pos;
        return {Kind::Open, {}};
      }

      if (c == '}') {
        ++m_pos;
        return {Kind::Close, {}};
      }

      if (c == '"') {
        return quoted();
      }

      return bare();
    }

  private:
    const QByteArray& m_data;
    qsizetype m_pos = 0;

    static bool isSpace(char c)
    {
      return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
    }

    void skipSpaceAndComments()
    {
      while (m_pos < m_data.size()) {
        const char c = m_data[m_pos];

        if (isSpace(c)) {
          ++m_pos;
        } else if (c == '/' && m_pos + 1 < m_data.size() && m_data[m_pos + 1] == '/') {
          // comment until the end of the line
          while (m_pos < m_data.size() && m_data[m_pos] != '\n') {
            ++m_pos;
          }
        } else if (c == '[') {
          // conditional such as [$WIN32], ignored
          while (m_pos < m_data.size() && m_data[m_pos] != ']') {
            ++m_pos;
          }
          ++m_pos;
        } else {
          break;
        }
      }
    }

    Token quoted()
    {
      QByteArray text;
      ++m_pos;  // opening quote

      while (m_pos < m_data.size()) {
        const char c = m_data[m_pos++];

        if (c == '"') {
          return {Kind::String, text};
        }

        if (c == '\\' && m_pos < m_data.size()) {
          const char e = m_data[m_pos++];
          switch (e) {
          case 'n':
            text.append('\n');
            break;
          case 't':
            text.append('\t');
            break;
          case '\\':
          case '"':
            text.append(e);
            break;
          default:
            text.append('\\').append(e);
            break;
          }
          continue;
        }

        text.append(c);
      }

      // unterminated string
      return {Kind::Error, {}};
    }

    Token bare()
    {
      const qsizetype start = m_pos;

      while (m_pos < m_data.size()) {
        const char c = m_data[m_pos];
        if (isSpace(c) || c == '{' || c == '}' || c == '"') {
          break;
        }
        ++m_pos;
      }

      return {Kind::String, m_data.mid(start, m_pos - start)};
    }
  };

  bool isNumber(const QByteArray& s)
  {
    bool ok = false;
    s.toUInt(&ok);
    return ok;
  }

  // library paths of a libraryfolders.vdf file: the "path" values of the blocks inside
  // the root block (current format) or the values with numeric keys directly in the
  // root block (format used before 2021)
  QStringList parseLibraryFolders(const QByteArray& data)
  {
    QStringList paths;
    std::size_t depth = 0;
    VdfTokenizer tokens(data);

    for (;;) {
      const auto key = tokens.next();

      if (key.kind == VdfTokenizer::Kind::Close && depth > 0) {
        --depth;
        continue;
      }

      if (key.kind != VdfTokenizer::Kind::String) {
        // end of file, or malformed: keep what was found so far
        break;
      }

      const auto value = tokens.next();

      if (value.kind == VdfTokenizer::Kind::Open) {
        if (++depth > MaxDepth) {
          break;
        }
        continue;
      }

      if (value.kind != VdfTokenizer::Kind::String) {
        break;
      }

      const bool current =
          depth == 2 && key.text.compare("path", Qt::CaseInsensitive) == 0;
      const bool old = depth == 1 && isNumber(key.text);

      if ((current || old) && !value.text.isEmpty()) {
        paths.append(QString::fromUtf8(value.text));
      }
    }

    return paths;
  }

  QStringList libraryFolders(const QDir& steamDir)
  {
    QFile file(steamDir.absoluteFilePath("steamapps/libraryfolders.vdf"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxVdfSize) {
      return {};
    }

    return parseLibraryFolders(file.read(MaxVdfSize));
  }

}  // namespace

QString findSteam()
{
  // native package, the ~/.steam/steam link of older installs, Flatpak
  static const char* const candidates[] = {
      ".local/share/Steam",
      ".steam/steam",
      ".var/app/com.valvesoftware.Steam/.local/share/Steam",
  };

  const QDir home(QDir::homePath());

  for (const char* candidate : candidates) {
    const QString path = home.absoluteFilePath(candidate);
    if (QFileInfo(path).isDir()) {
      return QDir::cleanPath(path);
    }
  }

  return "";
}

QString findSteamGame(const QString& appName, const QString& validFile)
{
  const QString steam = findSteam();
  if (steam.isEmpty()) {
    return "";
  }

  const QDir steamDir(steam);

  // the Steam install is always a library
  QStringList libraries = {steamDir.absolutePath()};
  for (const QString& folder : libraryFolders(steamDir)) {
    const QString clean = QDir::cleanPath(folder);
    if (QDir::isAbsolutePath(clean) && !libraries.contains(clean)) {
      libraries.append(clean);
    }
  }

  for (const QString& library : libraries) {
    QDir libraryDir(library);
    if (!libraryDir.cd("steamapps/common/" + appName)) {
      continue;
    }

    if (validFile.isEmpty() || libraryDir.exists(validFile)) {
      return libraryDir.absolutePath();
    }
  }

  return "";
}

}  // namespace MOBase
