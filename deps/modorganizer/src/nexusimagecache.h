#ifndef NEXUSIMAGECACHE_H
#define NEXUSIMAGECACHE_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>

// Disk cache of the pictures of the mods shown in the Nexus page of the side panel:
// "<gameName>_<modId>.<ext>" and, beside it, "<gameName>_<modId>.url" with the URL it
// came from
class NexusImageCache : public QObject
{
  Q_OBJECT

public:
  explicit NexusImageCache(QString cacheDir, QObject* parent = nullptr);

  QString cacheDir() const { return m_CacheDir; }

  // the absolute path of the cached picture if it exists, else {}
  QString cachedFile(const QString& gameName, int modId,
                     const QString& pictureUrl) const;

  // makes the picture available: emits imageReady at once if it is cached from the
  // same URL, else downloads it; a fetch of a key already in progress is ignored
  void fetch(const QString& gameName, int modId, const QString& pictureUrl);

signals:
  void imageReady(const QString& gameName, int modId, const QString& filePath);
  void imageFailed(const QString& gameName, int modId);

private:
  QString m_CacheDir;
  QNetworkAccessManager* m_Manager;
  QSet<QString> m_InProgress;
};

#endif  // NEXUSIMAGECACHE_H
