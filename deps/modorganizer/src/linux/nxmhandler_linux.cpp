// Mod Organizer as the handler of nxm:// links (nxmhandler_linux.h, spec of Phase 4a,
// section 4).

#include "nxmhandler_linux.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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

  bool writeDesktopFile(const QString& content, QString& error)
  {
    if (!QDir().mkpath(applicationsDir())) {
      error = QStringLiteral("cannot create %1").arg(applicationsDir());
      return false;
    }

    QSaveFile f(desktopFilePath());
    if (!f.open(QIODevice::WriteOnly) || f.write(content.toUtf8()) < 0 || !f.commit()) {
      error =
          QStringLiteral("cannot write %1: %2").arg(desktopFilePath(), f.errorString());
      return false;
    }

    MOBase::log::debug("wrote {}", desktopFilePath());
    return true;
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
  e.appImage    = qEnvironmentVariable("APPIMAGE");
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
  QStringList quoted;
  for (const auto& a : launcher(env)) {
    quoted.append(quoteExecArgument(a));
  }

  // string escapes of the file (applied before the quoting rules when read)
  QString exec = quoted.join(' ') + " %u";
  exec.replace("\\", "\\\\")
      .replace("\n", "\\n")
      .replace("\t", "\\t")
      .replace("\r", "\\r");

  return QStringLiteral("[Desktop Entry]\n"
                        "Type=Application\n"
                        "Name=Mod Organizer 2 (nxm links)\n"
                        "Comment=Download Nexus Mods links with Mod Organizer 2\n"
                        "Exec=%1\n"
                        "Terminal=false\n"
                        "NoDisplay=true\n"
                        "MimeType=%2;\n")
      .arg(exec, QString::fromLatin1(MimeType));
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

void registerHandler(bool force, Done done)
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
          updateDatabase(done);
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
          updateDatabase(done);
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
