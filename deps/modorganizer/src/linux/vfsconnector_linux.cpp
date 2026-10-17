// Linux implementation of the virtual file system connector (vfsconnector.h): the FUSE
// file system of the mo2-vfs program.
//
// updateMapping() and updateParams() only keep what they are given; activate() starts
// mo2-vfs (once; it is kept for the next activations) and mounts the file system, or
// updates it if it is mounted. The paths of the mappings are disk paths; the ones built
// by OrganizerCore::fileMapping() from the data tree and the ones given by plugins
// (local saves, file mappers) may use backslashes, which are converted to slashes here
// (MOShared::normalizeSeparators()).

#include "../shared/appconfig.h"
#include "../shared/util.h"
#include "../vfsconnector.h"
#include "vfsclient_linux.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcess>
#include <QTimer>
#include <stdexcept>

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
  process.start(binary, {"--cleanup"});

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

  void activate() override
  {
    if (m_client && !m_client->running()) {
      m_client.reset();
    }

    if (!m_client) {
      if (m_cleanupNeeded) {
        // the helper that ended abnormally may have left its mount behind
        const auto stale = cleanupStaleMounts();
        if (!stale.isEmpty()) {
          m_stale = stale;
        }
        m_cleanupNeeded = false;
      }

      start();
    }

    QJsonArray mappings;
    for (const auto& m : m_mapping) {
      QJsonObject o;
      o["source"]        = m.source;
      o["destination"]   = m.destination;
      o["is_directory"]  = m.isDirectory;
      o["create_target"] = m.createTarget;
      mappings.push_back(o);
    }

    const bool update = m_active;

    QJsonObject r;
    r["op"]                 = update ? "update" : "mount";
    r["mappings"]           = mappings;
    r["skip_file_suffixes"] = toJson(m_skipFileSuffixes);
    r["skip_directories"]   = toJson(m_skipDirectories);

    m_client->request(r, 30s);

    if (!update) {
      m_active  = true;
      m_watched = 0;
    }
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
      m_client->request(r, 10s);
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
      m_client->request(QJsonObject{{"op", "unmount"}}, 10s);
    } catch (const std::exception& e) {
      log::error("cannot unmount the virtual file system: {}", e.what());
      return;
    }

    deactivated();
  }

  bool isActive() const override { return m_active; }

  bool waitInactive(std::chrono::milliseconds timeout) override
  {
    QEventLoop loop;
    QTimer tick;
    QTimer deadline;

    deadline.setSingleShot(true);

    QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
      if (!m_active) {
        loop.quit();
      }
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);

    if (!m_active) {
      return true;
    }

    tick.start(20);
    deadline.start(static_cast<int>(timeout.count()));
    loop.exec();

    return !m_active;
  }

  void setDeactivatedCallback(std::function<void()> f) override
  {
    m_deactivated = std::move(f);
  }

private:
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

    auto client = std::make_unique<VfsClient>(binary, args);

    client->setUnmountedHandler([this] {
      deactivated();
    });

    client->setExitedHandler([this](int code, QString stderrTail) {
      if (!m_active) {
        return;
      }

      log::error("mo2-vfs ended with exit code {} while the virtual file system was "
                 "active: {}",
                 code, stderrTail);
      m_cleanupNeeded = true;
      deactivated();
    });

    m_client = std::move(client);
  }

  void deactivated()
  {
    if (!m_active) {
      return;
    }

    m_active  = false;
    m_watched = 0;

    if (m_deactivated) {
      // in the thread of the connector, once the event that caused this is handled
      QMetaObject::invokeMethod(
          &m_context,
          [f = m_deactivated] {
            f();
          },
          Qt::QueuedConnection);
    }
  }

  MappingType m_mapping;
  QStringList m_skipFileSuffixes;
  QStringList m_skipDirectories;
  log::Levels m_logLevel = log::Info;
  QStringList m_stale;

  QObject m_context;
  std::unique_ptr<VfsClient> m_client;
  bool m_active        = false;
  bool m_cleanupNeeded = false;
  int m_watched        = 0;
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
