// Linux implementations of the Win32-only functions of envmetrics.cpp (the rest of
// envmetrics.cpp is shared): the displays are the screens of Qt
// (QGuiApplication::screens()), identified by their name; the dpi is the logical dpi
// of the screen and the resolution is in physical pixels.

#include "../envmetrics.h"

#include <QGuiApplication>
#include <QScreen>

namespace env
{

namespace
{

  QScreen* findScreen(const QString& name)
  {
    for (auto* s : QGuiApplication::screens()) {
      if (s->name() == name) {
        return s;
      }
    }

    return nullptr;
  }

}  // namespace

int getDpi(const QString& monitorDevice)
{
  if (auto* s = findScreen(monitorDevice)) {
    return qRound(s->logicalDotsPerInch());
  }

  return 0;
}

void Display::getSettings()
{
  auto* s = findScreen(m_monitorDevice);
  if (!s) {
    return;
  }

  m_resX        = qRound(s->size().width() * s->devicePixelRatio());
  m_resY        = qRound(s->size().height() * s->devicePixelRatio());
  m_refreshRate = qRound(s->refreshRate());
}

void Metrics::getDisplays()
{
  const auto* primary = QGuiApplication::primaryScreen();

  for (auto* s : QGuiApplication::screens()) {
    // "manufacturer model", may be empty
    QString adapter = (s->manufacturer() + " " + s->model()).trimmed();
    if (adapter.isEmpty()) {
      adapter = s->name();
    }

    m_displays.emplace_back(adapter, s->name(), (s == primary));
  }
}

}  // namespace env
