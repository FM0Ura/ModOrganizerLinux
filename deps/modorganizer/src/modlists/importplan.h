#ifndef MODLISTS_IMPORTPLAN_H
#define MODLISTS_IMPORTPLAN_H

#include "plan.h"

#include <QString>
#include <QUrl>

#include <functional>
#include <optional>
#include <set>
#include <vector>

// Pure decisions of the import: what each row of the plan needs, the names the
// mods get, how the orders merge and which downloads can be reused.
namespace modlists
{

enum class Destination
{
  NewInstance,
  NewProfile,
  ExistingProfile
};

struct CaseInsensitive
{
  bool operator()(const QString& a, const QString& b) const
  {
    return QString::compare(a, b, Qt::CaseInsensitive) < 0;
  }
};

struct InstalledMod
{
  QString name;
  int modId = 0;
  std::vector<int> fileIds;
  QString version;
};

enum class RowStatus
{
  New,
  AlreadyInstalled,
  DifferentVersion,
  Manual,
  Bundled
};

enum class VersionChoice
{
  UseList,
  KeepMine
};

// DifferentVersion does not apply to a new instance (nothing is installed there)
// and becomes New. A separator is New: Task 8 creates it, nothing is downloaded.
RowStatus classify(const ModEntry& entry, const std::vector<InstalledMod>& installed,
                   Destination destination);

// the name of the entry if free, else "<name> [<list>]", then " (2)", " (3)"...
QString installName(const ModEntry& entry, const QString& listName,
                    const std::set<QString, CaseInsensitive>& existingNames);

struct ModOrderResult
{
  std::vector<QString> order;
  std::vector<QString> disabled;
};

ModOrderResult mergeModOrder(const std::vector<QString>& userLowToHigh,
                             const std::vector<QString>& planInstalledLowToHigh,
                             const std::vector<QString>& replacedUserMods,
                             Destination destination, bool moveReused);

struct PluginOrderResult
{
  std::vector<PluginEntry> order;
  std::vector<QString> notInstalled;
  bool userOrderChanged = false;
};

PluginOrderResult mergePluginOrder(const std::vector<PluginEntry>& user,
                                   const std::vector<PluginEntry>& plan,
                                   const std::set<QString, CaseInsensitive>& available,
                                   Destination destination);

struct DownloadCandidate
{
  QString path;
  int modId   = 0;
  int fileId  = 0;
  qint64 size = -1;
};

// md5Of is only called when a comparison needs it
std::optional<QString>
findReusableDownload(const Source& source,
                     const std::vector<DownloadCandidate>& candidates,
                     const std::function<QString(const QString& path)>& md5Of);

// True for an environment variable of an imported executable that could make the
// program load code from elsewhere (case-insensitive): LD_*, BASH_ENV, ENV,
// GCONV_PATH, PATH, PYTHON*, PERL*, *_PLUGIN_PATH, GIO_*, GTK*MODULES (GTK_MODULES,
// GTK3_MODULES...), QT_PLUGIN_PATH and XDG_*. WINE*, PROTON_*, DXVK_*, VKD3D_* and
// the rest are kept.
bool isUnsafeImportedEnv(const QString& name);

// NOTE: this URL has not been confirmed in a browser yet; it must be checked
// manually (manual checklist, Task 12): it has to open the file with the
// "Mod Manager Download" button.
QUrl nexusFilePageUrl(const QString& nexusDomain, int modId, int fileId);

}  // namespace modlists

#endif  // MODLISTS_IMPORTPLAN_H
