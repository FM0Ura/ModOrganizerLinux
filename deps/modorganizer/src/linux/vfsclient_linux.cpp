// Client of the mo2-vfs protocol (vfsclient_linux.h)

#include "vfsclient_linux.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTimer>
#include <stdexcept>
#include <uibase/log.h>

using namespace MOBase;

namespace
{
constexpr std::chrono::milliseconds HelloTimeout{10000};
constexpr std::chrono::milliseconds ExitTimeout{5000};
constexpr qsizetype StderrTailSize = 2000;
}  // namespace

struct VfsClient::State
{
  QString binary;
  QProcess process;
  QByteArray buffer;
  QByteArray stderrTail;
  QList<QJsonObject> replies;
  bool hello           = false;
  bool expectUnmounted = false;
  bool exited          = false;
  int exitCode         = 0;
  QEventLoop* loop     = nullptr;
  std::function<void()> unmounted;
  std::function<void(int, QString)> exitedHandler;

  void wake()
  {
    if (loop) {
      loop->quit();
    }
  }
};

VfsClient::VfsClient(QString binary, QStringList args) : m_state(new State)
{
  auto& s  = *m_state;
  s.binary = binary;

  s.process.setProcessChannelMode(QProcess::SeparateChannels);
  s.process.setProgram(binary);
  s.process.setArguments(args);

  auto* state = m_state.get();

  QObject::connect(&s.process, &QProcess::readyReadStandardOutput, &s.process, [this] {
    onStdout();
  });

  QObject::connect(&s.process, &QProcess::readyReadStandardError, &s.process, [state] {
    const auto data = state->process.readAllStandardError();
    state->stderrTail += data;
    if (state->stderrTail.size() > StderrTailSize) {
      state->stderrTail = state->stderrTail.right(StderrTailSize);
    }
    for (const auto& line : data.trimmed().split('\n')) {
      if (!line.trimmed().isEmpty()) {
        log::debug("mo2-vfs: {}", QString::fromUtf8(line.trimmed()));
      }
    }
  });

  QObject::connect(&s.process, &QProcess::finished, &s.process,
                   [this, state](int code, QProcess::ExitStatus status) {
                     // what the process said last may still be in the pipes
                     onStdout();
                     state->exited   = true;
                     state->exitCode = status == QProcess::NormalExit ? code : -1;
                     state->wake();
                     if (state->exitedHandler) {
                       state->exitedHandler(
                           state->exitCode,
                           QString::fromUtf8(state->stderrTail).trimmed());
                     }
                   });

  s.process.start(QIODevice::ReadWrite);
  if (!s.process.waitForStarted(5000)) {
    throw std::runtime_error("cannot start " + binary.toStdString() + ": " +
                             s.process.errorString().toStdString());
  }

  if (!waitFor(
          [state] {
            return state->hello || state->exited;
          },
          HelloTimeout) ||
      !s.hello) {
    const auto tail = QString::fromUtf8(s.stderrTail).trimmed();
    std::string msg = s.exited ? "mo2-vfs ended before saying hello (exit code " +
                                     std::to_string(s.exitCode) + ")"
                               : "mo2-vfs did not say hello in time";
    if (!tail.isEmpty()) {
      msg += ": " + tail.toStdString();
    }
    s.exitedHandler = nullptr;
    throw std::runtime_error(msg);
  }
}

VfsClient::~VfsClient()
{
  auto& s = *m_state;

  s.unmounted     = nullptr;
  s.exitedHandler = nullptr;

  if (s.process.state() == QProcess::NotRunning) {
    return;
  }

  s.process.closeWriteChannel();

  if (!s.process.waitForFinished(static_cast<int>(ExitTimeout.count()))) {
    // QProcess would kill it when destroyed; it is left running instead and ends by
    // itself when its watched process groups have ended (the object is not freed so
    // that it is not killed either)
    log::warn("mo2-vfs is still running, left to end by itself");
    s.process.disconnect();
    s.process.setParent(nullptr);
    (void)m_state.release();
  }
}

void VfsClient::setUnmountedHandler(std::function<void()> f)
{
  m_state->unmounted = std::move(f);
}

void VfsClient::setExitedHandler(std::function<void(int, QString)> f)
{
  m_state->exitedHandler = std::move(f);
}

bool VfsClient::running() const
{
  return !m_state->exited && m_state->process.state() == QProcess::Running;
}

bool VfsClient::waitFor(const std::function<bool()>& done,
                        std::chrono::milliseconds timeout)
{
  auto& s = *m_state;

  QElapsedTimer elapsed;
  elapsed.start();

  while (!done()) {
    const auto left = timeout.count() - elapsed.elapsed();
    if (left <= 0) {
      return false;
    }

    QEventLoop loop;
    QEventLoop* previous = s.loop;
    s.loop               = &loop;
    QTimer::singleShot(static_cast<int>(left), &loop, &QEventLoop::quit);
    loop.exec();
    s.loop = previous;
  }

  return true;
}

QJsonObject VfsClient::request(const QJsonObject& r, std::chrono::milliseconds timeout)
{
  auto& s = *m_state;

  if (!running()) {
    throw std::runtime_error("mo2-vfs is not running");
  }

  s.replies.clear();
  s.expectUnmounted = r.value("op").toString() == "unmount";

  s.process.write(QJsonDocument(r).toJson(QJsonDocument::Compact) + '\n');

  const bool got = waitFor(
      [&s] {
        return !s.replies.isEmpty() || s.exited;
      },
      timeout);

  s.expectUnmounted = false;

  if (s.replies.isEmpty()) {
    const auto op   = r.value("op").toString().toStdString();
    const auto tail = QString::fromUtf8(s.stderrTail).trimmed();

    std::string msg = got ? "mo2-vfs ended (exit code " + std::to_string(s.exitCode) +
                                ") before replying to " + op
                          : "mo2-vfs did not reply to " + op + " in time";
    if (got && !tail.isEmpty()) {
      msg += ": " + tail.toStdString();
    }
    throw std::runtime_error(msg);
  }

  const auto reply = s.replies.takeFirst();

  if (reply.value("event").toString() == "error") {
    std::string msg = reply.value("message").toString().toStdString();
    const auto path = reply.value("path");
    if (path.isString() && !path.toString().isEmpty()) {
      msg += " (" + path.toString().toStdString() + ")";
    }
    throw std::runtime_error(msg);
  }

  return reply;
}

void VfsClient::onStdout()
{
  auto& s = *m_state;

  s.buffer += s.process.readAllStandardOutput();

  qsizetype nl;
  while ((nl = s.buffer.indexOf('\n')) >= 0) {
    const auto line = s.buffer.left(nl).trimmed();
    s.buffer.remove(0, nl + 1);
    if (!line.isEmpty()) {
      onLine(line);
    }
  }
}

void VfsClient::onLine(const QByteArray& line)
{
  auto& s = *m_state;

  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(line, &error);
  if (!doc.isObject()) {
    log::warn("mo2-vfs: unexpected output: {}", QString::fromUtf8(line));
    return;
  }

  const auto event = doc.object();
  const auto name  = event.value("event").toString();

  if (name == "hello") {
    s.hello = true;
  } else if (name == "unmounted" && !s.expectUnmounted) {
    if (s.unmounted) {
      s.unmounted();
    }
  } else {
    s.replies.push_back(event);
  }

  s.wake();
}
