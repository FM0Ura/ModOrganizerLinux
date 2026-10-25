#include "pluginpaths.h"
#include "shared/appconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

QString builtInPluginsPath()
{
  return QCoreApplication::applicationDirPath() + "/" +
         QString::fromStdWString(AppConfig::pluginPath());
}

QString userPluginsPath()
{
  return QDir::fromNativeSeparators(
             QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)) +
         "/" + QString::fromStdWString(AppConfig::pluginPath());
}

QStringList pluginsPaths()
{
  return {userPluginsPath(), builtInPluginsPath()};
}

QStringList mergePluginEntries(const std::vector<QStringList>& entriesByFolder)
{
  QStringList result;
  QSet<QString> names;

  for (const QStringList& entries : entriesByFolder) {
    for (const QString& entry : entries) {
      const QString name = QFileInfo(entry).fileName();
      if (!names.contains(name)) {
        names.insert(name);
        result.append(entry);
      }
    }
  }

  return result;
}

QString pluginFilePath(const QStringList& folders, const QString& name)
{
  for (const QString& folder : folders) {
    const QString path = QDir(folder).absoluteFilePath(name);
    if (QFileInfo::exists(path)) {
      return path;
    }
  }

  return folders.isEmpty() ? name : QDir(folders.front()).absoluteFilePath(name);
}
