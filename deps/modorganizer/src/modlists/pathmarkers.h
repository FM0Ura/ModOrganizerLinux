#pragma once

#include <QString>
#include <vector>

namespace modlists
{

// Absolute paths without a trailing slash. An empty field takes no part in the
// substitution.
struct PathMarkers
{
  QString instance, game, protonPrefix, home;
};

// Replaces the machine paths by %INSTANCE%, %GAME%, %PROTON_PREFIX% and %HOME%,
// longest path first, only where the path ends on a path boundary (/, \, quote,
// space, ';', CR, LF or end of text). Also matches the Wine form (Z:\home\...).
QString toMarkers(const QString& text, const PathMarkers& markers);

// Inverse of toMarkers for the paths of `markers`. A marker followed by '\' is
// expanded to the Wine form (Z:\...) of the path.
QString fromMarkers(const QString& text, const PathMarkers& markers);

struct Leak
{
  QString file;
  int line;  // 1-based
  QString excerpt;
};

// Lines of `text` that still hold `home` or `userName` (whole word, case
// insensitive). A userName shorter than 3 characters is not searched.
std::vector<Leak> findLeaks(const QString& file, const QString& text,
                            const QString& userName, const QString& home);

}  // namespace modlists
