#include "importplan.h"

#include <QRegularExpression>

#include <algorithm>

namespace modlists
{

RowStatus classify(const ModEntry& entry, const std::vector<InstalledMod>& installed,
                   Destination destination)
{
  if (entry.isSeparator || entry.sources.empty()) {
    return RowStatus::New;
  }

  const Source& first = entry.sources.front();
  if (first.kind == SourceKind::Manual) {
    return RowStatus::Manual;
  }
  if (first.kind == SourceKind::Bundled) {
    return RowStatus::Bundled;
  }

  // Nexus rule, also for a mixed list of sources
  std::vector<int> wanted;
  for (const Source& s : entry.sources) {
    if (s.kind == SourceKind::Nexus && s.fileId != 0) {
      wanted.push_back(s.fileId);
    }
  }

  bool sameMod = false;
  for (const InstalledMod& mod : installed) {
    if (first.modId == 0 || mod.modId != first.modId) {
      continue;
    }
    sameMod        = true;
    const bool all = !wanted.empty() && std::all_of(wanted.begin(), wanted.end(), [&](int id) {
      return std::find(mod.fileIds.begin(), mod.fileIds.end(), id) != mod.fileIds.end();
    });
    if (all) {
      return RowStatus::AlreadyInstalled;
    }
  }

  if (sameMod && destination != Destination::NewInstance) {
    return RowStatus::DifferentVersion;
  }
  return RowStatus::New;
}

// the names come from an untrusted manifest and become folder names
static QString sanitizeName(const QString& name)
{
  QString s = name;
  for (QChar& c : s) {
    if (c.unicode() < 0x20 || c.unicode() == 0x7f || QString("/\\:*?\"<>|").contains(c)) {
      c = QChar('_');
    }
  }

  auto isTrim = [](QChar c) { return c == ' ' || c == '.'; };
  qsizetype b = 0, e = s.size();
  while (b < e && isTrim(s[b])) {
    ++b;
  }
  while (e > b && isTrim(s[e - 1])) {
    --e;
  }
  s = s.mid(b, e - b);
  return s.isEmpty() ? QString("Unnamed mod") : s;
}

QString installName(const ModEntry& entry, const QString& listName,
                    const std::set<QString, CaseInsensitive>& existingNames)
{
  const QString name = sanitizeName(entry.name);
  if (!existingNames.count(name)) {
    return name;
  }

  const QString base = QString("%1 [%2]").arg(name, sanitizeName(listName));
  if (!existingNames.count(base)) {
    return base;
  }
  for (int n = 2;; ++n) {
    const QString candidate = QString("%1 (%2)").arg(base).arg(n);
    if (!existingNames.count(candidate)) {
      return candidate;
    }
  }
}

ModOrderResult mergeModOrder(const std::vector<QString>& userLowToHigh,
                             const std::vector<QString>& planInstalledLowToHigh,
                             const std::vector<QString>& replacedUserMods,
                             Destination destination, bool moveReused)
{
  ModOrderResult r;
  std::set<QString, CaseInsensitive> inPlan(planInstalledLowToHigh.begin(),
                                            planInstalledLowToHigh.end());

  // a repeated plan name counts once, at its first place
  std::vector<QString> planOrder;
  {
    std::set<QString, CaseInsensitive> seen;
    for (const QString& m : planInstalledLowToHigh) {
      if (seen.insert(m).second) {
        planOrder.push_back(m);
      }
    }
  }

  if (destination != Destination::ExistingProfile) {
    r.order = planOrder;
    // the other mods stay installed, but off in this profile
    for (const QString& m : userLowToHigh) {
      if (!inPlan.count(m)) {
        r.disabled.push_back(m);
      }
    }
  } else if (moveReused) {
    for (const QString& m : userLowToHigh) {
      if (!inPlan.count(m)) {
        r.order.push_back(m);
      }
    }
    r.order.insert(r.order.end(), planOrder.begin(),
                   planOrder.end());
  } else {
    std::set<QString, CaseInsensitive> inUser(userLowToHigh.begin(),
                                              userLowToHigh.end());
    r.order = userLowToHigh;
    for (const QString& m : planOrder) {
      if (!inUser.count(m)) {
        r.order.push_back(m);
      }
    }
  }

  for (const QString& m : replacedUserMods) {
    if (std::find(r.disabled.begin(), r.disabled.end(), m) == r.disabled.end()) {
      r.disabled.push_back(m);
    }
  }
  return r;
}

PluginOrderResult mergePluginOrder(const std::vector<PluginEntry>& user,
                                   const std::vector<PluginEntry>& plan,
                                   const std::set<QString, CaseInsensitive>& available,
                                   Destination destination)
{
  PluginOrderResult r;

  if (destination != Destination::ExistingProfile) {
    for (const PluginEntry& p : plan) {
      if (available.count(p.name)) {
        r.order.push_back(p);
      } else {
        r.notInstalled.push_back(p.name);
      }
    }
    return r;
  }

  // the user's order stays; the plugins they do not have yet go after it
  r.order = user;
  std::set<QString, CaseInsensitive> have;
  for (const PluginEntry& p : user) {
    have.insert(p.name);
  }
  for (const PluginEntry& p : plan) {
    if (have.count(p.name)) {
      continue;
    }
    if (available.count(p.name)) {
      r.order.push_back(p);
    } else {
      r.notInstalled.push_back(p.name);
    }
  }
  return r;
}

std::optional<QString>
findReusableDownload(const Source& source,
                     const std::vector<DownloadCandidate>& candidates,
                     const std::function<QString(const QString& path)>& md5Of)
{
  auto md5Matches = [&](const QString& path) {
    return QString::compare(md5Of(path), source.md5, Qt::CaseInsensitive) == 0;
  };

  if (source.modId != 0 && source.fileId != 0) {
    for (const DownloadCandidate& c : candidates) {
      if (c.modId == source.modId && c.fileId == source.fileId) {
        if (source.md5.isEmpty() || md5Matches(c.path)) {
          return c.path;
        }
      }
    }
  }

  if (!source.md5.isEmpty() && source.size >= 0) {
    for (const DownloadCandidate& c : candidates) {
      if (c.size == source.size && md5Matches(c.path)) {
        return c.path;
      }
    }
  }
  return std::nullopt;
}

bool isAllowedImportedEnv(const QString& name, const QString& value)
{
  const auto anchored = [](const QString& pattern) {
    return QRegularExpression(QRegularExpression::anchoredPattern(pattern),
                              QRegularExpression::CaseInsensitiveOption);
  };
  static const QRegularExpression exact = anchored(
      "PROTON_LOG|PROTON_USE_WINED3D|PROTON_ENABLE_NVAPI|PROTON_NO_ESYNC|"
      "PROTON_NO_FSYNC|PROTON_HIDE_NVIDIA_GPU|PROTON_ENABLE_WAYLAND|MANGOHUD|"
      "WINEDEBUG|WINEESYNC|WINEFSYNC|RADV_PERFTEST|mesa_glthread|"
      "__GL_SHADER_DISK_CACHE|__GL_SHADER_DISK_CACHE_SKIP_CLEANUP|ENABLE_VKBASALT");
  static const QRegularExpression prefixed = anchored("(DXVK|VKD3D)_\\w*");
  static const QRegularExpression pathLike = anchored("\\w*_(PATH|FILE|CMD|DIR|DIRS)");
  // only dll names and load orders, never a path
  static const QRegularExpression overrides(QRegularExpression::anchoredPattern(
      "[A-Za-z0-9_.*-]+(,[A-Za-z0-9_.*-]+)*=[nbd]?(,[nbd])*"
      "(;[A-Za-z0-9_.*-]+(,[A-Za-z0-9_.*-]+)*=[nbd]?(,[nbd])*)*;?"));

  if (name.isEmpty()) {
    return false;
  }
  // no value of an allowed name holds a path, a marker or another line
  for (const QChar c : value) {
    if (c.category() == QChar::Other_Control || c == '/' || c == '\\' || c == '%' ||
        c == ':') {
      return false;
    }
  }
  if (name.compare("WINEDLLOVERRIDES", Qt::CaseInsensitive) == 0) {
    return overrides.match(value).hasMatch();
  }
  if (exact.match(name).hasMatch()) {
    return true;
  }
  return prefixed.match(name).hasMatch() && !pathLike.match(name).hasMatch();
}

QUrl nexusFilePageUrl(const QString& nexusDomain, int modId, int fileId)
{
  return QUrl(QString("https://www.nexusmods.com/%1/mods/%2?tab=files&file_id=%3")
                  .arg(nexusDomain)
                  .arg(modId)
                  .arg(fileId));
}

}  // namespace modlists
