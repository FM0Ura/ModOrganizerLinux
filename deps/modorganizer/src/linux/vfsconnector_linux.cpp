// Linux implementation of the virtual file system connector (vfsconnector.h): the FUSE
// file system of the mo2-vfs program.
//
// mo2-vfs is started through vfsbridge::helperCommand() (on the host when Mod Organizer
// is in a container); the mount table is the helper's (`--list-mounts`), not this
// process'.
//
// updateMapping() and updateParams() only keep what they are given; activate() starts
// mo2-vfs (once; it is kept for the next activations) and mounts the file system, or
// updates it if it is mounted (once per run: an update sent by updateMapping() while
// mounted is not repeated by the activate() that follows it). spawn() starts a program
// through the helper, which runs it where the helper runs (the host, when Mod
// Organizer is in a container); the exit codes come from the exited events
// (exitcodes_linux.h). The paths of the mappings are disk paths; the ones built
// by OrganizerCore::fileMapping() from the data tree and the ones given by plugins
// (local saves, file mappers) may use backslashes, which are converted to slashes here
// (MOShared::normalizeSeparators()).

#include "../shared/appconfig.h"
#include "../shared/util.h"
#include "../vfsconnector.h"
#include "exitcodes_linux.h"
#include "vfsbridge_linux.h"
#include "vfsclient_linux.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcess>
#include <QTimer>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace MOBase;

namespace
{
using namespace std::chrono_literals;

// the mo2-vfs program: MO2_VFS_BINARY, or the one next to the executable
QString vfsBinary()
{
  const auto env = qEnvironmentVariable("MO2_VFS_BINARY");
  if (!env.isEmpty()) {
    return env;
  }

  return QCoreApplication::applicationDirPath() + "/mo2-vfs";
}

// runs `mo2-vfs --cleanup`; returns the roots that could not be unmounted
QStringList cleanupStaleMounts()
{
  const auto binary = vfsBinary();

  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);

  try {
    const auto cmd = vfsbridge::helperCommand(binary, {"--cleanup"});
    process.start(cmd.program, cmd.arguments);
  } catch (const std::exception& e) {
    log::warn("cannot clean up stale mounts: {}", e.what());
    return {};
  }

  if (!process.waitForStarted(5000)) {
    log::warn("cannot run {} --cleanup: {}", binary, process.errorString());
    return {};
  }

  process.closeWriteChannel();

  if (!process.waitForFinished(10000)) {
    log::warn("{} --cleanup did not finish in time", binary);
    process.kill();
    process.waitForFinished(2000);
    return {};
  }

  QStringList stale;

  for (const auto& line : process.readAllStandardOutput().split('\n')) {
    const auto doc = QJsonDocument::fromJson(line.trimmed());
    if (!doc.isObject()) {
      continue;
    }

    const auto o = doc.object();
    if (!o.value("ok").toBool()) {
      const auto root = o.value("root").toString();
      log::warn("stale mount {} could not be unmounted: {}", root,
                o.value("error").toString());
      stale.push_back(root);
    }
  }

  return stale;
}

// the timeout of mount and update requests; MO2_VFS_TIMEOUT_MS replaces it (tests)
std::chrono::milliseconds mountTimeout()
{
  bool ok         = false;
  const auto test = qEnvironmentVariable("MO2_VFS_TIMEOUT_MS").toInt(&ok);
  return ok && test > 0 ? std::chrono::milliseconds(test) : 30s;
}

QString logLevelName(log::Levels level)
{
  switch (level) {
  case log::Debug:
    return "debug";
  case log::Warning:
    return "warn";
  case log::Error:
    return "error";
  case log::Info:
  default:
    return "info";
  }
}

QJsonArray toJson(const QStringList& list)
{
  QJsonArray a;
  for (const auto& s : list) {
    a.push_back(s);
  }
  return a;
}

// the mo2-vfs mount points of the mount namespace of the helper (`mo2-vfs
// --list-mounts`: on the host, when Mod Organizer is in a container)
QStringList mountedRoots()
{
  const auto binary = vfsBinary();

  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);

  try {
    const auto cmd = vfsbridge::helperCommand(binary, {"--list-mounts"});
    process.start(cmd.program, cmd.arguments);
  } catch (const std::exception& e) {
    log::warn("cannot list the mounts: {}", e.what());
    return {};
  }

  if (!process.waitForStarted(5000)) {
    log::warn("cannot run {} --list-mounts: {}", binary, process.errorString());
    return {};
  }

  process.closeWriteChannel();

  if (!process.waitForFinished(10000)) {
    log::warn("{} --list-mounts did not finish in time", binary);
    process.kill();
    process.waitForFinished(2000);
    return {};
  }

  for (const auto& line : process.readAllStandardOutput().split('\n')) {
    const auto doc = QJsonDocument::fromJson(line.trimmed());
    if (!doc.isObject() || !doc.object().contains("roots")) {
      continue;
    }

    QStringList roots;
    for (const auto& r : doc.object().value("roots").toArray()) {
      roots.push_back(r.toString());
    }

    return roots;
  }

  log::warn("{} --list-mounts did not list the mounts: {}", binary,
            QString::fromUtf8(process.readAllStandardError()).trimmed());
  return {};
}
}  // namespace

class LinuxVfsConnector : public VfsConnector
{
public:
  explicit LinuxVfsConnector(QStringList stale) : m_stale(std::move(stale)) {}

  void updateMapping(const MappingType& mapping) override
  {
    m_mapping.clear();
    m_mapping.reserve(mapping.size());

    for (const auto& m : mapping) {
      m_mapping.push_back({MOShared::normalizeSeparators(m.source),
                           MOShared::normalizeSeparators(m.destination), m.isDirectory,
                           m.createTarget});
    }

    log::debug("{} mappings kept", m_mapping.size());

    if (m_active) {
      // the mounted file system follows the mappings (also called from the refresh
      // and profile code, which don't expect an exception)
      try {
        send(mappingRequest("update"), mountTimeout());
        m_mappingSent = true;
      } catch (const std::exception& e) {
        m_mappingSent = false;
        log::error("cannot update the virtual file system: {}", e.what());
      }
    } else {
      m_mappingSent = false;
    }
  }

  void updateParams(log::Levels logLevel, env::CoreDumpTypes, const QString&,
                    std::chrono::seconds, QString, const QStringList& skipFileSuffixes,
                    const QStringList& skipDirectories) override
  {
    // the crash dumps, spawn delay and blacklisted executables are those of usvfs
    m_logLevel         = logLevel;
    m_skipFileSuffixes = skipFileSuffixes;
    m_skipDirectories  = skipDirectories;
  }

  void updateForcedLibraries(
      const QList<ExecutableForcedLoadSetting>& forcedLibraries) override
  {
    // forced libraries are DLLs injected by usvfs into the started programs
    if (!forcedLibraries.isEmpty()) {
      log::debug("forced libraries are not supported on Linux, {} ignored",
                 forcedLibraries.size());
    }
  }

  QString name() const override { return "mo2-vfs"; }

  QStringList staleMounts() const override { return m_stale; }

  // while it runs, the file system counts as active (isActive()): a refresh started
  // from the event loops of the requests must not walk a mount as it comes up
  void activate() override
  {
    const bool wasActivating = std::exchange(m_activating, true);

    try {
      doActivate();
    } catch (...) {
      m_activating = wasActivating;

      // what was deferred while activating (and a deactivation that was not delivered)
      // is still news
      queueDeactivatedCallback();
      throw;
    }

    m_activating = wasActivating;
  }

  void watchProcessGroup(qint64 pgid) override
  {
    if (!m_active) {
      return;
    }

    QJsonObject r;
    r["op"]   = "watch";
    r["pgid"] = pgid;

    try {
      send(r, 10s);
      ++m_watched;
    } catch (const std::exception& e) {
      log::error("cannot watch process group {}: {}", pgid, e.what());
    }
  }

  void releaseIfIdle() override
  {
    if (!m_active || m_watched > 0) {
      return;
    }

    try {
      send(QJsonObject{{"op", "unmount"}}, 10s);
    } catch (const std::exception& e) {
      log::error("cannot unmount the virtual file system: {}", e.what());
      return;
    }

    deactivated();
  }

  // also while activating, and while a mount that could not be unmounted is left
  // (the data directory would not show the game's own files)
  bool isActive() const override
  {
    return m_active || m_activating || !m_leftMounted.isEmpty();
  }

  QString takeLostReason() override { return std::exchange(m_lostReason, {}); }

  bool waitInactive(std::chrono::milliseconds timeout) override
  {
    QEventLoop loop;
    QTimer tick;
    QTimer deadline;

    deadline.setSingleShot(true);

    QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
      if (!isActive()) {
        loop.quit();
      }
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);

    if (!isActive()) {
      return true;
    }

    tick.start(20);
    deadline.start(static_cast<int>(timeout.count()));
    loop.exec();

    return !isActive();
  }

  void setDeactivatedCallback(std::function<void()> f) override
  {
    m_deactivated = std::move(f);
  }

  qint64 spawn(const VfsSpawnRequest& request) override
  {
    if (m_client && !m_client->running()) {
      helperLost("mo2-vfs is not running");
      m_client.reset();
    }

    if (!m_client) {
      // a program that runs without the virtual file system still runs where the
      // helper runs (the host)
      start();
    }

    QJsonObject env;
    for (const auto& e : request.environment) {
      const auto eq   = e.indexOf('=');
      env[e.left(eq)] = e.mid(eq + 1);  // validated by the caller (ProtonLauncher)
    }

    QJsonObject r;
    r["op"]   = "spawn";
    r["argv"] = toJson(request.argv);
    r["env"]  = env;
    r["cwd"]  = request.workingDirectory;
    if (!request.winePrefix.isEmpty()) {
      r["wineprefix"] = request.winePrefix;
    }
    if (!request.output.isEmpty()) {
      r["output"] = request.output;
    }

    log::debug("spawn {} in '{}', env {}", request.argv.join(' '),
               request.workingDirectory, request.environment.join(' '));

    const auto reply = send(r, 10s);
    const auto pid   = reply.value("pid").toInteger();

    if (pid <= 0) {
      throw std::runtime_error("mo2-vfs did not give the pid of the program");
    }

    if (m_active) {
      // its process group keeps the mount: releaseIfIdle() must not unmount
      ++m_watched;
    }

    return pid;
  }

  std::vector<qint64> runningPrograms() override
  {
    if (!m_client || !m_client->running()) {
      return {};
    }

    try {
      std::vector<qint64> pids;
      for (const auto& p :
           send(QJsonObject{{"op", "status"}}, 10s).value("processes").toArray()) {
        pids.push_back(p.toInteger());
      }
      return pids;
    } catch (const std::exception& e) {
      log::error("cannot ask mo2-vfs for its programs: {}", e.what());
      return {};
    }
  }

private:
  void doActivate()
  {
    if (m_client && !m_client->running()) {
      helperLost("mo2-vfs is not running");
      m_client.reset();
    }

    // a new helper has nothing mounted: always `mount`, never `update`
    const bool fresh = !m_client;

    if (fresh) {
      if (m_cleanupNeeded || !m_leftMounted.isEmpty()) {
        // the helper that ended abnormally may have left its mount behind
        cleanupLeftMounts();
      }

      if (!m_leftMounted.isEmpty()) {
        throw std::runtime_error(
            "the virtual file system of a previous mo2-vfs is still mounted on " +
            m_leftMounted.join(", ").toStdString() +
            " and cannot be unmounted; close the programs that use it and run "
            "fusermount3 -uz on it");
      }

      start();
    }

    if (m_active) {
      // one update per run: updateMapping() already sent these mappings
      if (!std::exchange(m_mappingSent, false)) {
        send(mappingRequest("update"), mountTimeout());
      }
      return;
    }

    // callbacks queued for earlier deactivations are history, even if they are
    // delivered while the mount request waits
    ++m_generation;

    const auto reply = send(mappingRequest("mount"), mountTimeout());

    m_roots.clear();
    for (const auto& r : reply.value("roots").toArray()) {
      m_roots.push_back(r.toString());
    }

    m_active              = true;
    m_mappingSent         = false;
    m_watched             = 0;
    m_pendingDeactivation = false;
    m_leftMounted.clear();
  }

  QJsonObject mappingRequest(const char* op) const
  {
    QJsonArray mappings;
    for (const auto& m : m_mapping) {
      QJsonObject o;
      o["source"]        = m.source;
      o["destination"]   = m.destination;
      o["is_directory"]  = m.isDirectory;
      o["create_target"] = m.createTarget;
      mappings.push_back(o);
    }

    QJsonObject r;
    r["op"]                 = op;
    r["mappings"]           = mappings;
    r["skip_file_suffixes"] = toJson(m_skipFileSuffixes);
    r["skip_directories"]   = toJson(m_skipDirectories);
    return r;
  }

  // a helper that is gone, or that stopped answering, loses the file system
  // (helperLost())
  QJsonObject send(const QJsonObject& r, std::chrono::milliseconds timeout)
  {
    try {
      return m_client->request(r, timeout);
    } catch (const std::exception& e) {
      if (!m_client->running()) {
        helperLost(QString::fromStdString(e.what()));
      }
      throw;
    }
  }

  // the helper ended, or stopped answering: one that stopped answering is killed when
  // no watched program may still use its mount, so that the refresh can't walk a hung
  // mount; then what it left mounted is unmounted (`mo2-vfs --cleanup`) before the file
  // system counts as inactive, so that the refresh doesn't walk a dead mount (ENOTCONN:
  // the data directory would look empty, and the plugin lists would be written without
  // the game's own plugins); once per helper
  void helperLost(const QString& reason)
  {
    if (m_lossHandled) {
      return;
    }
    m_lossHandled = true;

    lost(reason);

    if (m_client && !m_client->exited() && m_watched > 0) {
      // not killed: a program may still be using the mount; whatever the helper left
      // behind is cleaned up before the next one starts
      log::error("mo2-vfs stopped answering while programs use the virtual file "
                 "system: {}",
                 reason);
      m_cleanupNeeded = true;
      deactivated();
      return;
    }

    if (m_client && !m_client->exited()) {
      // a hung helper; its exited handler comes back here
      log::error("killing mo2-vfs, which stopped answering: {}", reason);
      m_client->kill();
    }

    if (m_active || m_activating || !m_leftMounted.isEmpty()) {
      cleanupLeftMounts();
    }
  }

  // runs `mo2-vfs --cleanup` and checks the mount table: inactive when none of the
  // roots of the last mount is still mounted, otherwise they are left mounted
  // (isActive(), staleMounts()) and the reason says so
  void cleanupLeftMounts()
  {
    cleanupStaleMounts();

    const auto mounted = mountedRoots();
    QStringList left;
    for (const auto& root : m_roots + m_leftMounted) {
      if (mounted.contains(root) && !left.contains(root)) {
        left.push_back(root);
      }
    }

    if (left.isEmpty()) {
      m_cleanupNeeded = false;
      deactivated();
      return;
    }

    log::error("the virtual file system is still mounted on {} after the cleanup",
               left.join(", "));

    keepLeftMounted(left, "it could not be unmounted: " + left.join(", ") +
                              "; the data directory is refreshed once it is "
                              "unmounted (fusermount3 -uz)");
    m_cleanupNeeded = true;
  }

  // the file system is no longer usable but is still mounted on these roots
  void keepLeftMounted(const QStringList& roots, const QString& why)
  {
    if (m_lostReason.isEmpty()) {
      m_lostReason = why;
    } else if (!m_lostReason.contains(why)) {
      m_lostReason += "; " + why;
    }

    m_active      = false;
    m_watched     = 0;
    m_leftMounted = roots;

    for (const auto& root : roots) {
      if (!m_stale.contains(root)) {
        m_stale.push_back(root);
      }
    }
  }

  void start()
  {
    const auto binary = vfsBinary();

    if (!QFileInfo(binary).isFile()) {
      throw std::runtime_error("mo2-vfs not found: " + binary.toStdString());
    }

    QStringList args;

    // no data path in the tests: the logs go to the standard error, which VfsClient
    // sends to the log of Mod Organizer
    if (const auto dp = qApp->property("dataPath"); !dp.isNull()) {
      const auto dir =
          dp.toString() + "/" + QString::fromStdWString(AppConfig::logPath());
      QDir().mkpath(dir);
      args << "--log" << dir + "/mo2-vfs.log";
    }

    args << "--log-level" << logLevelName(m_logLevel);

    const auto cmd = vfsbridge::helperCommand(binary, args);
    auto client    = std::make_unique<VfsClient>(cmd.program, cmd.arguments);

    client->setProgramExitedHandler([](qint64 pid, int code) {
      log::debug("program {} ended with {}", pid, code);
      exitcodes::record(pid, code);
    });

    client->setUnmountedHandler([this] {
      deactivated();
    });

    // the helper retries the unmount and reports `unmounted` when it works
    client->setUnmountFailedHandler([this](QString message, QString path) {
      if (!m_active) {
        return;
      }

      QStringList roots = m_roots;
      if (!path.isEmpty() && !roots.contains(path)) {
        roots.push_back(path);
      }

      m_cleanupNeeded = true;
      keepLeftMounted(roots, message);
    });

    client->setExitedHandler([this](QString how, QString stderrTail) {
      if (!isActive()) {
        return;
      }

      log::error("mo2-vfs ended ({}) while the virtual file system was active: {}", how,
                 stderrTail);
      helperLost(stderrTail.isEmpty() ? "mo2-vfs ended (" + how + ")"
                                      : "mo2-vfs ended (" + how + "): " + stderrTail);
    });

    m_client      = std::move(client);
    m_lossHandled = false;
  }

  // the file system stopped while it was mounted; the first reason is kept until
  // takeLostReason()
  void lost(const QString& reason)
  {
    if (m_active && m_lostReason.isEmpty()) {
      m_lostReason = reason;
    }
  }

  // nothing is mounted any more
  void deactivated()
  {
    if (!m_active && m_leftMounted.isEmpty()) {
      return;
    }

    m_active  = false;
    m_watched = 0;
    m_leftMounted.clear();

    m_pendingDeactivation = true;
    queueDeactivatedCallback();
  }

  void queueDeactivatedCallback()
  {
    if (!m_deactivated) {
      return;
    }

    // in the thread of the connector, once the event that caused this is handled
    QMetaObject::invokeMethod(
        &m_context,
        [this, f = m_deactivated, gen = m_generation] {
          // not for a deactivation that is already history
          if (!isActive() && gen == m_generation) {
            m_pendingDeactivation = false;
            f();
          }
        },
        Qt::QueuedConnection);
  }

  MappingType m_mapping;
  QStringList m_skipFileSuffixes;
  QStringList m_skipDirectories;
  log::Levels m_logLevel = log::Info;
  QStringList m_stale;
  QString m_lostReason;

  QObject m_context;
  std::unique_ptr<VfsClient> m_client;

  // the roots of the last mount (its `ready` event)
  QStringList m_roots;

  // the roots of a file system that is not usable any more but could not be unmounted
  QStringList m_leftMounted;

  bool m_active              = false;
  bool m_mappingSent         = false;
  bool m_activating          = false;
  bool m_cleanupNeeded       = false;
  bool m_lossHandled         = false;
  int m_watched              = 0;
  unsigned m_generation      = 0;
  bool m_pendingDeactivation = false;
  std::function<void()> m_deactivated;
};

std::unique_ptr<VfsConnector> createVfsConnector()
{
  auto stale = cleanupStaleMounts();

  if (!stale.isEmpty()) {
    log::warn("{} stale mounts left by a previous run", stale.size());
  }

  return std::make_unique<LinuxVfsConnector>(std::move(stale));
}
