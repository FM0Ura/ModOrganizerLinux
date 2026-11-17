#ifndef MODLISTS_PLAN_H
#define MODLISTS_PLAN_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <stdexcept>
#include <vector>

// The model of a modlist: what a profile is made of, independent of where it
// comes from (a .mo2list, a Nexus Collection) and of the JSON that stores it.
namespace modlists
{

inline constexpr int SchemaVersion = 1;

enum class SourceKind
{
  Nexus,
  Manual,
  Bundled
};

// where the files of a mod come from; "type" in the JSON
struct Source
{
  SourceKind kind = SourceKind::Nexus;
  int modId       = 0;
  int fileId      = 0;
  QString md5;
  qint64 size = -1;
  QString url;
  QString fileName;
  // the folder inside bundled/ of the package
  QString bundledDir;
  // the choices of the FOMOD installer, see FomodChoices
  std::optional<QJsonArray> fomodChoices;
};

struct ModEntry
{
  QString name;
  QString version;
  bool isSeparator = false;
  bool optional    = false;
  bool enabled     = true;
  std::vector<Source> sources;
  int phase = 0;
  QString vortexModType;
};

// "before" or "after"; not used by the import of part 1, but kept on round trips
struct OrderRule
{
  QString mod;
  QString type;
  QString reference;
};

struct PluginEntry
{
  QString name;
  bool enabled = true;
};

// the paths hold the markers (%INSTANCE%, %GAME%, ...)
struct ExecutableEntry
{
  QString title, binary, arguments, workingDirectory, steamAppId, proton;
  QStringList environment;
  bool showOnToolbar = false;
};

struct ProfileConfig
{
  bool localInis  = true;
  bool localSaves = true;
  // the names inside profile/
  QStringList iniFiles;
};

struct Header
{
  int schemaVersion = SchemaVersion;
  QString name, author, version, gameName, nexusDomain;
  QString origin = "mo2list";
  QString collectionSlug;
  int collectionRevision = 0;
};

struct Plan
{
  Header header;
  // from the lowest priority to the highest, the separators in their places
  std::vector<ModEntry> mods;
  std::vector<OrderRule> rules;
  bool pluginsUseLoot = false;
  std::vector<PluginEntry> plugins;
  ProfileConfig profile;
  std::vector<ExecutableEntry> executables;
};

class PlanError : public std::runtime_error
{
public:
  enum class Kind
  {
    Invalid,
    NewerSchema,
    UnknownSource
  };

  PlanError(Kind kind, const QString& message)
      : std::runtime_error(message.toUtf8().constData()), m_kind(kind)
  {}

  Kind kind() const { return m_kind; }

private:
  Kind m_kind;
};

QJsonObject toJson(const Plan& plan);

// throws PlanError
Plan fromJson(const QJsonObject& json);

}  // namespace modlists

#endif  // MODLISTS_PLAN_H
