#ifndef MODLISTS_BACKGROUND_H
#define MODLISTS_BACKGROUND_H

#include <QEventLoop>
#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>

namespace modlists
{

// Runs `work` on another thread and returns when it is done, the GUI responsive
// meanwhile (a nested event loop); `tick` runs on the GUI thread every 100 ms.
// `work` must not touch widgets, and whatever it uses must outlive it: the caller may
// be deleted during the nested loop, so it checks a QPointer to itself afterwards.
inline void runInBackground(const std::function<void()>& work,
                            const std::function<void()>& tick)
{
  QEventLoop loop;
  QFutureWatcher<void> watcher;
  QObject::connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
  QTimer timer;
  timer.setInterval(100);
  QObject::connect(&timer, &QTimer::timeout, tick);

  QFuture<void> future = QtConcurrent::run(work);
  watcher.setFuture(future);
  timer.start();
  if (!future.isFinished()) {
    loop.exec();
  }
  future.waitForFinished();
}

}  // namespace modlists

#endif  // MODLISTS_BACKGROUND_H
