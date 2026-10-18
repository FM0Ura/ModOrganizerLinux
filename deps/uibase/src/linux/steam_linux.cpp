// Steam installations, games, Proton prefixes and Proton versions
// (include/uibase/linux/steam.h), from the files of the Steam client.

#include <uibase/linux/steam.h>
#include <uibase/linux/vdf.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace MOBase::steam
{
namespace
{
  bool isAppId(const QString& s)
  {
    static const QRegularExpression re("^[0-9]+$");
    return re.match(s).hasMatch();
  }

  // "Proton - Experimental" -> proton_experimental, "Proton 9.0" -> proton_9,
  // "Proton 5.13" -> proton_513, "Proton Hotfix" -> proton_hotfix (Steam's names)
  QString officialInternalName(const QString& folder)
  {
    if (folder == "Proton - Experimental") {
      return "proton_experimental";
    }
    static const QRegularExpression version(R"(^Proton (\d+)\.(\d+)$)");
    if (const auto m = version.match(folder); m.hasMatch()) {
      return "proton_" + m.captured(1) + (m.captured(2) == "0" ? "" : m.captured(2));
    }
    QString name = folder.toLower();
    name.replace(QRegularExpression("[^a-z0-9]+"), "_");
    return name;
  }

  void addUnique(std::vector<Proton>& out, Proton p)
  {
    const auto canonical = QFileInfo(p.path).canonicalFilePath();
    for (const auto& q : out) {
      if (QFileInfo(q.path).canonicalFilePath() == canonical) {
        return;
      }
    }
    out.push_back(std::move(p));
  }
}  // namespace

QStringList roots()
{
  static const char* const candidates[] = {
      ".local/share/Steam",
      ".steam/steam",
      ".var/app/com.valvesoftware.Steam/.local/share/Steam",
  };

  QStringList result, canonical;
  const QDir home(QDir::homePath());

  for (const char* candidate : candidates) {
    const QString path = QDir::cleanPath(home.absoluteFilePath(candidate));
    const QString real = QFileInfo(path).canonicalFilePath();
    if (QFileInfo(path).isDir() && !canonical.contains(real)) {
      result.append(path);
      canonical.append(real);
    }
  }

  return result;
}

QStringList libraries(const QString& root)
{
  QStringList result{root}, canonical{QFileInfo(root).canonicalFilePath()};

  const auto file = vdf::parseFile(root + "/steamapps/libraryfolders.vdf");
  if (!file) {
    return result;
  }

  for (const auto& top : file->children) {
    for (const auto& entry : top.children) {
      bool numeric = false;
      entry.key.toUInt(&numeric);
      const QString path = QDir::cleanPath(entry.block ? entry.value(u"path")
                                           : numeric   ? entry.text
                                                       : QString());
      const QString real = QFileInfo(path).canonicalFilePath();
      if (!path.isEmpty() && QDir::isAbsolutePath(path) && QFileInfo(path).isDir() &&
          !canonical.contains(real)) {
        result.append(path);
        canonical.append(real);
      }
    }
  }

  return result;
}

std::optional<App> findApp(const QString& appId)
{
  if (!isAppId(appId)) {
    return {};
  }

  for (const auto& root : roots()) {
    for (const auto& library : libraries(root)) {
      const auto manifest =
          vdf::parseFile(library + "/steamapps/appmanifest_" + appId + ".acf");
      if (!manifest) {
        continue;
      }

      const auto installDir = manifest->path({u"AppState"})
                                  ? manifest->path({u"AppState"})->value(u"installdir")
                                  : QString();
      const auto gamePath   = library + "/steamapps/common/" + installDir;
      if (installDir.isEmpty() || installDir.contains('/') ||
          !QFileInfo(gamePath).isDir()) {
        continue;
      }

      App app{appId, root, library, gamePath, {}};
      for (const auto& base : {library, root}) {
        const auto compat = base + "/steamapps/compatdata/" + appId;
        if (QFileInfo(compat + "/pfx").isDir()) {
          app.compatData = compat;
          break;
        }
      }
      return app;
    }
  }

  return {};
}

std::vector<Proton> installedProtons()
{
  std::vector<Proton> result;

  for (const auto& root : roots()) {
    for (const auto& library : libraries(root)) {
      QDir common(library + "/steamapps/common");
      for (const auto& name : common.entryList(
               {"Proton*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const auto path = common.absoluteFilePath(name);
        if (QFileInfo(path + "/proton").isFile()) {
          addUnique(result, {name, officialInternalName(name), path});
        }
      }
    }
  }

  for (const auto& root : roots()) {
    QDir tools(root + "/compatibilitytools.d");
    for (const auto& dir :
         tools.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
      const auto manifestDir = tools.absoluteFilePath(dir);
      const auto manifest    = vdf::parseFile(manifestDir + "/compatibilitytool.vdf");
      const auto* compat =
          manifest ? manifest->path({u"compatibilitytools", u"compat_tools"}) : nullptr;
      if (!compat) {
        continue;
      }
      for (const auto& tool : compat->children) {
        if (!tool.block) {
          continue;
        }
        const auto install = tool.value(u"install_path");
        const auto path    = QDir::cleanPath(
            QDir(manifestDir)
                .absoluteFilePath(install.isEmpty() ? QStringLiteral(".") : install));
        if (QFileInfo(path + "/proton").isFile()) {
          const auto display = tool.value(u"display_name");
          addUnique(result, {display.isEmpty() ? tool.key : display, tool.key, path});
        }
      }
    }
  }

  return result;
}

QString compatToolName(const QString& appId)
{
  QString fallback;

  for (const auto& root : roots()) {
    const auto config   = vdf::parseFile(root + "/config/config.vdf");
    const auto* mapping = config
                              ? config->path({u"InstallConfigStore", u"Software",
                                              u"Valve", u"Steam", u"CompatToolMapping"})
                              : nullptr;
    if (!mapping) {
      continue;
    }
    if (const auto* app = mapping->child(appId);
        app && !app->value(u"name").isEmpty()) {
      return app->value(u"name");
    }
    if (const auto* def = mapping->child(u"0"); def && fallback.isEmpty()) {
      fallback = def->value(u"name");
    }
  }

  return fallback;
}

std::optional<Proton> findProton(const std::vector<Proton>& protons,
                                 const QString& name)
{
  for (const auto& p : protons) {
    if (p.internalName.compare(name, Qt::CaseInsensitive) == 0 ||
        p.displayName.compare(name, Qt::CaseInsensitive) == 0) {
      return p;
    }
  }
  return {};
}

std::optional<Proton> protonForApp(const App& app)
{
  const auto protons = installedProtons();

  if (const auto name = compatToolName(app.appId); !name.isEmpty()) {
    return findProton(protons, name);
  }

  QFile versionFile(app.compatData + "/version");
  if (app.compatData.isEmpty() || !versionFile.open(QIODevice::ReadOnly)) {
    return {};
  }
  const auto version = versionFile.read(256).trimmed();
  if (version.isEmpty()) {
    return {};
  }
  const QByteArray needle = "CURRENT_PREFIX_VERSION=\"" + version + "\"";

  for (const auto& p : protons) {
    QFile script(p.path + "/proton");
    if (script.open(QIODevice::ReadOnly) &&
        script.read(4 * 1024 * 1024).contains(needle)) {
      return p;
    }
  }

  return {};
}
}  // namespace MOBase::steam
