#pragma once

#include <QString>
#include <QStringList>

namespace modlists
{

// Key of one downloaded file of a mod: "<modId>_<fileId>". Both ids must be known
// (> 0); otherwise use the overload on the archive name.
QString fileKey(int modId, int fileId);

// Key of a file without Nexus ids: "archive_" + the first 12 characters of the
// base64url SHA-1 of the archive file name (not the path).
QString fileKey(const QString& archiveName);

// fileKey(modId, fileId) when both ids are known, else fileKey(archiveName).
QString fileKey(int modId, int fileId, const QString& archiveName);

// `order` without `key`, then `key` at the end.
QStringList appendInstallOrder(QStringList order, const QString& key);

// Name of the setting that holds the recorded FOMOD choices of a file:
// "choices_" + fileKey. The FOMOD installer repeats this literal.
QString fomodChoicesKey(const QString& fileKey);

}  // namespace modlists
