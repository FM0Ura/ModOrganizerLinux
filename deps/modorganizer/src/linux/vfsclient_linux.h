#ifndef VFSCLIENT_LINUX_H
#define VFSCLIENT_LINUX_H

#include <QByteArray>
#include <QEventLoop>
#include <QJsonObject>
#include <QList>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <chrono>
#include <functional>
#include <memory>

// the client of the mo2-vfs protocol: lines of JSON on the standard input and output
// of a mo2-vfs process (mo2-vfs/src/ipc.rs)
//
// the process is started by the constructor and the `hello` event is waited for;
// request() sends a request and runs an event loop until the reply arrives, so the
// client must be used from the thread it was created in, which must have a
// QCoreApplication
//
class VfsClient
{
public:
  // starts the process and waits for its hello event; throws std::runtime_error if it
  // can't be started or doesn't say hello within 10 seconds
  VfsClient(QString binary, QStringList args);

  // closes the standard input, which makes the process end by itself (when it watches
  // process groups, only once they have ended) and waits for it for up to 5 seconds;
  // it is not killed, because it may be holding the mount of a running game: the
  // process is left running after the timeout
  ~VfsClient();

  VfsClient(const VfsClient&)            = delete;
  VfsClient& operator=(const VfsClient&) = delete;

  // not re-entrant: a call made while another is waiting (from an event processed by
  // its event loop) throws; after a timeout the client is unusable (running() is false)
  //
  // sends a request and returns the reply event; throws std::runtime_error on timeout,
  // when the process ends before replying or when the reply is an error event (the
  // message is the one of the event, followed by its path in parentheses if it has
  // one)
  //
  // `unmounted` events are taken as the reply to an `unmount` request only; in any
  // other case they are spontaneous and given to the unmounted handler
  //
  QJsonObject request(const QJsonObject& r, std::chrono::milliseconds timeout);

  // called for the spontaneous unmounted events, from the event loop of the thread
  // of the client
  void setUnmountedHandler(std::function<void()> f);

  // called when the process has ended, with how ("exit code 1", "crashed (...)") and
  // the end of what it wrote on its standard error
  void setExitedHandler(std::function<void(QString, QString)> f);

  bool running() const;

private:
  struct State;

  // runs the event loop until done() or the timeout; false on timeout
  bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout);

  void onStdout();
  void onLine(const QByteArray& line);

  std::unique_ptr<State> m_state;
};

#endif  // VFSCLIENT_LINUX_H
