#include "nexuspage.h"

#include <QDate>
#include <QFileInfo>
#include <QLocale>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace nexuspage
{
namespace
{
// the style of the Nexus tab of the mod information dialog
const QString DescriptionCss = QStringLiteral(R"css(
    body
    {
      font-family: sans-serif;
      font-size: 14px;
      background: #404040;
      color: #f1f1f1;
      max-width: 1060px;
      margin-left: auto;
      margin-right: auto;
      padding-right: 7px;
      padding-left: 7px;
      padding-top: 20px;
      padding-bottom: 20px;
    }

    img {
      max-width: 100%;
    }

    figure.quote {
      position: relative;
      padding: 24px;
      margin: 10px 20px 10px 10px;
      color: #e1e1e1;
      line-height: 1.5;
      font-style: italic;
      border-left: 6px solid #57a5cc;
      border-left-color: rgb(87, 165, 204);
      background: #383838 url(data:image/svg+xml;base64,PHN2ZyBjbGFzcz0iaWNvbi1xdW90ZSIgdmVyc2lvbj0iMS4xIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHN0eWxlPSJmaWxsOnJnYig2OSwgNjksIDcwKTtoZWlnaHQ6MjlweDtsZWZ0OjE1cHg7cG9zaXRpb246YWJzb2x1dGU7dG9wOjE1cHg7d2lkdGg6MzhweDsiPjxwYXRoIGNsYXNzPSJwYXRoMSIgZD0iTTAgMjAuNjc0YzAgNy4yMjUgNC42NjggMTEuMzM3IDkuODkyIDExLjMzNyA0LjgyNC0wLjA2MiA4LjcxOS0zLjk1NiA4Ljc4MS04Ljc3NSAwLTQuNzg1LTMuMzM0LTguMDA5LTcuNTU4LTguMDA5LTAuMDc4LTAuMDA0LTAuMTctMC4wMDYtMC4yNjItMC4wMDYtMC43MDMgMC0xLjM3NyAwLjEyNC0yLjAwMSAwLjM1MiAxLjA0MS00LjAxNCA1LjE1My04LjY4MyA4LjcxLTEwLjU3MmwtNi4xMTMtNS4wMDJjLTYuODkxIDQuODkxLTExLjQ0OCAxMi4zMzgtMTEuNDQ4IDIwLjY3NHpNMjIuNjc1IDIwLjY3NGMwIDcuMjI1IDQuNjY4IDExLjMzNyA5Ljg5MiAxMS4zMzcgNC44LTAuMDU2IDguNjctMy45NjEgOC42Ny04Ljc2OSAwLTAuMDA0IDAtMC4wMDggMC0wLjAxMiAwLTQuNzc5LTMuMjIzLTguMDAyLTcuNDQ3LTguMDAyLTAuMDk1LTAuMDA2LTAuMjA2LTAuMDA5LTAuMzE4LTAuMDA5LTAuNjg0IDAtMS4zMzkgMC4xMjYtMS45NDMgMC4zNTUgMC45MjctNC4wMTQgNS4xNS04LjY4MiA4LjcwNy0xMC41NzJsLTYuMTI0LTUuMDAyYy02Ljg5MSA0Ljg5MS0xMS40MzcgMTIuMzM4LTExLjQzNyAyMC42NzR6IiBzdHlsZT0iZmlsbDpyZ2IoNjksIDY5LCA3MCk7aGVpZ2h0OmF1dG87d2lkdGg6YXV0bzsiLz48L3N2Zz4=) no-repeat;
    }

    figure.quote blockquote {
      position: relative;
      margin: 0;
      padding: 0;
    }

    div.spoiler_content {
      background: #262626;
      border: 1px dashed #3b3b3b;
      padding: 5px;
      margin: 5px;
    }

    div.bbc_spoiler_show{
      border: 1px solid black;
      background-color: #454545;
      font-size: 11px;
      padding: 3px;
      color: #E6E6E6;
      border-radius: 3px;
      display: inline-block;
      cursor: pointer;
    }

    details summary::marker {
      display:none;
    }

    summary:focus {
      outline: 0;
    }

    a
    {
      /*should avoid overflow with long links forcing wordwrap regardless of spaces*/
      overflow-wrap: break-word;
      word-wrap: break-word;

      color: #8197ec;
      text-decoration: none;
    }
)css");

// the same for the narrow panel: no maximum width and images keep their proportion
const QString PanelCss = QStringLiteral(R"css(
    body
    {
      font-family: sans-serif;
      font-size: 14px;
      background: #404040;
      color: #f1f1f1;
      margin-left: auto;
      margin-right: auto;
      padding-right: 7px;
      padding-left: 7px;
      padding-top: 20px;
      padding-bottom: 20px;
    }

    img {
      max-width: 100%;
      height: auto;
    }

    figure.quote {
      position: relative;
      padding: 24px;
      margin: 10px 20px 10px 10px;
      color: #e1e1e1;
      line-height: 1.5;
      font-style: italic;
      border-left: 6px solid #57a5cc;
      border-left-color: rgb(87, 165, 204);
      background: #383838 url(data:image/svg+xml;base64,PHN2ZyBjbGFzcz0iaWNvbi1xdW90ZSIgdmVyc2lvbj0iMS4xIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHN0eWxlPSJmaWxsOnJnYig2OSwgNjksIDcwKTtoZWlnaHQ6MjlweDtsZWZ0OjE1cHg7cG9zaXRpb246YWJzb2x1dGU7dG9wOjE1cHg7d2lkdGg6MzhweDsiPjxwYXRoIGNsYXNzPSJwYXRoMSIgZD0iTTAgMjAuNjc0YzAgNy4yMjUgNC42NjggMTEuMzM3IDkuODkyIDExLjMzNyA0LjgyNC0wLjA2MiA4LjcxOS0zLjk1NiA4Ljc4MS04Ljc3NSAwLTQuNzg1LTMuMzM0LTguMDA5LTcuNTU4LTguMDA5LTAuMDc4LTAuMDA0LTAuMTctMC4wMDYtMC4yNjItMC4wMDYtMC43MDMgMC0xLjM3NyAwLjEyNC0yLjAwMSAwLjM1MiAxLjA0MS00LjAxNCA1LjE1My04LjY4MyA4LjcxLTEwLjU3MmwtNi4xMTMtNS4wMDJjLTYuODkxIDQuODkxLTExLjQ0OCAxMi4zMzgtMTEuNDQ4IDIwLjY3NHpNMjIuNjc1IDIwLjY3NGMwIDcuMjI1IDQuNjY4IDExLjMzNyA5Ljg5MiAxMS4zMzcgNC44LTAuMDU2IDguNjctMy45NjEgOC42Ny04Ljc2OSAwLTAuMDA0IDAtMC4wMDggMC0wLjAxMiAwLTQuNzc5LTMuMjIzLTguMDAyLTcuNDQ3LTguMDAyLTAuMDk1LTAuMDA2LTAuMjA2LTAuMDA5LTAuMzE4LTAuMDA5LTAuNjg0IDAtMS4zMzkgMC4xMjYtMS45NDMgMC4zNTUgMC45MjctNC4wMTQgNS4xNS04LjY4MiA4LjcwNy0xMC41NzJsLTYuMTI0LTUuMDAyYy02Ljg5MSA0Ljg5MS0xMS40MzcgMTIuMzM4LTExLjQzNyAyMC42NzR6IiBzdHlsZT0iZmlsbDpyZ2IoNjksIDY5LCA3MCk7aGVpZ2h0OmF1dG87d2lkdGg6YXV0bzsiLz48L3N2Zz4=) no-repeat;
    }

    figure.quote blockquote {
      position: relative;
      margin: 0;
      padding: 0;
    }

    div.spoiler_content {
      background: #262626;
      border: 1px dashed #3b3b3b;
      padding: 5px;
      margin: 5px;
    }

    div.bbc_spoiler_show{
      border: 1px solid black;
      background-color: #454545;
      font-size: 11px;
      padding: 3px;
      color: #E6E6E6;
      border-radius: 3px;
      display: inline-block;
      cursor: pointer;
    }

    details summary::marker {
      display:none;
    }

    summary:focus {
      outline: 0;
    }

    a
    {
      /*should avoid overflow with long links forcing wordwrap regardless of spaces*/
      overflow-wrap: break-word;
      word-wrap: break-word;

      color: #8197ec;
      text-decoration: none;
    }
)css");

const QString HeadOpen = QStringLiteral(
    "<html>\n  <head>\n    <style class=\"nexus-description\">");
const QString HeadClose = QStringLiteral("    </style>\n  </head>\n  <body>");
const QString Tail      = QStringLiteral("</body>\n</html>");

QString dayText(const QDateTime& date, const QDateTime& now)
{
  if (!date.isValid()) {
    return QStringLiteral("never");
  }

  const QDate day   = date.toLocalTime().date();
  const QDate today = now.toLocalTime().date();

  if (day == today) {
    return QStringLiteral("today");
  }

  if (day == today.addDays(-1)) {
    return QStringLiteral("yesterday");
  }

  return QLocale().toString(day, QLocale::ShortFormat);
}
}  // namespace

QString cacheFileName(const QString& gameName, int modId, const QString& pictureUrl)
{
  static const QStringList extensions = {"jpg", "jpeg", "png", "webp", "gif"};

  QString ext = QFileInfo(QUrl(pictureUrl).path()).suffix().toLower();
  if (!extensions.contains(ext)) {
    ext = QStringLiteral("jpg");
  }

  // the game name must not leave the cache folder
  QString game = gameName;
  game.replace('/', '_').replace('\\', '_');

  return game + "_" + QString::number(modId) + "." + ext;
}

bool needsImageDownload(const QString& pictureUrl, const QString& cachedFromUrl,
                        bool fileExists)
{
  return !pictureUrl.isEmpty() && (!fileExists || cachedFromUrl != pictureUrl);
}

QString pageHtml(const QString& pictureFileUrl, const QString& headerHtml,
                 const QString& bodyHtml)
{
  QString html = HeadOpen + PanelCss + HeadClose;

  if (!pictureFileUrl.isEmpty()) {
    html += "<img src=\"" + pictureFileUrl + "\">";
  }

  return html + headerHtml + bodyHtml + Tail;
}

QString descriptionDocument(const QString& bodyHtml)
{
  return HeadOpen + DescriptionCss + HeadClose + bodyHtml + Tail;
}

QString statusText(const QDateTime& nexusLastModified, const QDateTime& lastQuery,
                   const QDateTime& now)
{
  return "Updated on Nexus " + dayText(nexusLastModified, now) + " · checked " +
         dayText(lastQuery, now);
}

QVariant panelRequestTag()
{
  return QVariantMap{{"source", "nexusPanel"}};
}

bool isPanelRequest(const QVariant& userData)
{
  return userData.typeId() == QMetaType::QVariantMap &&
         userData.toMap().value("source").toString() == "nexusPanel";
}
}  // namespace nexuspage
