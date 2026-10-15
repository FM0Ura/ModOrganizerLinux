// Linux implementations of the Win32-only parts of loot.cpp (the rest of loot.cpp is
// shared), and of loot_linux.h.
//
// lootcli is not part of the Linux builds yet: the sort button is disabled, with an
// explanation, when <bin>/loot/lootcli is missing. If it is there, LOOT still can't
// run before Phase 3, because spawn::startBinary() doesn't start programs on Linux
// (it tells the user, and Loot::start() fails before the loot thread is created).

#include "loot_linux.h"
#include "../loot.h"

#include <QAbstractButton>
#include <QFileInfo>
#include <QObject>

#include <uibase/log.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

using namespace MOBase;

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

  return QObject::tr("LOOT ainda não está disponível nesta versão para Linux.");
}

void disableIfLootUnavailable(QAbstractButton* button)
{
  const auto reason = lootUnavailableReason();

  if (!reason.isEmpty()) {
    button->setDisabled(true);
    button->setToolTip(reason);
  }
}

AsyncPipe::~AsyncPipe()
{
  for (int fd : {m_read, m_write}) {
    if (fd != -1) {
      ::close(fd);
    }
  }
}

env::HandlePtr AsyncPipe::create()
{
  int fds[2] = {-1, -1};

  if (::pipe2(fds, O_CLOEXEC) != 0) {
    const auto e = errno;
    log::error("failed to create the pipe for loot, {}", std::strerror(e));
    return {};
  }

  m_read  = fds[0];
  m_write = fds[1];

  // reads never block, like the overlapped reads of the named pipe on Windows
  ::fcntl(m_read, F_SETFL, ::fcntl(m_read, F_GETFL) | O_NONBLOCK);

  return env::HandlePtr(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(m_write)));
}

std::string AsyncPipe::read()
{
  if (m_read == -1) {
    return {};
  }

  char buffer[50000];
  const auto n = ::read(m_read, buffer, sizeof(buffer));

  if (n <= 0) {
    // nothing yet (EAGAIN), lootcli is finished (0) or an error
    return {};
  }

  return {buffer, buffer + n};
}

bool Loot::waitForCompletion()
{
  // never called before Phase 3: Loot::start() fails because spawn::startBinary()
  // doesn't start lootcli on Linux
  log::error("waiting for lootcli is not available on Linux yet");
  return false;
}
