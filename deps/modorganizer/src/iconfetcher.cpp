#include "iconfetcher.h"
#include "shared/util.h"
#include "thread_utils.h"

#ifndef _WIN32
#include "fileicons.h"
#include <uibase/linux/peicon.h>

#include <QIcon>
#include <QImage>
#include <cstdlib>
#endif

void IconFetcher::Waiter::wait()
{
  std::unique_lock lock(m_wakeUpMutex);
  m_wakeUp.wait(lock, [&] {
    return m_queueAvailable;
  });
  m_queueAvailable = false;
}

void IconFetcher::Waiter::wakeUp()
{
  {
    std::scoped_lock lock(m_wakeUpMutex);
    m_queueAvailable = true;
  }

  m_wakeUp.notify_one();
}

#ifdef _WIN32
IconFetcher::IconFetcher() : m_iconSize(GetSystemMetrics(SM_CXSMICON)), m_stop(false)
#else
IconFetcher::IconFetcher()
    : m_iconSize(QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize)),
      m_stop(false)
#endif
{
  m_quickCache.file      = getPixmapIcon(QFileIconProvider::File);
  m_quickCache.directory = getPixmapIcon(QFileIconProvider::Folder);

  m_thread = MOShared::startSafeThread([&] {
    threadFun();
  });
}

IconFetcher::~IconFetcher()
{
  stop();
  m_thread.join();
}

void IconFetcher::stop()
{
  m_stop = true;
  m_waiter.wakeUp();
}

QVariant IconFetcher::icon(const QString& path) const
{
  if (hasOwnIcon(path)) {
    return fileIcon(path);
  } else {
#ifdef _WIN32
    const auto dot = path.lastIndexOf(".");
#else
    // only a dot in the file name, not in a folder name (~/.local, "SkyUI 5.2/README")
    const auto slash = path.lastIndexOf('/');
    auto dot         = path.lastIndexOf('.');
    if (dot < slash) {
      dot = -1;
    }
#endif

    if (dot == -1) {
      // no extension
      return m_quickCache.file;
    }

    return extensionIcon(QStringView{path}.mid(dot));
  }
}

QPixmap IconFetcher::genericFileIcon() const
{
  return m_quickCache.file;
}

QPixmap IconFetcher::genericDirectoryIcon() const
{
  return m_quickCache.directory;
}

bool IconFetcher::hasOwnIcon(const QString& path) const
{
  static const QString exe = ".exe";
  static const QString lnk = ".lnk";
  static const QString ico = ".ico";

#ifdef _WIN32
  return path.endsWith(exe, Qt::CaseInsensitive) ||
         path.endsWith(lnk, Qt::CaseInsensitive) ||
         path.endsWith(ico, Qt::CaseInsensitive);
#else
  // .lnk files have no icon of their own on Linux, they go by their extension
  Q_UNUSED(lnk);
  return path.endsWith(exe, Qt::CaseInsensitive) ||
         path.endsWith(ico, Qt::CaseInsensitive);
#endif
}

void IconFetcher::threadFun()
{
  MOShared::SetThisThreadName("IconFetcher");

  while (!m_stop) {
    m_waiter.wait();
    if (m_stop) {
      break;
    }

    checkCache(m_extensionCache, false);
    checkCache(m_fileCache, true);
  }
}

void IconFetcher::checkCache(Cache& cache, bool files)
{
  std::set<QString> queue;

  {
    std::scoped_lock lock(cache.queueMutex);
    queue = std::move(cache.queue);
    cache.queue.clear();
  }

  if (queue.empty()) {
    return;
  }

  std::map<QString, QPixmap> map;
  for (auto&& ext : queue) {
#ifdef _WIN32
    Q_UNUSED(files);
    map.emplace(std::move(ext), getPixmapIcon(QFileInfo(ext)));
#else
    QPixmap pixmap = files ? linuxFileIcon(ext) : linuxExtensionIcon(ext);
    if (pixmap.isNull()) {
      pixmap = m_quickCache.file;
    }
    map.emplace(std::move(ext), std::move(pixmap));
#endif
  }

  {
    std::scoped_lock lock(cache.mapMutex);
    for (auto&& p : map) {
      cache.map.insert(std::move(p));
    }
  }
}

void IconFetcher::queue(Cache& cache, QString path) const
{
  {
    std::scoped_lock lock(cache.queueMutex);
    cache.queue.insert(std::move(path));
  }

  m_waiter.wakeUp();
}

QVariant IconFetcher::fileIcon(const QString& path) const
{
  {
    std::scoped_lock lock(m_fileCache.mapMutex);
    auto itor = m_fileCache.map.find(path);
    if (itor != m_fileCache.map.end()) {
      return itor->second;
    }
  }

  queue(m_fileCache, path);
  return {};
}

QVariant IconFetcher::extensionIcon(const QStringView& ext) const
{
  {
    std::scoped_lock lock(m_extensionCache.mapMutex);
    auto itor = m_extensionCache.map.find(ext);
    if (itor != m_extensionCache.map.end()) {
      return itor->second;
    }
  }

  queue(m_extensionCache, ext.toString());
  return {};
}

#ifndef _WIN32
// the icon of an .exe (embedded in the file) or of an .ico (the image itself)
QPixmap IconFetcher::linuxFileIcon(const QString& path) const
{
  if (path.endsWith(".exe", Qt::CaseInsensitive)) {
    const auto images = MOBase::details::peIconImages(path);

    const QImage* best = nullptr;
    for (const auto& image : images) {
      if (image.isNull()) {
        continue;
      }

      if (!best) {
        best = &image;
        continue;
      }

      // prefer sizes >= the wanted one, then the closest to it
      const int size  = image.width();
      const int bsize = best->width();
      const bool big  = size >= m_iconSize;
      const bool bbig = bsize >= m_iconSize;

      if (big != bbig) {
        if (big) {
          best = &image;
        }
      } else if (std::abs(size - m_iconSize) < std::abs(bsize - m_iconSize)) {
        best = &image;
      }
    }

    if (best) {
      QImage image = *best;
      if (image.width() != m_iconSize || image.height() != m_iconSize) {
        image = image.scaled(m_iconSize, m_iconSize, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
      }
      return QPixmap::fromImage(image);
    }

    return QIcon::fromTheme("application-x-executable").pixmap(m_iconSize);
  }

  return QIcon(path).pixmap(m_iconSize);
}

// `ext` has the dot
QPixmap IconFetcher::linuxExtensionIcon(const QString& ext) const
{
  const auto noDot = QStringView{ext}.mid(1);

  if (const auto game = fileicons::gameFileIcon(noDot)) {
    for (const auto& name : game->themeNames) {
      if (QIcon::hasThemeIcon(name)) {
        return QIcon::fromTheme(name).pixmap(m_iconSize);
      }
    }

    if (!game->fallbackResource.isEmpty()) {
      const QIcon icon(game->fallbackResource);
      if (!icon.isNull()) {
        return icon.pixmap(m_iconSize);
      }
    }
  }

  for (const auto& name : fileicons::mimeIconNames(noDot)) {
    if (QIcon::hasThemeIcon(name)) {
      return QIcon::fromTheme(name).pixmap(m_iconSize);
    }
  }

  // no theme icon: the built-in one of the kind of the type (image, text, archive)
  if (const QString resource = fileicons::mimeFallbackResource(noDot);
      !resource.isEmpty()) {
    const QIcon icon(resource);
    if (!icon.isNull()) {
      return icon.pixmap(m_iconSize);
    }
  }

  return {};
}
#endif
