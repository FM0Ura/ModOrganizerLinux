// Mod Organizer as the handler of nxm:// links (nxmhandler_linux.h, spec of Phase 4a,
// section 4).

#include "nxmhandler_linux.h"

#include "appimage_linux.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <uibase/log.h>

#include <memory>
#include <stdexcept>

namespace nxmhandler
{
namespace
{
  using Next = std::function<void(bool ok, const QString& out, const QString& error)>;

  // starts `c`; `next` gets its trimmed stdout when it exits with 0, or the cause of
  // the failure (could not start, exit code and stderr, crash)
  void run(const vfsbridge::Command& c, Next next)
  {
    auto* p          = new QProcess(QCoreApplication::instance());
    const auto shown = (QStringList{c.program} + c.arguments).join(' ');

    QObject::connect(
        p, &QProcess::errorOccurred, p, [p, next, shown](QProcess::ProcessError e) {
          // finished() reports the other errors
          if (e != QProcess::FailedToStart) {
            return;
          }
          next(false, {},
               QStringLiteral("cannot run %1: %2").arg(shown, p->errorString()));
          p->deleteLater();
        });

    QObject::connect(p, &QProcess::finished, p,
                     [p, next, shown](int code, QProcess::ExitStatus status) {
                       const auto out =
                           QString::fromUtf8(p->readAllStandardOutput()).trimmed();
                       const auto err =
                           QString::fromUtf8(p->readAllStandardError()).trimmed();

                       if (status != QProcess::NormalExit) {
                         next(false, {}, QStringLiteral("%1 crashed").arg(shown));
                       } else if (code != 0) {
                         next(false, {},
                              QStringLiteral("%1 exited with %2%3")
                                  .arg(shown)
                                  .arg(code)
                                  .arg(err.isEmpty() ? QString() : ": " + err));
                       } else {
                         next(true, out, {});
                       }

                       p->deleteLater();
                     });

    p->start(c.program, c.arguments);
  }

  // `done` from the event loop, never from inside the caller
  void finish(const Done& done, bool ok, const QString& message)
  {
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [done, ok, message] {
          done(ok, message);
        },
        Qt::QueuedConnection);
  }

  bool writeFile(const QString& path, const QString& content, QString& error)
  {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
      error = QStringLiteral("cannot create %1").arg(QFileInfo(path).absolutePath());
      return false;
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(content.toUtf8()) < 0 || !f.commit()) {
      error = QStringLiteral("cannot write %1: %2").arg(path, f.errorString());
      return false;
    }

    MOBase::log::debug("wrote {}", path);
    return true;
  }

  bool writeDesktopFile(const QString& content, QString& error)
  {
    return writeFile(desktopFilePath(), content, error);
  }

  // the desktop file of both handlers: only the name, the comment, the argument that
  // takes the link or file and the MIME type differ
  QString desktopFile(const Environment& env, const QString& name,
                      const QString& comment, const QString& arg,
                      const QString& mimeType, bool noDisplay)
  {
    QStringList quoted;
    for (const auto& a : launcher(env)) {
      quoted.append(quoteExecArgument(a));
    }

    // string escapes of the file (applied before the quoting rules when read)
    QString exec = quoted.join(' ') + " " + arg;
    exec.replace("\\", "\\\\")
        .replace("\n", "\\n")
        .replace("\t", "\\t")
        .replace("\r", "\\r");

    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=%1\n"
                          "Comment=%2\n"
                          "Exec=%3\n"
                          "Terminal=false\n"
                          "%4"
                          "MimeType=%5;\n")
        .arg(name, comment, exec,
             noDisplay ? QStringLiteral("NoDisplay=true\n") : QString(), mimeType);
  }

  // the registration of .mo2list files; never fails, `done` is called when it ends
  void registerMo2list(bool force, const std::function<void()>& done)
  {
    QString content;
    vfsbridge::Command query;

    try {
      content = mo2listDesktopFileContent(currentEnvironment());
      query   = mo2listQueryCommand();
    } catch (const std::exception& e) {
      MOBase::log::warn("cannot register Mod Organizer for .mo2list files: {}",
                        e.what());
      done();
      return;
    }

    // the query first, so that nothing is written on a host without xdg-mime
    run(query, [force, content, done](bool ok, const QString& current,
                                      const QString& queryError) {
      if (!ok) {
        MOBase::log::warn("cannot register Mod Organizer for .mo2list files: {}",
                          queryError);
        done();
        return;
      }

      QString error;
      if (!writeFile(mo2listDesktopFilePath(), content, error) ||
          !writeFile(mo2listMimePackagePath(), mo2listMimeXml(), error)) {
        MOBase::log::warn("cannot register Mod Organizer for .mo2list files: {}",
                          error);
        done();
        return;
      }

      // the default handler is set after the MIME database is updated, with the same
      // rule as nxm links (nothing to do when it is already ours)
      const bool setIt = decide(current, force) == Action::WriteFileAndSetDefault;
      if (!setIt) {
        MOBase::log::debug(".mo2list files are handled by {}, not changed", current);
      }

      auto setDefault = [setIt, done] {
        if (!setIt) {
          done();
          return;
        }

        vfsbridge::Command set;
        try {
          set = mo2listSetDefaultCommand();
        } catch (const std::exception& e) {
          MOBase::log::warn("cannot register Mod Organizer for .mo2list files: {}",
                            e.what());
          done();
          return;
        }

        run(set, [done](bool ok, const QString&, const QString& error) {
          if (!ok) {
            MOBase::log::warn("cannot register Mod Organizer for .mo2list files: {}",
                              error);
          }
          done();
        });
      };

      vfsbridge::Command update;
      try {
        update = updateMimeDatabaseCommand(mimeDir());
      } catch (const std::exception& e) {
        MOBase::log::warn("update-mime-database not run: {}", e.what());
        setDefault();
        return;
      }

      run(update, [setDefault](bool ok, const QString&, const QString& error) {
        if (!ok) {
          // the menu entry keeps working without it
          MOBase::log::warn("update-mime-database failed, .mo2list files may not be "
                            "recognized until the MIME database is updated: {}",
                            error);
        }
        setDefault();
      });
    });
  }

  // update-desktop-database (failure only logged), then `done`
  void updateDatabase(const Done& done)
  {
    vfsbridge::Command c;
    try {
      c = updateDatabaseCommand(applicationsDir());
    } catch (const std::exception& e) {
      MOBase::log::debug("update-desktop-database not run: {}", e.what());
      finish(done, true, {});
      return;
    }

    run(c, [done](bool ok, const QString&, const QString& error) {
      if (!ok) {
        MOBase::log::debug("{}", error);
      }
      done(true, {});
    });
  }
}  // namespace

Environment currentEnvironment()
{
  Environment e;
  e.appImage    = appimage::info().appImage;
  e.containerId = qEnvironmentVariable("CONTAINER_ID");
  e.inContainer = vfsbridge::mode() == vfsbridge::Mode::Host;
  e.executable  = QCoreApplication::applicationFilePath();
  return e;
}

QString quoteExecArgument(const QString& arg)
{
  static const QString reserved = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");

  QString escaped;
  bool quote = arg.isEmpty();

  for (const QChar c : arg) {
    if (reserved.contains(c)) {
      quote = true;
    }
    if (c == '"' || c == '`' || c == '$' || c == '\\') {
      escaped += '\\';
    }
    escaped += c;
  }

  escaped.replace("%", "%%");

  return quote ? '"' + escaped + '"' : escaped;
}

QStringList launcher(const Environment& env)
{
  if (!env.appImage.isEmpty()) {
    return {env.appImage};
  }

  if (env.inContainer) {
    if (env.containerId.isEmpty()) {
      throw std::runtime_error(
          "cannot register the nxm handler from this container: CONTAINER_ID is not "
          "set (only distrobox is supported)");
    }
    return {"distrobox", "enter", env.containerId, "--", env.executable};
  }

  return {env.executable};
}

QString desktopFileContent(const Environment& env)
{
  return desktopFile(env, QStringLiteral("Mod Organizer 2 (nxm links)"),
                     QStringLiteral("Download Nexus Mods links with Mod Organizer 2"),
                     QStringLiteral("%u"), QString::fromLatin1(MimeType), true);
}

QString mo2listDesktopFileContent(const Environment& env)
{
  return desktopFile(env, QStringLiteral("Mod Organizer 2 (modlists)"),
                     QStringLiteral("Import .mo2list modlists with Mod Organizer 2"),
                     QStringLiteral("%f"), QString::fromLatin1(Mo2listMimeType), false);
}

QString mo2listMimeXml()
{
  return QStringLiteral(
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
             "  <mime-type type=\"%1\">\n"
             "    <comment>Mod Organizer modlist</comment>\n"
             "    <glob pattern=\"*.mo2list\"/>\n"
             "  </mime-type>\n"
             "</mime-info>\n")
      .arg(QString::fromLatin1(Mo2listMimeType));
}

Action decide(const QString& currentDefault, bool force)
{
  if (force) {
    return Action::WriteFileAndSetDefault;
  }

  const auto current = currentDefault.trimmed();

  if (current.isEmpty()) {
    return Action::WriteFileAndSetDefault;
  }

  if (current == QLatin1String(DesktopFileName)) {
    return Action::WriteFile;
  }

  return Action::None;
}

QString applicationsDir()
{
  return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
         "/applications";
}

QString desktopFilePath()
{
  return applicationsDir() + "/" + QLatin1String(DesktopFileName);
}

QString mimeDir()
{
  return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
         "/mime";
}

QString mo2listMimePackagePath()
{
  return mimeDir() + "/packages/" + QLatin1String(Mo2listMimeFileName);
}

QString mo2listDesktopFilePath()
{
  return applicationsDir() + "/" + QLatin1String(Mo2listDesktopFileName);
}

vfsbridge::Command mo2listQueryCommand()
{
  return vfsbridge::helperCommand(
      "xdg-mime", {"query", "default", QString::fromLatin1(Mo2listMimeType)});
}

vfsbridge::Command mo2listSetDefaultCommand()
{
  return vfsbridge::helperCommand(
      "xdg-mime", {"default", QString::fromLatin1(Mo2listDesktopFileName),
                   QString::fromLatin1(Mo2listMimeType)});
}

vfsbridge::Command updateMimeDatabaseCommand(const QString& dir)
{
  return vfsbridge::helperCommand("update-mime-database",
                                  {vfsbridge::hostCanonicalPath(dir)});
}

vfsbridge::Command queryCommand()
{
  return vfsbridge::helperCommand("xdg-mime",
                                  {"query", "default", QString::fromLatin1(MimeType)});
}

vfsbridge::Command setDefaultCommand()
{
  return vfsbridge::helperCommand(
      "xdg-mime",
      {"default", QString::fromLatin1(DesktopFileName), QString::fromLatin1(MimeType)});
}

vfsbridge::Command updateDatabaseCommand(const QString& dir)
{
  return vfsbridge::helperCommand("update-desktop-database",
                                  {vfsbridge::hostCanonicalPath(dir)});
}

static void registerNxm(bool force, Done done)
{
  QString content;
  vfsbridge::Command query;

  try {
    content = desktopFileContent(currentEnvironment());
    query   = queryCommand();
  } catch (const std::exception& e) {
    finish(done, false, QString::fromStdString(e.what()));
    return;
  }

  run(query,
      [force, content, done](bool ok, const QString& current, const QString& error) {
        if (!ok) {
          done(false, error);
          return;
        }

        const auto action = decide(current, force);
        if (action == Action::None) {
          MOBase::log::debug("nxm links are handled by {}, not changed", current);
          done(true, {});
          return;
        }

        QString writeError;
        if (!writeDesktopFile(content, writeError)) {
          done(false, writeError);
          return;
        }

        if (action == Action::WriteFile) {
          done(true, {});
          return;
        }

        vfsbridge::Command set;
        try {
          set = setDefaultCommand();
        } catch (const std::exception& e) {
          done(false, QString::fromStdString(e.what()));
          return;
        }

        run(set, [done](bool ok, const QString&, const QString& error) {
          if (!ok) {
            done(false, error);
            return;
          }
          MOBase::log::debug("Mod Organizer is now the handler of nxm links");
          done(true, {});
        });
      });
}

void registerHandler(bool force, Done done)
{
  registerNxm(force, [force, done](bool ok, const QString& message) {
    if (!ok) {
      MOBase::log::warn("cannot register Mod Organizer for nxm links: {}", message);
    }

    // independent of the nxm result, which is what `done` gets
    registerMo2list(force, [done, ok, message] {
      // once, for both desktop files
      updateDatabase([done, ok, message](bool, const QString&) {
        done(ok, message);
      });
    });
  });
}

void dump()
{
  MOBase::log::debug("nxm handler:");

  try {
    const auto c = queryCommand();
    QProcess p;
    p.start(c.program, c.arguments);
    if (p.waitForFinished(5000) && p.exitStatus() == QProcess::NormalExit &&
        p.exitCode() == 0) {
      MOBase::log::debug(" . default: {}",
                         QString::fromUtf8(p.readAllStandardOutput()).trimmed());
    } else {
      MOBase::log::debug(" . default: (xdg-mime failed: {})", p.errorString());
    }
  } catch (const std::exception& e) {
    MOBase::log::debug(" . default: ({})", e.what());
  }

  QFile f(desktopFilePath());
  if (!f.open(QIODevice::ReadOnly)) {
    MOBase::log::debug(" . {}: not found", desktopFilePath());
    return;
  }

  for (const auto& line : QString::fromUtf8(f.readAll()).split('\n')) {
    if (line.startsWith("Exec=")) {
      MOBase::log::debug(" . {}: {}", desktopFilePath(), line);
    }
  }
}
}  // namespace nxmhandler
