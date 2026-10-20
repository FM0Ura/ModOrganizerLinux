// plugin_load <game|installer|preview|tool|diagnose|filemapper|plugin> <plugin.so>...
//
// Loads each plugin with QPluginLoader, like PluginContainer::loadQtPlugin(), and checks
// that its instance is an IPlugin that implements the interface of the kind (IPluginGame,
// IPluginInstaller, IPluginPreview, IPluginTool, IPluginDiagnose or IPluginFileMapper;
// "plugin" accepts any IPlugin). Prints one line per plugin, with its interfaces and the
// keys of its settings; exits with 1 if any plugin fails, 2 on a usage error.

#include <QApplication>
#include <QFileInfo>
#include <QPluginLoader>
#include <QStringList>

#include <uibase/iplugin.h>
#include <uibase/iplugindiagnose.h>
#include <uibase/ipluginfilemapper.h>
#include <uibase/iplugingame.h>
#include <uibase/iplugininstaller.h>
#include <uibase/iplugininstallercustom.h>
#include <uibase/iplugininstallersimple.h>
#include <uibase/ipluginpreview.h>
#include <uibase/iplugintool.h>
#include <uibase/log.h>
#include <uibase/pluginsetting.h>

#include <cstdio>

using namespace MOBase;

namespace
{

QStringList interfaces(QObject* object)
{
  QStringList list;

  if (qobject_cast<IPluginGame*>(object)) {
    list.append("IPluginGame");
  }
  if (qobject_cast<IPluginInstaller*>(object)) {
    list.append("IPluginInstaller");
  }
  if (qobject_cast<IPluginInstallerSimple*>(object)) {
    list.append("IPluginInstallerSimple");
  }
  if (qobject_cast<IPluginInstallerCustom*>(object)) {
    list.append("IPluginInstallerCustom");
  }
  if (qobject_cast<IPluginPreview*>(object)) {
    list.append("IPluginPreview");
  }
  if (qobject_cast<IPluginTool*>(object)) {
    list.append("IPluginTool");
  }
  if (qobject_cast<IPluginDiagnose*>(object)) {
    list.append("IPluginDiagnose");
  }
  if (qobject_cast<IPluginFileMapper*>(object)) {
    list.append("IPluginFileMapper");
  }

  return list;
}

const QStringList Kinds = {"game",     "installer",  "preview", "tool",
                           "diagnose", "filemapper", "plugin"};

// whether the object implements the interface of the kind
bool implements(const QString& kind, QObject* object)
{
  if (kind == "game") {
    return qobject_cast<IPluginGame*>(object) != nullptr;
  }
  if (kind == "installer") {
    return qobject_cast<IPluginInstaller*>(object) != nullptr;
  }
  if (kind == "preview") {
    return qobject_cast<IPluginPreview*>(object) != nullptr;
  }
  if (kind == "tool") {
    return qobject_cast<IPluginTool*>(object) != nullptr;
  }
  if (kind == "diagnose") {
    return qobject_cast<IPluginDiagnose*>(object) != nullptr;
  }
  if (kind == "filemapper") {
    return qobject_cast<IPluginFileMapper*>(object) != nullptr;
  }
  return kind == "plugin";  // any IPlugin
}

QStringList settingKeys(const IPlugin* plugin)
{
  QStringList keys;
  for (const auto& setting : plugin->settings()) {
    keys.append(setting.key);
  }
  return keys;
}

bool check(const QString& kind, const QString& path)
{
  // an absolute path: QPluginLoader looks for relative paths in the library paths
  const QFileInfo info(path);
  const QString file = info.fileName();
  QPluginLoader loader(info.absoluteFilePath());

  QObject* object = loader.instance();
  if (object == nullptr) {
    std::printf("FAIL %s: %s\n", qUtf8Printable(file),
                qUtf8Printable(loader.errorString()));
    return false;
  }

  auto* plugin = qobject_cast<IPlugin*>(object);
  if (plugin == nullptr) {
    std::printf("FAIL %s: not an IPlugin\n", qUtf8Printable(file));
    return false;
  }

  const bool ok = implements(kind, object);

  QString what = plugin->name();
  if (auto* game = qobject_cast<IPluginGame*>(object)) {
    what += " (" + game->gameName() + ")";
  }

  std::printf("%s %s: '%s' [%s] settings: %s\n", ok ? "ok  " : "FAIL",
              qUtf8Printable(file), qUtf8Printable(what),
              qUtf8Printable(interfaces(object).join(", ")),
              qUtf8Printable(settingKeys(plugin).join(", ")));
  return ok;
}

}  // namespace

int main(int argc, char** argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }

  QApplication app(argc, argv);

  log::LoggerConfiguration conf;
  conf.name     = "plugin_load";
  conf.maxLevel = log::Debug;
  conf.pattern  = "%^[%Y-%m-%d %H:%M:%S.%e %L] %v%$";
  log::createDefault(conf);

  const QStringList args = app.arguments();
  if (args.size() < 3 || !Kinds.contains(args[1])) {
    std::fprintf(stderr, "usage: plugin_load <%s> <plugin.so>...\n",
                 qUtf8Printable(Kinds.join('|')));
    return 2;
  }

  int failed = 0;
  for (int i = 2; i < args.size(); ++i) {
    if (!check(args[1], args[i])) {
      ++failed;
    }
  }

  std::printf("%d plugin(s) loaded, %d failed\n", int(args.size() - 2 - failed), failed);
  return failed == 0 ? 0 : 1;
}
