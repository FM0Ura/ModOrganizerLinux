#ifndef SELFUPDATE_LINUX_H
#define SELFUPDATE_LINUX_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

// the update of Mod Organizer installed as an AppImage, from the releases of the
// GitHub repository of the port (spec of Phase 5, section 3.4): the choice of the
// assets of a release, the check of the downloaded file against the SHA256SUMS of the
// release, the atomic replacement of the AppImage and the restart into the new one.
// SelfUpdater (selfupdater.cpp) does the network part and the dialogs.
namespace selfupdate
{
// what can be done with an update, for Mod Organizer running from `appImage` (the
// path of $APPIMAGE, empty outside an AppImage)
enum class UpdateMode
{
  // not an AppImage: no check for updates
  None,
  // download the new AppImage and replace this one
  Install,
  // the folder of the AppImage is not writable: only tell about a new version and offer
  // the page of the release, nothing is downloaded nor replaced
  NotifyOnly
};

UpdateMode updateMode(const QString& appImage);

struct Asset
{
  QString name;
  // browser_download_url
  QUrl url;
};

// the AppImage of a release (as returned by the GitHub API), the asset named
// ModOrganizer-<version>-x86_64.AppImage (not the .zsync nor the debug symbols)
std::optional<Asset> appImageAsset(const QJsonObject& release);

// the SHA256SUMS asset of a release
std::optional<Asset> checksumAsset(const QJsonObject& release);

// the SHA-256 of `fileName` in `sums`, in the format of sha256sum ("<hex>  <name>",
// or "<hex> *<name>"), as lowercase hex; std::nullopt when the file is not listed
std::optional<QByteArray> expectedSha256(const QByteArray& sums,
                                         const QString& fileName);

enum class ReplaceError
{
  None,
  // the directory of the target can't be written to
  NotWritable,
  // the downloaded file is not the one of the release
  ChecksumMismatch,
  // anything else (a missing target, a download outside the directory of the target,
  // a failed chmod or rename)
  IoError
};

// a description of the error, for the messages
QString toString(ReplaceError e);

// replaces `target` with `downloaded`, which must be in the directory of `target` so
// that the rename is atomic: checks the SHA-256 of `downloaded` against
// `expectedSha256` (lowercase hex), gives it the permissions of `target` and renames
// it over `target`. On any error, `target` is left as it was and `downloaded` is
// removed (when the directory allows it).
ReplaceError replaceAppImage(const QString& downloaded, const QString& target,
                             const QByteArray& expectedSha256);

// where to download the new version of `target`: <dir of target>/.<name>.update-<pid>
QString downloadPathFor(const QString& target);

// remembers a program to execute once Mod Organizer has exited normally (main(), after
// run(): the virtual file system is unmounted and the application is gone)
void scheduleReexec(const QString& appImage, const QStringList& args);

// executes what scheduleReexec() remembered, with execv() (no shell), argv[0] being
// the path, and the current environment (that of the host, appimage_linux.h). Returns
// false at once when nothing was scheduled; otherwise it only returns if execv() failed,
// which is logged, with true.
bool runPendingReexec();
}  // namespace selfupdate

#endif  // SELFUPDATE_LINUX_H
