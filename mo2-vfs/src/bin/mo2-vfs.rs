//! `mo2-vfs`: line protocol on stdin/stdout, or `--cleanup` to unmount what a dead process
//! left behind.

use mo2_vfs::ipc::{Event, Server, parse_request};
use mo2_vfs::state;
use serde_json::json;
use std::io::{BufRead, Write};
use std::path::PathBuf;
use std::process::ExitCode;
use std::sync::mpsc::{self, RecvTimeoutError};
use std::time::Duration;

const POLL: Duration = Duration::from_millis(500);

enum Msg {
    Line(String),
    Eof,
    Signal(i32),
}

struct Args {
    log: Option<PathBuf>,
    level: String,
    cleanup: bool,
}

fn parse_args() -> Result<Args, String> {
    let mut args = Args {
        log: None,
        level: "info".to_string(),
        cleanup: false,
    };
    let mut it = std::env::args().skip(1);
    while let Some(a) = it.next() {
        match a.as_str() {
            "--log" => args.log = Some(it.next().ok_or("--log needs a file")?.into()),
            "--log-level" => {
                let l = it.next().ok_or("--log-level needs a level")?;
                if !["trace", "debug", "info", "warn", "error"].contains(&l.as_str()) {
                    return Err(format!("invalid log level {l:?}"));
                }
                args.level = l;
            }
            "--cleanup" => args.cleanup = true,
            other => return Err(format!("unknown argument {other:?}")),
        }
    }
    Ok(args)
}

/// Logs go to the `--log` file, or to stderr without it; never to stdout.
fn init_logging(args: &Args) {
    let filter = tracing_subscriber::EnvFilter::new(&args.level);
    let builder = tracing_subscriber::fmt()
        .with_env_filter(filter)
        .with_ansi(false);
    match &args.log {
        Some(path) => {
            let dir = path
                .parent()
                .filter(|d| !d.as_os_str().is_empty())
                .unwrap_or(std::path::Path::new("."));
            match path.file_name() {
                Some(name) => {
                    builder
                        .with_writer(tracing_appender::rolling::never(dir, name))
                        .init();
                }
                None => builder.with_writer(std::io::stderr).init(),
            }
        }
        None => builder.with_writer(std::io::stderr).init(),
    }
}

fn emit(event: &Event) {
    let mut out = std::io::stdout().lock();
    let line = serde_json::to_string(event).expect("events serialize");
    match writeln!(out, "{line}").and_then(|()| out.flush()) {
        Ok(()) => {}
        // the application closed its end (it exited or crashed): nothing to report
        Err(e) if e.kind() == std::io::ErrorKind::BrokenPipe => {
            tracing::debug!("stdout is closed, event dropped: {line}")
        }
        Err(e) => tracing::warn!(error = %e, "cannot write to stdout"),
    }
}

fn cleanup() -> ExitCode {
    let mut all_ok = true;
    for (root, result) in state::cleanup_stale() {
        let line = match result {
            Ok(()) => json!({"root": root, "ok": true}),
            Err(e) => {
                all_ok = false;
                json!({"root": root, "ok": false, "error": e})
            }
        };
        println!("{line}");
    }
    if all_ok {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}

fn shutdown(server: &mut Server) {
    if let Err(e) = server.unmount() {
        tracing::error!(error = %e, "unmount failed");
    }
}

/// SIGHUP (the terminal of the application closed) is ignored. SIGINT and SIGTERM
/// unmount and exit at once when no watched process group is alive; otherwise the mounts
/// stay until the last group ends, as after the end of stdin, and the process exits then:
/// a program started by the application keeps its files.
fn serve() -> ExitCode {
    use signal_hook::consts::{SIGHUP, SIGINT, SIGTERM};
    // a session of its own whoever started it (QProcess with setsid() in the child, or
    // host-spawn through distrobox-host-exec): the signals of a terminal don't reach it;
    // fails harmlessly when it already leads a session or a process group
    if let Err(e) = rustix::process::setsid() {
        tracing::debug!(error = %e, "setsid");
    }
    let (tx, rx) = mpsc::channel();
    match signal_hook::iterator::Signals::new([SIGTERM, SIGINT, SIGHUP]) {
        Ok(mut signals) => {
            let tx = tx.clone();
            std::thread::spawn(move || {
                for sig in signals.forever() {
                    if tx.send(Msg::Signal(sig)).is_err() {
                        break;
                    }
                }
            });
        }
        Err(e) => tracing::warn!(error = %e, "cannot install signal handlers"),
    }
    std::thread::spawn(move || {
        for line in std::io::stdin().lock().lines() {
            match line {
                Ok(l) => {
                    if tx.send(Msg::Line(l)).is_err() {
                        return;
                    }
                }
                Err(_) => break,
            }
        }
        let _ = tx.send(Msg::Eof);
    });

    emit(&Event::Hello {
        version: env!("CARGO_PKG_VERSION").to_string(),
    });
    let mut server = Server::new();
    // stdin ended or a termination signal came: exit once no watched group is left
    let mut ending = false;
    loop {
        match rx.recv_timeout(POLL) {
            Ok(Msg::Line(line)) => {
                if !line.trim().is_empty() {
                    let event = match parse_request(&line) {
                        Ok(request) => server.handle(request),
                        Err(event) => event,
                    };
                    emit(&event);
                }
            }
            Ok(Msg::Eof) | Err(RecvTimeoutError::Disconnected) => {
                ending = true;
                tracing::info!("stdin closed");
            }
            Ok(Msg::Signal(SIGHUP)) => tracing::info!("SIGHUP ignored"),
            Ok(Msg::Signal(sig)) => {
                if !server.has_watched() {
                    tracing::info!(signal = sig, "termination signal");
                    shutdown(&mut server);
                    return ExitCode::SUCCESS;
                }
                tracing::warn!(
                    signal = sig,
                    "termination signal while watched process groups are alive: the \
                     virtual file system stays mounted until they end, then mo2-vfs exits"
                );
                ending = true;
            }
            Err(RecvTimeoutError::Timeout) => {}
        }
        for event in server.poll_children() {
            emit(&event);
        }
        if let Some(event) = server.poll_groups() {
            // a program that ended after the poll above is a zombie, which counts as a dead
            // group: reap it again so its `exited` comes before this event
            for exited in server.poll_children() {
                emit(&exited);
            }
            emit(&event);
        }
        if ending && !server.has_watched() {
            shutdown(&mut server);
            return ExitCode::SUCCESS;
        }
    }
}

fn main() -> ExitCode {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("mo2-vfs: {e}");
            eprintln!(
                "usage: mo2-vfs [--log <file>] [--log-level trace|debug|info|warn|error] | --cleanup"
            );
            return ExitCode::from(2);
        }
    };
    init_logging(&args);
    if args.cleanup { cleanup() } else { serve() }
}
