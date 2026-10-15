// plugin_load <game|installer> <plugin.so>...
//
// Loads each plugin with QPluginLoader, like PluginContainer::loadQtPlugin(), and checks
// that its instance is an IPlugin that implements the expected interface (IPluginGame
// or IPluginInstaller). Prints one line per plugin; exits with 1 if any plugin fails.

#include <QApplication>
#include <QFileInfo>
#include <QPluginLoader>
#include <QStringList>

#include <uibase/iplugin.h>
#include <uibase/iplugingame.h>
#include <uibase/iplugininstaller.h>
#include <uibase/iplugininstallercustom.h>
#include <uibase/iplugininstallersimple.h>
#include <uibase/log.h>

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

  return list;
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

  const bool ok = kind == "game" ? qobject_cast<IPluginGame*>(object) != nullptr
                                 : qobject_cast<IPluginInstaller*>(object) != nullptr;

  QString what = plugin->name();
  if (auto* game = qobject_cast<IPluginGame*>(object)) {
    what += " (" + game->gameName() + ")";
  }

  std::printf("%s %s: '%s' [%s]\n", ok ? "ok  " : "FAIL", qUtf8Printable(file),
              qUtf8Printable(what), qUtf8Printable(interfaces(object).join(", ")));
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
  if (args.size() < 3 || (args[1] != "game" && args[1] != "installer")) {
    std::fprintf(stderr, "usage: plugin_load <game|installer> <plugin.so>...\n");
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
