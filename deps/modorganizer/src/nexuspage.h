#ifndef NEXUSPAGE_H
#define NEXUSPAGE_H

#include <QDateTime>
#include <QString>
#include <QVariant>

// Pure helpers of the Nexus page in the side panel (picture and description of the
// selected mod): the name of the cached picture, when to download it again, the HTML
// of the page and the status line.
namespace nexuspage
{
// "<gameName>_<modId>.<ext>"; ext is the suffix of the path of the URL (without the
// query), in lower case, if it is jpg, jpeg, png, webp or gif, else jpg
QString cacheFileName(const QString& gameName, int modId, const QString& pictureUrl);

// true if there is a picture and it is not cached yet or was cached from another URL
bool needsImageDownload(const QString& pictureUrl, const QString& cachedFromUrl,
                        bool fileExists);

// the complete document of the panel: the picture (if pictureFileUrl is not empty),
// headerHtml and bodyHtml; they are concatenated as they are, never used as a format
QString pageHtml(const QString& pictureFileUrl, const QString& headerHtml,
                 const QString& bodyHtml);

// the document of the Nexus tab of the mod information dialog
QString descriptionDocument(const QString& bodyHtml);

// "Updated on Nexus <date> · checked <date>"; a date is "today", "yesterday" or short
// in the locale, "never" if invalid
QString statusText(const QDateTime& nexusLastModified, const QDateTime& lastQuery,
                   const QDateTime& now);

// the userData of the requests of the panel, which do not open a popup on error
QVariant panelRequestTag();
bool isPanelRequest(const QVariant& userData);
}  // namespace nexuspage

#endif  // NEXUSPAGE_H
