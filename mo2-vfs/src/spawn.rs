//! Programs started for the application (`spawn` requests).

use crate::session::{ErrorCode, SessionError};
use std::collections::BTreeMap;
use std::fs::{self, OpenOptions};
use std::os::unix::fs::MetadataExt;
use std::os::unix::process::{CommandExt, ExitStatusExt};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, ExitStatus, Stdio};

fn failed(message: String, path: &Path) -> SessionError {
    SessionError {
        code: ErrorCode::SpawnFailed,
        message,
        path: Some(path.to_path_buf()),
    }
}

/// Starts `argv` in `cwd`, in a process group of its own (its pgid is its pid), with the
/// environment of this process plus `env`. Its standard input is `/dev/null`; its output
/// and errors go to `output` (truncated) or to `/dev/null`, never to the protocol.
/// `argv[0]` is looked up in the `PATH` of the final environment. `argv` is not empty.
pub fn start(
    argv: &[String],
    env: &BTreeMap<String, String>,
    cwd: &Path,
    output: Option<&Path>,
) -> Result<Child, SessionError> {
    let (program, args) = argv.split_first().expect("argv is not empty");
    if !cwd.is_dir() {
        return Err(failed(
            format!("the working directory {} does not exist", cwd.display()),
            cwd,
        ));
    }
    let mut command = Command::new(program);
    command
        .args(args)
        .envs(env)
        .current_dir(cwd)
        .stdin(Stdio::null())
        .process_group(0);
    match output {
        Some(path) => {
            let file = OpenOptions::new()
                .create(true)
                .write(true)
                .truncate(true)
                .open(path)
                .map_err(|e| failed(format!("cannot open {}: {e}", path.display()), path))?;
            let err = file
                .try_clone()
                .map_err(|e| failed(format!("cannot open {}: {e}", path.display()), path))?;
            command.stdout(file).stderr(err);
        }
        None => {
            command.stdout(Stdio::null()).stderr(Stdio::null());
        }
    }
    command
        .spawn()
        .map_err(|e| failed(format!("cannot start {program}: {e}"), Path::new(program)))
}

/// Sends `signal` to the process group `pgid`; a group that is gone is not an error.
pub fn signal_group(pgid: i32, signal: rustix::process::Signal) {
    let Some(pid) = rustix::process::Pid::from_raw(pgid) else {
        return;
    };
    match rustix::process::kill_process_group(pid, signal) {
        Ok(()) | Err(rustix::io::Errno::SRCH) => {}
        Err(e) => tracing::warn!(pgid, error = %e, "cannot signal the process group"),
    }
}

/// The exit code of a program, or 128 + the signal that killed it.
pub fn exit_code(status: ExitStatus) -> i32 {
    status
        .code()
        .unwrap_or_else(|| 128 + status.signal().unwrap_or(0))
}

/// The directory of the Wine server of a prefix (§5.3 of the Phase 3 spec), under `tmp`.
pub(crate) fn wineserver_dir_in(wineprefix: &Path, tmp: &Path) -> std::io::Result<PathBuf> {
    let pfx = wineprefix.join("pfx");
    let dir = if pfx.is_dir() {
        pfx
    } else {
        wineprefix.to_path_buf()
    };
    let md = fs::metadata(&dir)?;
    Ok(tmp
        .join(format!(".wine-{}", rustix::process::getuid().as_raw()))
        .join(format!("server-{:x}-{:x}", md.dev(), md.ino())))
}

/// `<tmp>/.wine-<uid>/server-<dev>-<ino>` of `<wineprefix>/pfx` (or of `wineprefix`
/// without pfx/), where the wineserver of that prefix runs; `<tmp>` is
/// `MO2_VFS_WINE_TMP` (tests) or `/tmp`, as Wine uses.
pub fn wineserver_dir(wineprefix: &Path) -> std::io::Result<PathBuf> {
    let tmp = std::env::var_os("MO2_VFS_WINE_TMP")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/tmp"));
    wineserver_dir_in(wineprefix, &tmp)
}

/// The pids of the live processes named `wineserver` whose working directory is
/// `server_dir` (readable for processes of the same user, also inside pressure-vessel).
pub fn wineservers(server_dir: &Path) -> Vec<i32> {
    let Ok(dir) = fs::read_dir("/proc") else {
        return Vec::new();
    };
    dir.flatten()
        .filter_map(|entry| {
            let pid: i32 = entry.file_name().to_str()?.parse().ok()?;
            let comm = fs::read_to_string(entry.path().join("comm")).ok()?;
            if comm.trim_end() != "wineserver" {
                return None;
            }
            let stat = fs::read_to_string(entry.path().join("stat")).ok()?;
            let state = stat[stat.rfind(')')? + 1..]
                .split_ascii_whitespace()
                .next()?;
            if state == "Z" {
                return None;
            }
            (fs::read_link(entry.path().join("cwd")).ok()? == server_dir).then_some(pid)
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::MetadataExt;

    fn expected(dir: &Path, tmp: &Path) -> PathBuf {
        let md = std::fs::metadata(dir).unwrap();
        tmp.join(format!(".wine-{}", rustix::process::getuid().as_raw()))
            .join(format!("server-{:x}-{:x}", md.dev(), md.ino()))
    }

    #[test]
    fn server_dir_of_a_proton_prefix_is_the_one_of_pfx() {
        let t = tempfile::tempdir().unwrap();
        let compat = t.path().join("compatdata/489830");
        std::fs::create_dir_all(compat.join("pfx")).unwrap();
        let got = wineserver_dir_in(&compat, t.path()).unwrap();
        assert_eq!(got, expected(&compat.join("pfx"), t.path()));
    }

    #[test]
    fn server_dir_of_a_plain_prefix_and_of_a_link() {
        let t = tempfile::tempdir().unwrap();
        let plain = t.path().join("wine");
        std::fs::create_dir_all(&plain).unwrap();
        assert_eq!(
            wineserver_dir_in(&plain, t.path()).unwrap(),
            expected(&plain, t.path())
        );
        // pfx as a link: stat follows it, like Wine
        let compat = t.path().join("compat");
        std::fs::create_dir_all(&compat).unwrap();
        std::os::unix::fs::symlink(&plain, compat.join("pfx")).unwrap();
        assert_eq!(
            wineserver_dir_in(&compat, t.path()).unwrap(),
            expected(&plain, t.path())
        );
        assert!(wineserver_dir_in(&t.path().join("missing"), t.path()).is_err());
    }

    #[test]
    fn wineservers_matches_name_and_working_directory() {
        let t = tempfile::tempdir().unwrap();
        let server = t.path().join("server-1-2");
        std::fs::create_dir_all(&server).unwrap();
        let fake = t.path().join("wineserver");
        std::fs::write(&fake, "#!/bin/sh\nsleep 5\n").unwrap();
        std::fs::set_permissions(&fake, std::os::unix::fs::PermissionsExt::from_mode(0o755))
            .unwrap();
        let mut child = Command::new(&fake).current_dir(&server).spawn().unwrap();
        let pid = child.id() as i32;
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
        while !wineservers(&server).contains(&pid) {
            assert!(
                std::time::Instant::now() < deadline,
                "fake wineserver not found"
            );
            std::thread::sleep(std::time::Duration::from_millis(20));
        }
        assert!(wineservers(&t.path().join("other")).is_empty());
        child.kill().unwrap();
        child.wait().unwrap();
        assert!(!wineservers(&server).contains(&pid));
    }
}
