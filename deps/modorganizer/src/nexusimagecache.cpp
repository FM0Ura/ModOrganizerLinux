#include "nexusimagecache.h"
#include "nexuspage.h"
#include <log.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

using namespace MOBase;

namespace
{
// time limit of the response
constexpr int kTimeoutMs = 30000;

// bigger downloads are aborted
constexpr qint64 kMaxBytes = 20 * 1024 * 1024;
}  // namespace

NexusImageCache::NexusImageCache(QString cacheDir, QObject* parent)
    : QObject(parent), m_CacheDir(std::move(cacheDir)),
      m_Manager(new QNetworkAccessManager(this))
{
  m_Manager->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

QString NexusImageCache::cachedFile(const QString& gameName, int modId,
                                    const QString& pictureUrl) const
{
  const QString path = QDir(m_CacheDir).absoluteFilePath(
      nexuspage::cacheFileName(gameName, modId, pictureUrl));
  return QFileInfo::exists(path) ? path : QString();
}

void NexusImageCache::fetch(const QString& gameName, int modId,
                            const QString& pictureUrl)
{
  const QString fileName = nexuspage::cacheFileName(gameName, modId, pictureUrl);
  const QString path     = QDir(m_CacheDir).absoluteFilePath(fileName);
  const QString urlPath  = QDir(m_CacheDir).absoluteFilePath(
      QFileInfo(fileName).completeBaseName() + ".url");

  QString cachedFrom;
  QFile urlFile(urlPath);
  if (urlFile.open(QIODevice::ReadOnly)) {
    cachedFrom = QString::fromUtf8(urlFile.readAll()).trimmed();
    urlFile.close();
  }

  const bool exists = QFileInfo::exists(path);

  if (!nexuspage::needsImageDownload(pictureUrl, cachedFrom, exists)) {
    if (exists) {
      emit imageReady(gameName, modId, path);
    }
    return;
  }

  if (m_InProgress.contains(path)) {
    return;
  }

  const QUrl url(pictureUrl);
  if (url.scheme() != "http" && url.scheme() != "https") {
    log::debug("nexus image cache: refusing picture url {}", pictureUrl);
    emit imageFailed(gameName, modId);
    return;
  }

  if (!QDir().mkpath(m_CacheDir)) {
    log::error("nexus image cache: cannot create {}", m_CacheDir);
    emit imageFailed(gameName, modId);
    return;
  }

  m_InProgress.insert(path);

  QNetworkRequest request{url};
  request.setTransferTimeout(kTimeoutMs);
  QNetworkReply* reply = m_Manager->get(request);

  connect(reply, &QNetworkReply::downloadProgress, this,
          [reply, pictureUrl](qint64 received, qint64 total) {
            if (received > kMaxBytes || total > kMaxBytes) {
              log::debug("nexus image cache: {} is too big", pictureUrl);
              reply->abort();
            }
          });

  connect(reply, &QNetworkReply::finished, this, [=, this] {
    reply->deleteLater();
    m_InProgress.remove(path);

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok =
        reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;

    const QString partPath = path + ".part";
    bool saved             = false;

    if (ok) {
      QFile part(partPath);
      if (part.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QByteArray data = reply->readAll();
        saved                 = part.write(data) == data.size();
        part.close();
      }
      if (saved) {
        QFile::remove(path);
        saved = QFile::rename(partPath, path);
      }
      if (saved) {
        QFile out(urlPath);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
          out.write(pictureUrl.toUtf8());
        }
      }
    }

    if (!saved) {
      QFile::remove(partPath);
      log::debug("nexus image cache: download of {} failed ({})", pictureUrl,
                 reply->errorString());
      emit imageFailed(gameName, modId);
      return;
    }

    emit imageReady(gameName, modId, path);
  });
}
