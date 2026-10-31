#include "plan.h"

namespace modlists
{

namespace
{

  [[noreturn]] void invalid(const QString& message)
  {
    throw PlanError(PlanError::Kind::Invalid, message);
  }

  QString kindName(SourceKind k)
  {
    switch (k) {
    case SourceKind::Nexus:
      return "nexus";
    case SourceKind::Manual:
      return "manual";
    case SourceKind::Bundled:
      return "bundled";
    }
    return {};
  }

  void putString(QJsonObject& o, const char* key, const QString& v)
  {
    if (!v.isEmpty()) {
      o[key] = v;
    }
  }

  QString requireString(const QJsonObject& o, const char* key, const QString& where)
  {
    const auto v = o.value(key);
    if (!v.isString() || v.toString().isEmpty()) {
      invalid(QString("%1: missing \"%2\"").arg(where, key));
    }
    return v.toString();
  }

  QString optString(const QJsonObject& o, const char* key)
  {
    return o.value(key).toString();
  }

  bool optBool(const QJsonObject& o, const char* key, bool def)
  {
    const auto v = o.value(key);
    return v.isBool() ? v.toBool() : def;
  }

  int optInt(const QJsonObject& o, const char* key, int def)
  {
    const auto v = o.value(key);
    return v.isDouble() ? v.toInt(def) : def;
  }

  QStringList optStrings(const QJsonObject& o, const char* key)
  {
    QStringList out;
    for (const auto& v : o.value(key).toArray()) {
      out.append(v.toString());
    }
    return out;
  }

  QJsonArray fromStrings(const QStringList& l)
  {
    QJsonArray a;
    for (const auto& s : l) {
      a.append(s);
    }
    return a;
  }

  QJsonObject toJson(const Source& s)
  {
    QJsonObject o;
    o["type"] = kindName(s.kind);
    if (s.modId != 0)
      o["modId"] = s.modId;
    if (s.fileId != 0)
      o["fileId"] = s.fileId;
    putString(o, "md5", s.md5);
    if (s.size >= 0)
      o["size"] = static_cast<double>(s.size);
    putString(o, "url", s.url);
    putString(o, "fileName", s.fileName);
    putString(o, "bundledDir", s.bundledDir);
    if (s.fomodChoices)
      o["fomodChoices"] = *s.fomodChoices;
    return o;
  }

  Source sourceFromJson(const QJsonObject& o, const QString& where)
  {
    Source s;
    const QString type = requireString(o, "type", where);
    if (type == "nexus") {
      s.kind = SourceKind::Nexus;
    } else if (type == "manual") {
      s.kind = SourceKind::Manual;
    } else if (type == "bundled") {
      s.kind = SourceKind::Bundled;
    } else {
      throw PlanError(PlanError::Kind::UnknownSource,
                      QString("%1: unknown source type \"%2\"").arg(where, type));
    }
    s.modId      = optInt(o, "modId", 0);
    s.fileId     = optInt(o, "fileId", 0);
    s.md5        = optString(o, "md5");
    s.size       = o.contains("size") ? static_cast<qint64>(o["size"].toDouble(-1)) : -1;
    s.url        = optString(o, "url");
    s.fileName   = optString(o, "fileName");
    s.bundledDir = optString(o, "bundledDir");
    if (o.value("fomodChoices").isArray()) {
      s.fomodChoices = o["fomodChoices"].toArray();
    }
    return s;
  }

  QJsonObject toJson(const ModEntry& m)
  {
    QJsonObject o;
    o["name"] = m.name;
    putString(o, "version", m.version);
    if (m.isSeparator)
      o["separator"] = true;
    if (m.optional)
      o["optional"] = true;
    if (!m.enabled)
      o["enabled"] = false;
    if (m.phase != 0)
      o["phase"] = m.phase;
    putString(o, "vortexModType", m.vortexModType);
    QJsonArray sources;
    for (const auto& s : m.sources) {
      sources.append(toJson(s));
    }
    o["sources"] = sources;
    return o;
  }

  ModEntry modFromJson(const QJsonObject& o, int index)
  {
    const QString where = QString("mods[%1]").arg(index);
    ModEntry m;
    m.name          = requireString(o, "name", where);
    m.version       = optString(o, "version");
    m.isSeparator   = optBool(o, "separator", false);
    m.optional      = optBool(o, "optional", false);
    m.enabled       = optBool(o, "enabled", true);
    m.phase         = optInt(o, "phase", 0);
    m.vortexModType = optString(o, "vortexModType");
    int i           = 0;
    for (const auto& v : o.value("sources").toArray()) {
      m.sources.push_back(
          sourceFromJson(v.toObject(), QString("%1.sources[%2]").arg(where).arg(i++)));
    }
    return m;
  }

}  // namespace

QJsonObject toJson(const Plan& plan)
{
  QJsonObject o;
  const auto& h = plan.header;
  o["schemaVersion"] = h.schemaVersion;
  o["name"]          = h.name;
  putString(o, "author", h.author);
  putString(o, "version", h.version);
  putString(o, "game", h.gameName);
  putString(o, "nexusDomain", h.nexusDomain);
  o["origin"] = h.origin;
  putString(o, "collectionSlug", h.collectionSlug);
  if (h.collectionRevision != 0)
    o["collectionRevision"] = h.collectionRevision;

  QJsonArray mods;
  for (const auto& m : plan.mods) {
    mods.append(toJson(m));
  }
  o["mods"] = mods;

  QJsonArray rules;
  for (const auto& r : plan.rules) {
    rules.append(QJsonObject{{"mod", r.mod}, {"type", r.type}, {"reference", r.reference}});
  }
  o["rules"] = rules;

  QJsonObject plugins;
  plugins["useLoot"] = plan.pluginsUseLoot;
  QJsonArray list;
  for (const auto& p : plan.plugins) {
    list.append(QJsonObject{{"name", p.name}, {"enabled", p.enabled}});
  }
  plugins["list"] = list;
  o["plugins"]    = plugins;

  o["profile"] = QJsonObject{{"localInis", plan.profile.localInis},
                             {"localSaves", plan.profile.localSaves},
                             {"iniFiles", fromStrings(plan.profile.iniFiles)}};

  QJsonArray exes;
  for (const auto& e : plan.executables) {
    QJsonObject eo;
    eo["title"]  = e.title;
    eo["binary"] = e.binary;
    putString(eo, "arguments", e.arguments);
    putString(eo, "workingDirectory", e.workingDirectory);
    putString(eo, "steamAppId", e.steamAppId);
    putString(eo, "proton", e.proton);
    if (!e.environment.isEmpty())
      eo["environment"] = fromStrings(e.environment);
    if (e.showOnToolbar)
      eo["showOnToolbar"] = true;
    exes.append(eo);
  }
  o["executables"] = exes;
  return o;
}

Plan fromJson(const QJsonObject& o)
{
  Plan plan;
  auto& h = plan.header;

  if (!o.value("schemaVersion").isDouble()) {
    invalid("missing \"schemaVersion\"");
  }
  h.schemaVersion = o["schemaVersion"].toInt();
  if (h.schemaVersion > SchemaVersion) {
    throw PlanError(PlanError::Kind::NewerSchema,
                    "This modlist was made by a newer Mod Organizer. Update Mod "
                    "Organizer to import it.");
  }
  if (h.schemaVersion < 1) {
    invalid("invalid \"schemaVersion\"");
  }

  h.name               = requireString(o, "name", "manifest");
  h.author             = optString(o, "author");
  h.version            = optString(o, "version");
  h.gameName           = optString(o, "game");
  h.nexusDomain        = optString(o, "nexusDomain");
  h.origin             = o.contains("origin") ? optString(o, "origin") : "mo2list";
  h.collectionSlug     = optString(o, "collectionSlug");
  h.collectionRevision = optInt(o, "collectionRevision", 0);

  int i = 0;
  for (const auto& v : o.value("mods").toArray()) {
    plan.mods.push_back(modFromJson(v.toObject(), i++));
  }

  i = 0;
  for (const auto& v : o.value("rules").toArray()) {
    const auto r        = v.toObject();
    const QString where = QString("rules[%1]").arg(i++);
    OrderRule rule;
    rule.mod       = requireString(r, "mod", where);
    rule.type      = requireString(r, "type", where);
    rule.reference = requireString(r, "reference", where);
    if (rule.type != "before" && rule.type != "after") {
      invalid(QString("%1: type must be \"before\" or \"after\"").arg(where));
    }
    plan.rules.push_back(rule);
  }

  const auto plugins = o.value("plugins").toObject();
  plan.pluginsUseLoot = optBool(plugins, "useLoot", false);
  i                   = 0;
  for (const auto& v : plugins.value("list").toArray()) {
    const auto p = v.toObject();
    PluginEntry e;
    e.name    = requireString(p, "name", QString("plugins[%1]").arg(i++));
    e.enabled = optBool(p, "enabled", true);
    plan.plugins.push_back(e);
  }

  const auto profile        = o.value("profile").toObject();
  plan.profile.localInis    = optBool(profile, "localInis", true);
  plan.profile.localSaves   = optBool(profile, "localSaves", true);
  plan.profile.iniFiles     = optStrings(profile, "iniFiles");

  i = 0;
  for (const auto& v : o.value("executables").toArray()) {
    const auto x        = v.toObject();
    const QString where = QString("executables[%1]").arg(i++);
    ExecutableEntry e;
    e.title            = requireString(x, "title", where);
    e.binary           = requireString(x, "binary", where);
    e.arguments        = optString(x, "arguments");
    e.workingDirectory = optString(x, "workingDirectory");
    e.steamAppId       = optString(x, "steamAppId");
    e.proton           = optString(x, "proton");
    e.environment      = optStrings(x, "environment");
    e.showOnToolbar    = optBool(x, "showOnToolbar", false);
    plan.executables.push_back(e);
  }
  return plan;
}

}  // namespace modlists
