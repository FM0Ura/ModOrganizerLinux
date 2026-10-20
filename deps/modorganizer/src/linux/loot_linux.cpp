// Linux implementations of the Win32-only parts of loot.cpp that don't need the rest of
// the application (loot_linux.h); the members of Loot that start lootcli and wait for
// it are in lootrun_linux.cpp.

#include "loot_linux.h"
#include "../shared/appconfig.h"
#include "vfsbridge_linux.h"

#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#include <lootcli/lootcli.h>
#include <uibase/log.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <unistd.h>
#include <utility>

using namespace MOBase;

namespace
{
// a file as the host sees it: its directory through hostCanonicalPath() (which returns
// the path of a file that doesn't exist yet unchanged) and its name
QString hostFilePath(const QString& path, const QString& rootDir)
{
  const QFileInfo info(path);
  return vfsbridge::hostCanonicalPath(info.absolutePath(), rootDir) + "/" +
         info.fileName();
}
}  // namespace

QString lootcliPath(const QString& binDirectory)
{
  return binDirectory + "/loot/lootcli";
}

QString lootUnavailableReason(const QString& binDirectory)
{
  const QFileInfo lootcli(lootcliPath(binDirectory));

  if (lootcli.isFile() && lootcli.isExecutable()) {
    return {};
  }

  return QObject::tr("lootcli was not found in %1.").arg(binDirectory + "/loot");
}

void disableIfLootUnavailable(QAbstractButton* button)
{
  const auto reason = lootUnavailableReason();

  if (!reason.isEmpty()) {
    button->setDisabled(true);
    button->setToolTip(reason);
  }
}

QString lootFilesDirectory(const QString& dataPath)
{
  if (dataPath.isEmpty()) {
    return QDir::tempPath();
  }

  const auto dir = dataPath + "/" + QString::fromStdWString(AppConfig::logPath());
  QDir().mkpath(dir);
  return dir;
}

VfsSpawnRequest lootcliSpawnRequest(const LootcliRun& run, const QString& rootDir)
{
  VfsSpawnRequest r;

  r.argv = {hostFilePath(run.lootcli, rootDir),
            "--game",
            run.game,
            "--gamePath",
            vfsbridge::hostCanonicalPath(run.gamePath, rootDir),
            "--pluginListPath",
            hostFilePath(run.pluginListPath, rootDir),
            "--logLevel",
            run.logLevel,
            "--out",
            hostFilePath(run.reportPath, rootDir),
            "--pluginListOutputPath",
            hostFilePath(run.sortedPluginListPath, rootDir),
            "--language",
            run.language};

  if (run.skipUpdateMasterlist) {
    r.argv << "--skipUpdateMasterlist";
  }

  r.workingDirectory =
      vfsbridge::hostCanonicalPath(QFileInfo(run.lootcli).absolutePath(), rootDir);
  r.output = hostFilePath(run.outputPath, rootDir);

  return r;
}

QStringList lootcliOtherOutput(const QString& outputPath, int max)
{
  QFile file(outputPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }

  QStringList lines;
  for (const auto& raw : file.readAll().split('\n')) {
    const auto line = raw.trimmed();
    if (line.isEmpty()) {
      continue;
    }

    const std::string_view view(line.constData(),
                                static_cast<std::size_t>(line.size()));
    if (lootcli::parseMessage(view).type != lootcli::MessageType::None) {
      continue;
    }

    lines << QString::fromUtf8(line);
  }

  if (lines.size() > max) {
    lines = lines.mid(lines.size() - max);
  }

  return lines;
}

AsyncPipe::AsyncPipe(QString path) : m_path(std::move(path)) {}

AsyncPipe::~AsyncPipe()
{
  if (m_fd != -1) {
    ::close(m_fd);
  }
}

env::HandlePtr AsyncPipe::create()
{
  const auto native = QFile::encodeName(m_path);
  const int fd =
      ::open(native.constData(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

  if (fd == -1) {
    const auto e = errno;
    log::error("cannot create '{}' for the output of lootcli, {}", m_path,
               std::strerror(e));
    return {};
  }

  if (m_fd != -1) {
    ::close(m_fd);
  }
  m_fd = fd;

  return env::HandlePtr(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(m_fd)));
}

std::string AsyncPipe::read()
{
  if (m_fd == -1) {
    return {};
  }

  std::string out;
  char buffer[50000];

  for (;;) {
    const auto n = ::read(m_fd, buffer, sizeof(buffer));
    if (n <= 0) {
      // the end of what was written so far, or an error
      break;
    }
    out.append(buffer, static_cast<std::size_t>(n));
  }

  return out;
}
