#include "modlistinstaller.h"

#include "executableslist.h"
#include "exportcollector.h"
#include "loot.h"
#include "modinfo.h"
#include "organizercore.h"
#include "pluginlist.h"
#include "profile.h"
#include "settings.h"

#include <uibase/iplugingame.h>
#include <uibase/log.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <climits>
#include <tuple>

// Steps 6 to 9 of the import, after the batch: the mod order, the plugins, the INIs of
// the profile and the executables. The questions here come after the batch, never
// during it.

using namespace MOBase;

namespace modlists
{

namespace
{

  // same encoding trick as the export (exportbuilder.cpp): the INIs are bytes in an
  // unknown encoding, read as Latin-1 they come back unchanged
  QByteArray restoredIni(const QByteArray& data, const PathMarkers& markers)
  {
    const auto bytes = [](const QString& s) {
      return QString::fromLatin1(s.toUtf8());
    };
    const PathMarkers latin1{bytes(markers.instance), bytes(markers.game),
                             bytes(markers.protonPrefix), bytes(markers.home)};
    return fromMarkers(QString::fromLatin1(data), latin1).toLatin1();
  }

  // the INIs of the game that a profile holds (Skyrim.ini, SkyrimPrefs.ini...), as
  // plain file names; the manifest is untrusted, so nothing else is ever written
  // into the profile (settings.ini, modlist.txt, plugins.txt...)
  std::set<QString, CaseInsensitive> gameIniNames(const IPluginGame* game)
  {
    std::set<QString, CaseInsensitive> names;
    if (game != nullptr) {
      for (const auto& ini : game->iniFiles()) {
        const QString name = QFileInfo(ini).fileName();
        if (!name.isEmpty()) {
          names.insert(name);
        }
      }
    }
    return names;
  }

  bool plainName(const QString& name)
  {
    return !name.isEmpty() && !name.contains('/') && !name.contains('\\') &&
           name != "." && name != "..";
  }

  bool writeFile(const QString& path, const QByteArray& data)
  {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() &&
           file.commit();
  }

}  // namespace

void ModlistInstaller::whenIdle(std::function<void()> f)
{
  QPointer<ModlistInstaller> self(this);
  m_core.onNextRefresh(
      [self, f] {
        // after the refresh that is running is over, lists included
        if (self) {
          QTimer::singleShot(0, self, f);
        }
      },
      OrganizerCore::RefreshCallbackGroup::INTERNAL,
      OrganizerCore::RefreshCallbackMode::RUN_NOW_IF_POSSIBLE);
}

void ModlistInstaller::refreshThen(std::function<void()> f)
{
  m_core.refresh();

  QPointer<ModlistInstaller> self(this);
  m_core.onNextRefresh(
      [self, f] {
        // the callbacks run before refreshLists(): wait for the plugin list too
        if (self) {
          QTimer::singleShot(0, self, f);
        }
      },
      OrganizerCore::RefreshCallbackGroup::INTERNAL,
      OrganizerCore::RefreshCallbackMode::FORCE_WAIT_FOR_REFRESH);
}

void ModlistInstaller::applyModOrder()
{
  auto profile = m_core.currentProfile();
  if (profile == nullptr) {
    return;
  }
  const Destination destination = m_pending.destination;

  try {
    // the plan's mods as installed or reused, low to high
    std::vector<QString> planOrder;
    std::map<QString, const ModEntry*, CaseInsensitive> entryOf;
    for (const auto& [e, name] : m_modOfEntry) {
      planOrder.push_back(name);
      entryOf.emplace(name, &plan().mods[e]);
    }

    // the user's mods by priority; the foreign ones (DLCs, unmanaged) stay at the
    // bottom of a new profile
    std::vector<std::pair<int, QString>> byPriority;
    std::vector<QString> foreign;
    for (unsigned int i = 0; i < ModInfo::getNumMods() && i < profile->numMods(); ++i) {
      const auto mod = ModInfo::getByIndex(i);
      const int p    = profile->getModPriority(i);
      if (!mod->hasAutomaticPriority() && p >= 0) {
        byPriority.emplace_back(p, mod->name());
      }
    }
    std::sort(byPriority.begin(), byPriority.end());

    std::vector<QString> user;
    for (const auto& [p, name] : byPriority) {
      const auto mod = ModInfo::getByIndex(ModInfo::getIndex(name));
      if (destination != Destination::ExistingProfile && mod->isForeign()) {
        foreign.push_back(name);
      } else {
        user.push_back(name);
      }
    }

    bool moveReused = false;
    if (destination == Destination::ExistingProfile) {
      // the reused mods are out of place when their order differs between the two
      const auto keep = mergeModOrder(user, planOrder, m_replacedUserMods,
                                      destination, false);
      const auto move =
          mergeModOrder(user, planOrder, m_replacedUserMods, destination, true);
      const auto onlyPlan = [&](const std::vector<QString>& order) {
        std::vector<QString> out;
        for (const auto& m : order) {
          if (entryOf.count(m)) {
            out.push_back(m);
          }
        }
        return out;
      };
      const auto a = onlyPlan(keep.order), b = onlyPlan(move.order);
      int outOfPlace = 0;
      for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (a[i].compare(b[i], Qt::CaseInsensitive) != 0 && !m_ownMods.count(a[i])) {
          ++outOfPlace;
        }
      }
      if (outOfPlace > 0) {
        moveReused =
            QMessageBox::question(
                m_parent, tr("Mod order"),
                tr("Move %1 reused mods to the list's order?").arg(outOfPlace),
                QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
      }
    }

    const auto result =
        mergeModOrder(user, planOrder, m_replacedUserMods, destination, moveReused);

    std::vector<QString> order = foreign;
    std::set<QString, CaseInsensitive> placed(foreign.begin(), foreign.end());
    for (const auto& m : result.order) {
      if (placed.insert(m).second) {
        order.push_back(m);
      }
    }
    for (const auto& m : user) {
      if (placed.insert(m).second) {
        order.push_back(m);
      }
    }

    int priority = 0;
    for (const auto& name : order) {
      const unsigned int index = ModInfo::getIndex(name);
      if (index == UINT_MAX) {
        continue;
      }
      int p = priority++;
      profile->setModPriority(index, p);
    }

    for (const auto& [name, entry] : entryOf) {
      const unsigned int index = ModInfo::getIndex(name);
      if (index != UINT_MAX && !entry->isSeparator) {
        profile->setModEnabled(index, entry->enabled);
      }
    }
    for (const auto& name : result.disabled) {
      const unsigned int index = ModInfo::getIndex(name);
      if (index != UINT_MAX) {
        profile->setModEnabled(index, false);
      }
    }

    profile->writeModlistNow();
  } catch (const std::exception& e) {
    log::error("modlist import: cannot apply the mod order: {}", e.what());
    m_summary.rows.push_back(
        failedRow(tr("Mod order"), QString::fromUtf8(e.what())));
  }
}

void ModlistInstaller::applyPlugins()
{
  PluginList* list = m_core.pluginList();
  try {
    QStringList names = list->pluginNames();
    std::sort(names.begin(), names.end(), [&](const QString& a, const QString& b) {
      return list->priority(a) < list->priority(b);
    });

    std::vector<PluginEntry> user;
    std::set<QString, CaseInsensitive> available;
    for (const auto& name : names) {
      user.push_back({name, list->isEnabled(name)});
      available.insert(name);
    }

    const auto result =
        mergePluginOrder(user, plan().plugins, available, m_pending.destination);

    QStringList order;
    for (const auto& p : result.order) {
      order.append(p.name);
    }
    list->setLoadOrder(order);
    for (const auto& p : result.order) {
      list->setState(p.name, p.enabled ? IPluginList::STATE_ACTIVE
                                       : IPluginList::STATE_INACTIVE);
    }

    m_summary.pluginsNotInstalled    = result.notInstalled;
    m_summary.userPluginOrderChanged = result.userOrderChanged;

    m_summary.missingMasters.clear();
    for (const auto& p : result.order) {
      if (!list->isEnabled(p.name)) {
        continue;
      }
      QStringList missing;
      for (const auto& master : list->masters(p.name)) {
        if (!available.count(master)) {
          missing.append(master);
        }
      }
      if (!missing.isEmpty()) {
        m_summary.missingMasters.emplace_back(p.name, missing);
      }
    }

    // written, then read back so that the plugin list shows it
    m_core.savePluginList();
    m_core.refreshESPList(false);
  } catch (const std::exception& e) {
    log::error("modlist import: cannot apply the plugins: {}", e.what());
    m_summary.rows.push_back(failedRow(tr("Plugins"), QString::fromUtf8(e.what())));
    return;
  }

  if (plan().pluginsUseLoot) {
    const bool offline = m_core.settings().network().offlineMode();
    if (runLoot(m_parent, m_core, offline)) {
      m_core.refreshESPList(false);
      m_core.savePluginList();
    }
  }
  m_summary.offerLoot = plan().pluginsUseLoot || m_summary.userPluginOrderChanged;
}

void ModlistInstaller::applyInis()
{
  auto profile = m_core.currentProfile();
  if (profile == nullptr) {
    return;
  }
  const bool fresh = m_pending.destination != Destination::ExistingProfile;

  // a new instance or profile has local INIs and saves, whatever the list says
  if (fresh) {
    profile->enableLocalSettings(true);
    profile->enableLocalSaves(true);
  }

  const auto markers = ExportCollector(m_core, *profile).markers();
  const QDir dir(profile->absolutePath());
  const auto present = dir.entryList(QDir::Files | QDir::Hidden);

  // (name, path in the profile, content) of the INIs that change
  std::vector<std::tuple<QString, QString, QByteArray>> changes;
  const auto allowed = gameIniNames(m_core.managedGame());
  for (const auto& name : plan().profile.iniFiles) {
    if (!plainName(name) || !allowed.count(name)) {
      m_summary.inisNotApplied.push_back(
          tr("%1 (not an INI file of the game)").arg(name));
      continue;
    }
    const QByteArray data = m_package.profileFile(name);
    if (data.isEmpty()) {
      m_summary.inisNotApplied.push_back(tr("%1 (missing from the package)").arg(name));
      continue;
    }

    // the file of the profile may have another case
    QString path = dir.filePath(name);
    for (const auto& p : present) {
      if (p.compare(name, Qt::CaseInsensitive) == 0) {
        path = dir.filePath(p);
        break;
      }
    }

    const QByteArray content = restoredIni(data, markers);
    QFile current(path);
    if (current.open(QIODevice::ReadOnly) && current.readAll() == content) {
      continue;
    }
    changes.emplace_back(name, path, content);
  }

  if (changes.empty()) {
    return;
  }

  bool apply = fresh;
  if (!fresh) {
    QStringList files;
    for (const auto& [name, path, content] : changes) {
      files.append(name);
    }
    if (!profile->localSettingsEnabled()) {
      // the profile uses the global INIs; turning local INIs on is the user's call
      apply = false;
    } else {
      apply = QMessageBox::question(
                  m_parent, tr("INI files"),
                  tr("The list has other settings in these INI files of the profile "
                     "\"%1\":\n\n%2\n\nReplace them with the list's?")
                      .arg(profile->name(), files.join("\n")),
                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
    }
  }

  for (const auto& [name, path, content] : changes) {
    if (!apply) {
      m_summary.inisNotApplied.push_back(name);
    } else if (!writeFile(path, content)) {
      log::error("modlist import: cannot write {}", path);
      m_summary.inisNotApplied.push_back(name);
    }
  }
}

void ModlistInstaller::applyExecutables()
{
  auto profile = m_core.currentProfile();
  if (profile == nullptr || plan().executables.empty()) {
    return;
  }

  const auto markers    = ExportCollector(m_core, *profile).markers();
  ExecutablesList* list = m_core.executablesList();
  const auto restore    = [&](const QString& s) {
    return fromMarkers(s, markers);
  };

  // the program of an imported executable is in the game, the instance, or the mods
  // and overwrite folders of the instance (which can be set outside of it)
  QStringList roots;
  const auto& paths = m_core.settings().paths();
  for (const auto& dir :
       {markers.game, markers.instance, paths.mods(), paths.overwrite()}) {
    const QString canonical = QDir(dir).canonicalPath();
    if (!dir.isEmpty() && !canonical.isEmpty()) {
      roots.append(canonical);
    }
  }
  const auto inside = [&](const QString& path) {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return !canonical.isEmpty() &&
           std::any_of(roots.begin(), roots.end(), [&](const QString& root) {
             return canonical.startsWith(root + "/");
           });
  };

  for (const auto& exe : plan().executables) {
    const QString binary = restore(exe.binary);
    if (exe.title.isEmpty() || binary.isEmpty() || !QFileInfo::exists(binary)) {
      m_summary.executablesWithoutTarget.push_back(exe.title);
      continue;
    }
    if (!inside(binary)) {
      m_summary.executablesWithoutTarget.push_back(
          tr("%1 (outside the game and instance folders)").arg(exe.title));
      continue;
    }

    // the variables that make a program load code from elsewhere are left out
    QStringList environment, dropped;
    for (const auto& variable : exe.environment) {
      const QString name = variable.section('=', 0, 0).trimmed();
      if (isUnsafeImportedEnv(name)) {
        dropped.append(name);
      } else {
        environment.append(restore(variable));
      }
    }

    // never merged into an executable of the user: a taken title gets the list's name
    QString title = exe.title;
    if (list->titleExists(title)) {
      const QString base = QString("%1 [%2]").arg(exe.title, plan().header.name);
      title              = base;
      for (int n = 2; list->titleExists(title); ++n) {
        title = QString("%1 (%2)").arg(base).arg(n);
      }
    }

    const QString arguments = restore(exe.arguments);
    Executable e(title);
    e.binaryInfo(QFileInfo(binary))
        .arguments(arguments)
        .workingDirectory(restore(exe.workingDirectory))
        .steamAppID(exe.steamAppId)
        .proton(exe.proton)
        .environment(environment)
        .flags(exe.showOnToolbar ? Executable::Flags(Executable::ShowInToolbar)
                                 : Executable::Flags());
    list->setExecutable(e);

    QString line = QString("%1: %2").arg(
        title, arguments.isEmpty() ? binary : binary + " " + arguments);
    if (!dropped.isEmpty()) {
      line += tr(" (left out: %1)").arg(dropped.join(", "));
    }
    m_summary.executablesAdded.push_back(line);
  }

  list->store(m_core.settings());
}

}  // namespace modlists
