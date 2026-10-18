// Exit codes of the programs started through mo2-vfs (exitcodes_linux.h): they are not
// children of Mod Organizer, so waitpid() can't get them; the connector records them
// from the exited events, which arrive on the thread of the connector.

#include "exitcodes_linux.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <map>
#include <mutex>

namespace exitcodes
{
namespace
{
  std::mutex g_mutex;
  std::map<qint64, int> g_codes;

  std::optional<int> takeNow(qint64 pid)
  {
    std::scoped_lock lock(g_mutex);
    const auto it = g_codes.find(pid);
    if (it == g_codes.end()) {
      return {};
    }
    const int code = it->second;
    g_codes.erase(it);
    return code;
  }
}  // namespace

void record(qint64 pid, int code)
{
  std::scoped_lock lock(g_mutex);
  g_codes[pid] = code;
}

std::optional<int> take(qint64 pid, std::chrono::milliseconds timeout)
{
  QElapsedTimer elapsed;
  elapsed.start();

  for (;;) {
    if (auto code = takeNow(pid)) {
      return code;
    }
    if (elapsed.elapsed() >= timeout.count()) {
      return {};
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(10);
  }
}
}  // namespace exitcodes
