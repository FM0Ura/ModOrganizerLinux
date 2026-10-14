// Linux implementations of the Win32-only functions of envshell.cpp (the rest of
// envshell.cpp is shared).
//
// There is no shell context menu on Linux: the menu is a Qt menu with "Open" (with
// the default application of each file), "Open Folder" (the file manager, with
// MOBase::shell::Explore()) and "Copy Path" (the paths of the files, one per line).
// A collection shows one submenu per ShellMenu, after the details.

#include "../envshell.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMenu>

#include <uibase/log.h>
#include <uibase/utility.h>

using namespace MOBase;

namespace env
{

void ShellMenu::addActions(QMenu& menu)
{
  if (m_files.empty()) {
    menu.addAction(QObject::tr("No files"))->setEnabled(false);
    return;
  }

  // copies, the menu may outlive this object
  const auto files = m_files;

  QObject::connect(menu.addAction(QObject::tr("&Open")), &QAction::triggered, [files] {
    for (auto&& f : files) {
      const auto r = shell::Open(f.absoluteFilePath());
      if (!r) {
        log::error("can't open '{}', {}", f.absoluteFilePath(), r.toString());
      }
    }
  });

  QObject::connect(menu.addAction(QObject::tr("Open &Folder")), &QAction::triggered,
                   [files] {
                     for (auto&& f : files) {
                       const auto r = shell::Explore(f);
                       if (!r) {
                         log::error("can't open the folder of '{}', {}",
                                    f.absoluteFilePath(), r.toString());
                       }
                     }
                   });

  QObject::connect(menu.addAction(QObject::tr("&Copy Path")), &QAction::triggered,
                   [files] {
                     QStringList paths;
                     for (auto&& f : files) {
                       paths.push_back(f.absoluteFilePath());
                     }

                     QGuiApplication::clipboard()->setText(paths.join("\n"));
                   });
}

void ShellMenu::exec(const QPoint& pos)
{
  QMenu menu(m_mw);
  addActions(menu);
  menu.exec(pos);
}

void ShellMenuCollection::exec(const QPoint& pos)
{
  QMenu menu(m_mw);

  if (!m_details.empty()) {
    for (auto&& d : m_details) {
      menu.addAction(d)->setEnabled(false);
    }

    menu.addSeparator();
  }

  for (auto&& m : m_menus) {
    auto* sub = menu.addMenu(m.name);
    m.menu.addActions(*sub);
  }

  menu.exec(pos);
}

}  // namespace env
