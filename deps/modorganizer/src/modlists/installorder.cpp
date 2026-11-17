#include "installorder.h"

#include <QCryptographicHash>

namespace modlists
{

QString fileKey(int modId, int fileId)
{
  return QStringLiteral("%1_%2").arg(modId).arg(fileId);
}

QString fileKey(const QString& archiveName)
{
  const QByteArray hash =
      QCryptographicHash::hash(archiveName.toUtf8(), QCryptographicHash::Sha1)
          .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  return QStringLiteral("archive_") + QString::fromLatin1(hash.left(12));
}

QString fileKey(int modId, int fileId, const QString& archiveName)
{
  if (modId > 0 && fileId > 0) {
    return fileKey(modId, fileId);
  }
  return fileKey(archiveName);
}

QStringList appendInstallOrder(QStringList order, const QString& key)
{
  order.removeAll(key);
  order.append(key);
  return order;
}

QString fomodChoicesKey(const QString& fileKey)
{
  return QStringLiteral("choices_") + fileKey;
}

}  // namespace modlists
