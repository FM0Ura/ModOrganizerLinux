#ifndef MODORGANIZER_PLUGINPATHS_H
#define MODORGANIZER_PLUGINPATHS_H

#include <QString>
#include <QStringList>

#include <vector>

// plugins are loaded from two folders: the user's, where plugins are installed, and the
// one shipped with Mod Organizer, next to the binary, read-only in the AppImage; a
// plugin of the user's folder hides the one with the same file name in the shipped
// folder, so it can also replace a shipped plugin

// <bin>/plugins
QString builtInPluginsPath();

// <global data>/plugins, ~/.local/share/ModOrganizer/plugins on Linux, beside the
// global instances (an instance is a folder with a ModOrganizer.ini, this one has none)
QString userPluginsPath();

// the user's folder first, then the shipped one
QStringList pluginsPaths();

// the paths of the entries of each folder, in the order of the folders; an entry whose
// file name an earlier folder already gave is left out
QStringList mergePluginEntries(const std::vector<QStringList>& entriesByFolder);

// <folder>/<name> in the first folder where it exists, in the first folder if none has
// it
QString pluginFilePath(const QStringList& folders, const QString& name);

#endif  // MODORGANIZER_PLUGINPATHS_H
