//! Mount state: the files that let a later run clean up after a crashed one, and the list of
//! `fuse.mo2-vfs` mounts of the system.

use crate::session::unmount_path;
use rustix::process::{Pid, test_kill_process};
use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

pub const FSTYPE: &str = "fuse.mo2-vfs";

#[derive(Serialize, Deserialize, Debug, PartialEq, Eq)]
pub struct StateFile {
    pub pid: u32,
    pub roots: Vec<PathBuf>,
}

/// `$MO2_VFS_STATE_DIR` if set, else `$XDG_RUNTIME_DIR/mo2-vfs`.
pub fn state_dir() -> PathBuf {
    if let Some(d) = std::env::var_os("MO2_VFS_STATE_DIR").filter(|d| !d.is_empty()) {
        return PathBuf::from(d);
    }
    match std::env::var_os("XDG_RUNTIME_DIR").filter(|d| !d.is_empty()) {
        Some(d) => PathBuf::from(d).join("mo2-vfs"),
        // No runtime dir: a per-user directory under /tmp.
        None => PathBuf::from(format!(
            "/tmp/mo2-vfs-{}",
            rustix::process::getuid().as_raw()
        )),
    }
}

fn file_for(dir: &Path, pid: u32) -> PathBuf {
    dir.join(format!("state-{pid}.json"))
}

impl StateFile {
    pub fn write(pid: u32, roots: &[PathBuf]) -> io::Result<()> {
        Self::write_in(&state_dir(), pid, roots)
    }

    pub fn remove(pid: u32) {
        Self::remove_in(&state_dir(), pid)
    }

    pub fn write_in(dir: &Path, pid: u32, roots: &[PathBuf]) -> io::Result<()> {
        fs::create_dir_all(dir)?;
        let body = serde_json::to_vec(&StateFile {
            pid,
            roots: roots.to_vec(),
        })
        .map_err(io::Error::other)?;
        // Write then rename so a reader never sees half a file.
        let tmp = dir.join(format!(".state-{pid}.json.tmp"));
        fs::write(&tmp, body)?;
        fs::rename(&tmp, file_for(dir, pid))
    }

    pub fn remove_in(dir: &Path, pid: u32) {
        match fs::remove_file(file_for(dir, pid)) {
            Ok(()) => {}
            Err(e) if e.kind() == io::ErrorKind::NotFound => {}
            Err(e) => tracing::warn!(error = %e, "cannot remove the state file"),
        }
    }
}

fn unescape(field: &str) -> PathBuf {
    use std::os::unix::ffi::OsStringExt;
    let b = field.as_bytes();
    let mut out = Vec::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        if b[i] == b'\\'
            && i + 3 < b.len()
            && b[i + 1..i + 4].iter().all(|c| (b'0'..=b'7').contains(c))
        {
            let v = (b[i + 1] - b'0') as u32 * 64
                + (b[i + 2] - b'0') as u32 * 8
                + (b[i + 3] - b'0') as u32;
            out.push(v as u8);
            i += 4;
        } else {
            out.push(b[i]);
            i += 1;
        }
    }
    PathBuf::from(std::ffi::OsString::from_vec(out))
}

/// Mount points of the `fuse.mo2-vfs` entries in a `mountinfo` text.
pub fn parse_mountinfo(text: &str) -> Vec<PathBuf> {
    text.lines()
        .filter_map(|line| {
            let (pre, post) = line.split_once(" - ")?;
            (post.split_whitespace().next()? == FSTYPE).then_some(())?;
            pre.split_whitespace().nth(4).map(unescape)
        })
        .collect()
}

/// Mount points of every `fuse.mo2-vfs` mount of this mount namespace.
pub fn mounted_mo2_vfs() -> Vec<PathBuf> {
    match fs::read_to_string("/proc/self/mountinfo") {
        Ok(t) => parse_mountinfo(&t),
        Err(e) => {
            tracing::warn!(error = %e, "cannot read /proc/self/mountinfo");
            Vec::new()
        }
    }
}

fn pid_alive(pid: u32) -> bool {
    let Some(p) = i32::try_from(pid).ok().and_then(Pid::from_raw) else {
        return false;
    };
    match test_kill_process(p) {
        Ok(()) => true,
        Err(e) => e != rustix::io::Errno::SRCH,
    }
}

pub fn cleanup_stale() -> Vec<(PathBuf, Result<(), String>)> {
    cleanup_stale_in(&state_dir())
}

/// Unmounts the roots left by dead processes (state files whose pid no longer exists). Only
/// roots that are `fuse.mo2-vfs` mounts right now are touched. A state file is removed once
/// every one of its roots was handled.
pub fn cleanup_stale_in(dir: &Path) -> Vec<(PathBuf, Result<(), String>)> {
    let mut results = Vec::new();
    let Ok(entries) = fs::read_dir(dir) else {
        return results;
    };
    for entry in entries.flatten() {
        let name = entry.file_name();
        let name = name.to_string_lossy();
        if !(name.starts_with("state-") && name.ends_with(".json")) {
            continue;
        }
        let path = entry.path();
        let Ok(state) = fs::read(&path)
            .map_err(|e| e.to_string())
            .and_then(|b| serde_json::from_slice::<StateFile>(&b).map_err(|e| e.to_string()))
        else {
            tracing::warn!(file = %path.display(), "unreadable state file, leaving it");
            continue;
        };
        if pid_alive(state.pid) {
            continue;
        }
        let mounted = mounted_mo2_vfs();
        let mut all_ok = true;
        for root in state.roots {
            let r = if mounted.contains(&root) {
                unmount_path(&root)
            } else {
                Ok(())
            };
            all_ok &= r.is_ok();
            results.push((root, r));
        }
        if all_ok {
            let _ = fs::remove_file(&path);
        }
    }
    results
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mountinfo_parsing() {
        let text = "\
36 35 98:0 /mnt1 /mnt/with\\040space rw,noatime master:1 - fuse.mo2-vfs mo2-vfs rw,user_id=1000
37 35 0:41 / /other rw - tmpfs tmpfs rw
38 35 0:42 / /x/Data rw - fuse.sshfs host:/ rw
39 35 0:43 / /y/Saves rw shared:3 - fuse.mo2-vfs mo2-vfs rw
";
        assert_eq!(
            parse_mountinfo(text),
            vec![PathBuf::from("/mnt/with space"), PathBuf::from("/y/Saves")]
        );
    }

    #[test]
    fn state_file_roundtrip() {
        let d = tempfile::tempdir().unwrap();
        StateFile::write_in(d.path(), 7, &[PathBuf::from("/a")]).unwrap();
        let v: StateFile =
            serde_json::from_slice(&fs::read(d.path().join("state-7.json")).unwrap()).unwrap();
        assert_eq!(v.pid, 7);
        StateFile::remove_in(d.path(), 7);
        StateFile::remove_in(d.path(), 7);
        assert!(!d.path().join("state-7.json").exists());
    }

    #[test]
    fn own_pid_is_alive_and_absurd_pid_is_not() {
        assert!(pid_alive(std::process::id()));
        assert!(!pid_alive(u32::MAX));
    }
}
