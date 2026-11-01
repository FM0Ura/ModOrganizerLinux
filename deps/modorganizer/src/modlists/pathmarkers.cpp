#include "pathmarkers.h"

#include <algorithm>

namespace modlists
{

namespace
{

  struct Entry
  {
    QString marker;
    QString path;  // Linux form
    QString wine;  // Z:\... form
  };

  QString stripTrailingSlash(QString p)
  {
    while (p.size() > 1 && p.endsWith(QLatin1Char('/'))) {
      p.chop(1);
    }
    return p;
  }

  std::vector<Entry> entries(const PathMarkers& m)
  {
    std::vector<Entry> out;
    const std::pair<const char*, const QString*> all[] = {
        {"%INSTANCE%", &m.instance},
        {"%GAME%", &m.game},
        {"%PROTON_PREFIX%", &m.protonPrefix},
        {"%HOME%", &m.home}};
    for (const auto& [marker, path] : all) {
      const QString p = stripTrailingSlash(*path);
      if (p.isEmpty() || p == QLatin1String("/")) {
        continue;
      }
      QString wine = QStringLiteral("Z:") + p;
      wine.replace(QLatin1Char('/'), QLatin1Char('\\'));
      out.push_back({QString::fromLatin1(marker), p, wine});
    }
    return out;
  }

  bool isBoundary(const QString& text, qsizetype pos)
  {
    if (pos >= text.size()) {
      return true;
    }
    const QChar c = text[pos];
    return c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char('"') ||
           c == QLatin1Char('\'') || c == QLatin1Char(' ') || c == QLatin1Char(';') ||
           c == QLatin1Char('\r') || c == QLatin1Char('\n');
  }

  // A match may only start at the start of the text or after a character that
  // cannot be part of a path segment.
  bool isLeftBoundary(const QString& text, qsizetype pos)
  {
    if (pos <= 0) {
      return true;
    }
    const QChar c = text[pos - 1];
    return !(c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('_') ||
             c == QLatin1Char('-') || c == QLatin1Char('%') || c == QLatin1Char('/') ||
             c == QLatin1Char('\\'));
  }

  bool isWordChar(QChar c)
  {
    return c.isLetterOrNumber() || c == QLatin1Char('_');
  }

  // whole-word, case-insensitive search of needle in line
  bool containsWord(const QString& line, const QString& needle)
  {
    if (needle.isEmpty()) {
      return false;
    }
    qsizetype from = 0;
    for (;;) {
      const qsizetype i = line.indexOf(needle, from, Qt::CaseInsensitive);
      if (i < 0) {
        return false;
      }
      const qsizetype end = i + needle.size();
      const bool leftOk  = i == 0 || !isWordChar(line[i - 1]);
      const bool rightOk = end >= line.size() || !isWordChar(line[end]);
      if (leftOk && rightOk) {
        return true;
      }
      from = i + 1;
    }
  }

}  // namespace

QString toMarkers(const QString& text, const PathMarkers& markers)
{
  struct Candidate
  {
    QString marker;
    QString needle;
    bool wine;
  };

  std::vector<Candidate> candidates;
  for (const auto& e : entries(markers)) {
    candidates.push_back({e.marker, e.path, false});
    candidates.push_back({e.marker, e.wine, true});
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate& a, const Candidate& b) {
                     return a.needle.size() > b.needle.size();
                   });

  QString out;
  out.reserve(text.size());
  qsizetype i = 0;
  while (i < text.size()) {
    bool matched = false;
    for (const auto& c : candidates) {
      // the drive letter of the Wine form is case insensitive, the rest is not
      const bool ok =
          c.wine ? (text.mid(i, 2).compare(QLatin1String("Z:"), Qt::CaseInsensitive) == 0 &&
                    text.mid(i + 2, c.needle.size() - 2) == c.needle.mid(2))
                 : text.mid(i, c.needle.size()) == c.needle;
      if (ok && isLeftBoundary(text, i) && isBoundary(text, i + c.needle.size())) {
        out += c.wine ? QStringLiteral("Z:") + c.marker : c.marker;
        i += c.needle.size();
        matched = true;
        break;
      }
    }
    if (!matched) {
      out += text[i++];
    }
  }
  return out;
}

QString fromMarkers(const QString& text, const PathMarkers& markers)
{
  const auto list = entries(markers);

  QString out;
  out.reserve(text.size());
  qsizetype i = 0;
  while (i < text.size()) {
    bool matched = false;
    if (text[i] == QLatin1Char('%')) {
      for (const auto& e : list) {
        if (text.mid(i, e.marker.size()) == e.marker) {
          out += e.path;
          i += e.marker.size();
          matched = true;
          break;
        }
      }
    } else if (text.mid(i, 2).compare(QLatin1String("Z:"), Qt::CaseInsensitive) == 0 &&
               text.size() > i + 2 && text[i + 2] == QLatin1Char('%')) {
      for (const auto& e : list) {
        if (text.mid(i + 2, e.marker.size()) == e.marker) {
          out += e.wine;
          i += 2 + e.marker.size();
          matched = true;
          break;
        }
      }
    }
    if (!matched) {
      out += text[i++];
    }
  }
  return out;
}

std::vector<Leak> findLeaks(const QString& file, const QString& text,
                            const QString& userName, const QString& home)
{
  std::vector<Leak> leaks;
  const bool useName = userName.size() >= 3;
  const QString h    = stripTrailingSlash(home);

  const auto lines = text.split(QLatin1Char('\n'));
  for (int n = 0; n < lines.size(); ++n) {
    QString line = lines[n];
    if (line.endsWith(QLatin1Char('\r'))) {
      line.chop(1);
    }
    if ((useName && containsWord(line, userName)) ||
        (!h.isEmpty() && containsWord(line, h))) {
      QString excerpt = line.trimmed();
      if (excerpt.size() > 200) {
        excerpt = excerpt.left(200) + QStringLiteral("...");
      }
      leaks.push_back({file, n + 1, excerpt});
    }
  }
  return leaks;
}

}  // namespace modlists
