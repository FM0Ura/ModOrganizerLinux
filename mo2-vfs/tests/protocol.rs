mod common;

use common::{require_fuse, tempdir};
use serde_json::{Value, json};
use std::fs;
use std::io::{BufRead, BufReader, Write};
use std::os::unix::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError};
use std::thread;
use std::time::{Duration, Instant};

const EVENT_TIMEOUT: Duration = Duration::from_secs(10);

fn is_mounted(p: &Path) -> bool {
    mo2_vfs::state::mounted_mo2_vfs().iter().any(|m| m == p)
}

fn wait_until(what: &str, timeout: Duration, mut cond: impl FnMut() -> bool) {
    let deadline = Instant::now() + timeout;
    while !cond() {
        if Instant::now() > deadline {
            panic!("timed out after {timeout:?} waiting for {what}");
        }
        thread::sleep(Duration::from_millis(20));
    }
}

/// Layers under a tempdir: `Data` is the original (the mountpoint), `modA`/`modB` are mods.
struct Env {
    _tmp: tempfile::TempDir,
    root: PathBuf,
    state: PathBuf,
}

impl Env {
    fn new() -> Env {
        let tmp = tempdir();
        let root = tmp.path().canonicalize().unwrap();
        let env = Env {
            state: root.join("state"),
            root,
            _tmp: tmp,
        };
        env.write("Data/base.txt", "base");
        env.write("modA/a.txt", "a");
        env.write("modB/b.txt", "b");
        env.write("modB/sub/c.txt", "c");
        env
    }

    fn write(&self, rel: &str, content: &str) {
        let p = self.root.join(rel);
        fs::create_dir_all(p.parent().unwrap()).unwrap();
        fs::write(p, content).unwrap();
    }

    fn p(&self, rel: &str) -> PathBuf {
        self.root.join(rel)
    }

    fn mount_request(&self, mods: &[&str]) -> Value {
        let mappings: Vec<Value> = mods
            .iter()
            .map(|m| {
                json!({
                    "source": self.p(m),
                    "destination": self.p("Data"),
                    "is_directory": true,
                    "create_target": false,
                })
            })
            .collect();
        json!({"op": "mount", "mappings": mappings})
    }

    fn command(&self) -> Command {
        let mut c = Command::new(env!("CARGO_BIN_EXE_mo2-vfs"));
        c.env("MO2_VFS_STATE_DIR", &self.state);
        c
    }
}

struct Client {
    child: Child,
    stdin: Option<ChildStdin>,
    events: Receiver<String>,
}

impl Client {
    fn start(env: &Env) -> Client {
        Client::start_with(env.command())
    }

    fn start_with(mut command: Command) -> Client {
        let mut child = command
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .expect("spawn mo2-vfs");
        let stdin = child.stdin.take();
        let stdout = child.stdout.take().unwrap();
        let (tx, events) = mpsc::channel();
        thread::spawn(move || {
            for line in BufReader::new(stdout).lines() {
                match line {
                    Ok(l) => {
                        if tx.send(l).is_err() {
                            break;
                        }
                    }
                    Err(_) => break,
                }
            }
        });
        let mut c = Client {
            child,
            stdin,
            events,
        };
        let hello = c.next_event();
        assert_eq!(hello["event"], "hello", "{hello}");
        assert_eq!(hello["version"], env!("CARGO_PKG_VERSION"));
        c
    }

    fn send(&mut self, v: Value) {
        self.send_raw(&v.to_string());
    }

    fn send_raw(&mut self, line: &str) {
        let stdin = self.stdin.as_mut().expect("stdin already closed");
        writeln!(stdin, "{line}").expect("write request");
        stdin.flush().unwrap();
    }

    fn close_stdin(&mut self) {
        self.stdin = None;
    }

    fn try_event(&mut self, timeout: Duration) -> Option<Value> {
        match self.events.recv_timeout(timeout) {
            Ok(line) => Some(
                serde_json::from_str(&line)
                    .unwrap_or_else(|e| panic!("stdout line is not JSON ({e}): {line:?}")),
            ),
            Err(RecvTimeoutError::Timeout) => None,
            Err(RecvTimeoutError::Disconnected) => panic!("mo2-vfs closed stdout"),
        }
    }

    fn next_event(&mut self) -> Value {
        self.try_event(EVENT_TIMEOUT)
            .unwrap_or_else(|| panic!("no event within {EVENT_TIMEOUT:?}"))
    }

    fn request(&mut self, v: Value) -> Value {
        self.send(v);
        self.next_event()
    }

    fn wait_exit(&mut self, timeout: Duration) -> i32 {
        let deadline = Instant::now() + timeout;
        loop {
            if let Some(st) = self.child.try_wait().unwrap() {
                return st.code().expect("mo2-vfs killed by a signal");
            }
            if Instant::now() > deadline {
                panic!("mo2-vfs did not exit within {timeout:?}");
            }
            thread::sleep(Duration::from_millis(20));
        }
    }
}

impl Drop for Client {
    fn drop(&mut self) {
        self.stdin = None;
        if self.child.try_wait().ok().flatten().is_none() {
            kill_term(self.child.id());
            let deadline = Instant::now() + Duration::from_secs(5);
            while self.child.try_wait().ok().flatten().is_none() && Instant::now() < deadline {
                thread::sleep(Duration::from_millis(20));
            }
            let _ = self.child.kill();
            let _ = self.child.wait();
        }
    }
}

/// Unmounts what a failed test may have left behind.
struct MountGuard(PathBuf);

impl Drop for MountGuard {
    fn drop(&mut self) {
        if is_mounted(&self.0) {
            let _ = mo2_vfs::session::unmount_path(&self.0);
        }
    }
}

fn sleeper(secs: u32) -> Child {
    Command::new("sleep")
        .arg(secs.to_string())
        .process_group(0)
        .spawn()
        .expect("spawn sleep")
}

#[test]
fn hello_then_mount_ready() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    let ev = c.request(env.mount_request(&["modA"]));
    assert_eq!(ev["event"], "ready", "{ev}");
    assert_eq!(ev["roots"], json!([env.p("Data")]));
    assert_eq!(fs::read_to_string(env.p("Data/a.txt")).unwrap(), "a");
    assert_eq!(fs::read_to_string(env.p("Data/base.txt")).unwrap(), "base");
    let ev = c.request(json!({"op": "unmount"}));
    assert_eq!(ev, json!({"event": "unmounted"}));
    assert!(!is_mounted(&env.p("Data")));
}

#[test]
fn update_and_status() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    let mut req = env.mount_request(&["modA"]);
    req["skip_file_suffixes"] = json!([".bak"]);
    req["skip_directories"] = json!(["skipme"]);
    req["unknown_field"] = json!(1);
    assert_eq!(c.request(req)["event"], "ready");
    let st1 = c.request(json!({"op": "status"}));
    assert_eq!(st1["event"], "status", "{st1}");
    assert_eq!(st1["roots"], json!([env.p("Data")]));
    assert_eq!(st1["groups"], json!([]));
    let nodes1 = st1["nodes"].as_u64().unwrap();
    assert!(st1["masks"].is_u64());

    let ev = c.request(env.mount_request(&["modA", "modB"]).tap_op("update"));
    assert_eq!(ev, json!({"event": "ok"}));
    assert_eq!(fs::read_to_string(env.p("Data/sub/c.txt")).unwrap(), "c");
    let st2 = c.request(json!({"op": "status"}));
    assert!(st2["nodes"].as_u64().unwrap() > nodes1, "{st1} then {st2}");
    c.request(json!({"op": "unmount"}));
}

trait TapOp {
    fn tap_op(self, op: &str) -> Value;
}

impl TapOp for Value {
    fn tap_op(mut self, op: &str) -> Value {
        self["op"] = json!(op);
        self
    }
}

#[test]
fn error_bad_mapping() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    // a relative destination is rejected by the mapping planner
    let ev = c.request(json!({"op": "mount", "mappings": [
        {"source": env.p("modA"), "destination": "relative/Data",
         "is_directory": true, "create_target": false}
    ]}));
    assert_eq!(ev["event"], "error", "{ev}");
    assert_eq!(ev["code"], "bad_mapping", "{ev}");
    assert!(ev["message"].is_string());
    // the process keeps answering, and a good mount still works
    let ev = c.request(env.mount_request(&["modA"]));
    assert_eq!(ev["event"], "ready", "{ev}");
    c.request(json!({"op": "unmount"}));
}

#[test]
fn auto_unmount_when_watched_group_ends() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    let child = sleeper(1);
    let pgid = child.id() as i32;
    let ev = c.request(json!({"op": "watch", "pgid": pgid}));
    assert_eq!(ev, json!({"event": "ok"}));
    let st = c.request(json!({"op": "status"}));
    assert_eq!(st["groups"], json!([pgid]));
    let started = Instant::now();
    let ev = c.try_event(Duration::from_secs(3)).expect("no unmounted");
    assert_eq!(ev, json!({"event": "unmounted"}));
    assert!(started.elapsed() < Duration::from_secs(3));
    assert!(!is_mounted(&env.p("Data")));
    // the process keeps serving: groups were cleared, a new mount works
    let st = c.request(json!({"op": "status"}));
    assert_eq!(st["groups"], json!([]));
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    c.request(json!({"op": "unmount"}));
    drop(child);
}

#[test]
fn zombie_does_not_hold_mount() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    // exits at once and is NOT reaped: it stays a zombie until wait()
    let mut child = Command::new("sh")
        .args(["-c", "exit 0"])
        .process_group(0)
        .spawn()
        .unwrap();
    let pgid = child.id() as i32;
    wait_until(
        "the child to become a zombie",
        Duration::from_secs(5),
        || {
            fs::read_to_string(format!("/proc/{pgid}/stat"))
                .map(|s| {
                    s.rsplit(')')
                        .next()
                        .unwrap_or("")
                        .trim_start()
                        .starts_with('Z')
                })
                .unwrap_or(false)
        },
    );
    assert!(!mo2_vfs::watch::group_alive(pgid));
    assert_eq!(
        c.request(json!({"op": "watch", "pgid": pgid})),
        json!({"event": "ok"})
    );
    let ev = c.try_event(Duration::from_secs(3)).expect("no unmounted");
    assert_eq!(ev, json!({"event": "unmounted"}));
    assert!(!is_mounted(&env.p("Data")));
    child.wait().unwrap(); // reaped only now
}

#[test]
fn stdin_eof_without_group_unmounts_and_exits() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    c.close_stdin();
    assert_eq!(c.wait_exit(Duration::from_secs(10)), 0);
    assert!(!is_mounted(&env.p("Data")));
    assert!(
        !env.state
            .join(format!("state-{}.json", c.child.id()))
            .exists()
    );
}

#[test]
fn stdin_eof_with_live_group_keeps_mount_until_group_ends() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    let child = sleeper(2);
    let started = Instant::now();
    c.request(json!({"op": "watch", "pgid": child.id() as i32}));
    c.close_stdin();
    thread::sleep(Duration::from_millis(1000));
    assert!(
        is_mounted(&env.p("Data")),
        "mount dropped while the group lives"
    );
    assert!(c.child.try_wait().unwrap().is_none(), "exited early");
    assert_eq!(c.wait_exit(Duration::from_secs(10)), 0);
    assert!(started.elapsed() >= Duration::from_millis(1800));
    assert!(!is_mounted(&env.p("Data")));
    drop(child);
}

#[test]
fn kill9_then_cleanup() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    let state_file = env.state.join(format!("state-{}.json", c.child.id()));
    assert!(state_file.exists());
    c.child.kill().unwrap(); // SIGKILL
    c.child.wait().unwrap();
    let err = fs::metadata(env.p("Data")).unwrap_err();
    assert_eq!(err.raw_os_error(), Some(libc::ENOTCONN), "{err}");
    assert!(is_mounted(&env.p("Data")));

    let out = env.command().arg("--cleanup").output().unwrap();
    assert!(out.status.success(), "{out:?}");
    let lines: Vec<Value> = String::from_utf8(out.stdout)
        .unwrap()
        .lines()
        .map(|l| serde_json::from_str(l).unwrap())
        .collect();
    assert_eq!(lines, vec![json!({"root": env.p("Data"), "ok": true})]);
    assert!(!is_mounted(&env.p("Data")));
    assert!(!state_file.exists());
    assert_eq!(fs::read_to_string(env.p("Data/base.txt")).unwrap(), "base");
}

#[test]
fn cleanup_with_nothing_to_do_exits_zero() {
    let env = Env::new();
    let out = env.command().arg("--cleanup").output().unwrap();
    assert!(out.status.success());
    assert!(out.stdout.is_empty());
}

#[test]
fn unknown_op_is_error_not_crash() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let ev = c.request(json!({"op": "frobnicate"}));
    assert_eq!(ev["event"], "error", "{ev}");
    assert_eq!(ev["code"], "bad_request");
    c.send_raw("this is not json");
    let ev = c.next_event();
    assert_eq!(ev["code"], "bad_request", "{ev}");
    // update/watch need a mount; unmount is idempotent
    assert_eq!(
        c.request(json!({"op": "update", "mappings": []}))["code"],
        "bad_request"
    );
    assert_eq!(
        c.request(json!({"op": "watch", "pgid": 1}))["code"],
        "bad_request"
    );
    assert_eq!(
        c.request(json!({"op": "unmount"})),
        json!({"event": "unmounted"})
    );
    let st = c.request(json!({"op": "status"}));
    assert_eq!(st["event"], "status");
    assert_eq!(st["roots"], json!([]));
    assert_eq!(st["nodes"], 0);
}

#[test]
fn mount_twice_is_bad_request() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    let ev = c.request(env.mount_request(&["modA"]));
    assert_eq!(ev["code"], "bad_request", "{ev}");
    c.request(json!({"op": "unmount"}));
}

#[test]
fn sigterm_unmounts_and_exits() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    kill_term(c.child.id());
    assert_eq!(c.wait_exit(Duration::from_secs(10)), 0);
    assert!(!is_mounted(&env.p("Data")));
}

#[test]
fn sigint_without_group_unmounts_and_exits() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    kill_signal(c.child.id(), rustix::process::Signal::INT);
    assert_eq!(c.wait_exit(Duration::from_secs(10)), 0);
    assert!(!is_mounted(&env.p("Data")));
}

/// SIGINT/SIGTERM while a watched group lives: the mount stays until the group ends, and
/// the process exits then (stdin still open).
fn signal_with_live_group_keeps_mount(sig: rustix::process::Signal) {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    let child = sleeper(2);
    let started = Instant::now();
    c.request(json!({"op": "watch", "pgid": child.id() as i32}));
    kill_signal(c.child.id(), sig);
    thread::sleep(Duration::from_millis(1000));
    assert!(
        is_mounted(&env.p("Data")),
        "mount dropped on {sig:?} while the group lives"
    );
    assert_eq!(fs::read_to_string(env.p("Data/a.txt")).unwrap(), "a");
    assert!(c.child.try_wait().unwrap().is_none(), "exited early");
    let ev = c.try_event(Duration::from_secs(5)).expect("no unmounted");
    assert_eq!(ev, json!({"event": "unmounted"}));
    assert_eq!(c.wait_exit(Duration::from_secs(10)), 0);
    assert!(started.elapsed() >= Duration::from_millis(1800));
    assert!(!is_mounted(&env.p("Data")));
    drop(child);
}

#[test]
fn sigint_with_live_group_keeps_mount_until_group_ends() {
    signal_with_live_group_keeps_mount(rustix::process::Signal::INT);
}

#[test]
fn sigterm_with_live_group_keeps_mount_until_group_ends() {
    signal_with_live_group_keeps_mount(rustix::process::Signal::TERM);
}

#[test]
fn sighup_is_ignored() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    kill_signal(c.child.id(), rustix::process::Signal::HUP);
    thread::sleep(Duration::from_millis(700));
    assert!(
        c.child.try_wait().unwrap().is_none(),
        "SIGHUP ended mo2-vfs"
    );
    assert!(is_mounted(&env.p("Data")));
    let st = c.request(json!({"op": "status"}));
    assert_eq!(st["roots"], json!([env.p("Data")]), "{st}");
    assert_eq!(
        c.request(json!({"op": "unmount"})),
        json!({"event": "unmounted"})
    );
    assert!(!is_mounted(&env.p("Data")));
}

/// The real fusermount3, from PATH.
fn real_fusermount() -> PathBuf {
    std::env::split_paths(&std::env::var_os("PATH").unwrap_or_default())
        .map(|d| d.join("fusermount3"))
        .find(|p| p.is_file())
        .expect("fusermount3 in PATH")
}

#[test]
fn failed_auto_unmount_is_an_error_and_is_retried() {
    if !require_fuse() {
        return;
    }
    use std::os::unix::fs::PermissionsExt;
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    // a fusermount3 whose unmounts fail while the flag file exists
    let bin = env.p("bin");
    let flag = env.p("fail-unmount");
    fs::create_dir_all(&bin).unwrap();
    let fake = bin.join("fusermount3");
    fs::write(
        &fake,
        format!(
            "#!/bin/sh\ncase \"$1\" in -u*) [ -e '{}' ] && {{ echo 'fake failure' >&2; exit 1; }};; esac\nexec '{}' \"$@\"\n",
            flag.display(),
            real_fusermount().display()
        ),
    )
    .unwrap();
    fs::set_permissions(&fake, fs::Permissions::from_mode(0o755)).unwrap();
    let path = std::env::join_paths(std::iter::once(bin.clone()).chain(std::env::split_paths(
        &std::env::var_os("PATH").unwrap_or_default(),
    )))
    .unwrap();

    let mut cmd = env.command();
    cmd.env("PATH", path);
    let mut c = Client::start_with(cmd);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    let child = sleeper(1);
    c.request(json!({"op": "watch", "pgid": child.id() as i32}));
    fs::write(&flag, "").unwrap();

    // not `unmounted`: the mount is still there
    let ev = c.try_event(Duration::from_secs(10)).expect("no event");
    assert_eq!(ev["event"], "error", "{ev}");
    assert_eq!(ev["code"], "io", "{ev}");
    assert_eq!(ev["spontaneous"], true, "{ev}");
    assert_eq!(ev["path"], json!(env.p("Data")), "{ev}");
    assert!(
        ev["message"].as_str().unwrap().contains("fake failure"),
        "{ev}"
    );
    assert!(is_mounted(&env.p("Data")));
    // the state file still lists the root, for --cleanup after a crash
    let state_file = env.state.join(format!("state-{}.json", c.child.id()));
    assert!(state_file.exists());
    // a new mount over it is refused
    let ev = c.request(env.mount_request(&["modA"]));
    assert_eq!(ev["code"], "io", "{ev}");
    assert!(ev.get("spontaneous").is_none(), "{ev}");

    // retried lazily until it works, then `unmounted`
    fs::remove_file(&flag).unwrap();
    let ev = c.try_event(Duration::from_secs(5)).expect("no unmounted");
    assert_eq!(ev, json!({"event": "unmounted"}));
    assert!(!is_mounted(&env.p("Data")));
    assert!(!state_file.exists());
    assert_eq!(fs::read_to_string(env.p("Data/base.txt")).unwrap(), "base");
    drop(child);
}

fn kill_signal(pid: u32, sig: rustix::process::Signal) {
    use rustix::process::{Pid, kill_process};
    let pid = Pid::from_raw(pid as i32).expect("pid");
    kill_process(pid, sig).expect("kill");
}

fn kill_term(pid: u32) {
    kill_signal(pid, rustix::process::Signal::TERM);
}

/// Events until one matches `pred` (returned), with the ones before it.
fn until(c: &mut Client, what: &str, pred: impl Fn(&Value) -> bool) -> (Value, Vec<Value>) {
    let mut before = Vec::new();
    loop {
        let ev = c
            .try_event(EVENT_TIMEOUT)
            .unwrap_or_else(|| panic!("no {what} within {EVENT_TIMEOUT:?}; got {before:?}"));
        if pred(&ev) {
            return (ev, before);
        }
        before.push(ev);
    }
}

fn spawn_request(argv: &[&str], cwd: &Path) -> Value {
    json!({"op": "spawn", "argv": argv, "env": {}, "cwd": cwd})
}

#[test]
fn spawn_without_mount_runs_and_reports_exit() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let out = env.p("out.txt");
    let script = format!(
        "echo \"$FOO\" > '{0}'; pwd >> '{0}'; cut -d' ' -f5 /proc/$$/stat >> '{0}'; echo $$ >> '{0}'; exit 3",
        out.display()
    );
    let mut req = spawn_request(&["sh", "-c", &script], &env.p("Data"));
    req["env"] = json!({"FOO": "bar"});
    let ev = c.request(req);
    assert_eq!(ev["event"], "spawned", "{ev}");
    let pid = ev["pid"].as_i64().unwrap();
    let (ev, _) = until(&mut c, "exited", |e| e["event"] == "exited");
    assert_eq!(ev, json!({"event": "exited", "pid": pid, "code": 3}));
    let lines: Vec<String> = fs::read_to_string(&out)
        .unwrap()
        .lines()
        .map(String::from)
        .collect();
    assert_eq!(lines[0], "bar");
    assert_eq!(Path::new(&lines[1]), env.p("Data"));
    // its own process group: pgrp == pid
    assert_eq!(lines[2], lines[3]);
    assert_eq!(lines[3], pid.to_string());
}

#[test]
fn spawn_exit_by_signal_is_128_plus_signal() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let ev = c.request(spawn_request(&["sh", "-c", "kill -TERM $$"], &env.root));
    let pid = ev["pid"].as_i64().unwrap();
    let (ev, _) = until(&mut c, "exited", |e| e["event"] == "exited");
    assert_eq!(ev, json!({"event": "exited", "pid": pid, "code": 143}));
}

#[test]
fn spawn_failures() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let ev = c.request(spawn_request(&["/nonexistent/program"], &env.root));
    assert_eq!(ev["event"], "error", "{ev}");
    assert_eq!(ev["code"], "spawn_failed");
    assert_eq!(ev["path"], "/nonexistent/program");
    let missing = env.p("missing-dir");
    let ev = c.request(spawn_request(&["true"], &missing));
    assert_eq!(ev["code"], "spawn_failed", "{ev}");
    assert_eq!(ev["path"], json!(missing));
    let ev = c.request(json!({"op": "spawn", "argv": [], "env": {}, "cwd": env.root}));
    assert_eq!(ev["code"], "bad_request", "{ev}");
    // still serving
    assert_eq!(c.request(json!({"op": "status"}))["event"], "status");
}

#[test]
fn spawn_argv_with_spaces_and_unicode() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let out = env.p("args.txt");
    let script = format!("printf '%s\\n' \"$@\" > '{}'", out.display());
    let ev = c.request(spawn_request(
        &[
            "sh",
            "-c",
            &script,
            "sh",
            "Skyrim Special Edition",
            "Ünïcödé/x.exe",
            "a \"b\" c",
        ],
        &env.root,
    ));
    assert_eq!(ev["event"], "spawned", "{ev}");
    until(&mut c, "exited", |e| e["event"] == "exited");
    assert_eq!(
        fs::read_to_string(&out).unwrap(),
        "Skyrim Special Edition\nÜnïcödé/x.exe\na \"b\" c\n"
    );
}

#[test]
fn spawn_output_goes_to_the_file_not_to_the_protocol() {
    let env = Env::new();
    let mut c = Client::start(&env);
    let log = env.p("run.log");
    let mut req = spawn_request(
        &["sh", "-c", "echo not-json; echo on-stderr >&2"],
        &env.root,
    );
    req["output"] = json!(log);
    c.request(req);
    // a non-JSON line on stdout would make the client panic
    until(&mut c, "exited", |e| e["event"] == "exited");
    assert_eq!(fs::read_to_string(&log).unwrap(), "not-json\non-stderr\n");
    // without output, nothing reaches the protocol either
    c.request(spawn_request(&["sh", "-c", "echo not-json"], &env.root));
    until(&mut c, "exited", |e| e["event"] == "exited");
}

#[test]
fn spawn_while_mounted_is_watched_and_exited_comes_before_unmounted() {
    if !require_fuse() {
        return;
    }
    let env = Env::new();
    let _g = MountGuard(env.p("Data"));
    let mut c = Client::start(&env);
    assert_eq!(c.request(env.mount_request(&["modA"]))["event"], "ready");
    let ev = c.request(spawn_request(
        &["sh", "-c", "cat a.txt > ../seen.txt; sleep 0.5"],
        &env.p("Data"),
    ));
    let pid = ev["pid"].as_i64().unwrap();
    let st = c.request(json!({"op": "status"}));
    assert_eq!(st["groups"], json!([pid]));
    let (ev, before) = until(&mut c, "unmounted", |e| e["event"] == "unmounted");
    assert_eq!(ev, json!({"event": "unmounted"}));
    assert_eq!(
        before,
        vec![json!({"event": "exited", "pid": pid, "code": 0})]
    );
    assert_eq!(fs::read_to_string(env.p("seen.txt")).unwrap(), "a");
    assert!(!is_mounted(&env.p("Data")));
}

#[test]
fn mo2_vfs_is_session_leader() {
    let env = Env::new();
    let c = Client::start(&env);
    let pid = c.child.id();
    let stat = fs::read_to_string(format!("/proc/{pid}/stat")).unwrap();
    let rest: Vec<&str> = stat[stat.rfind(')').unwrap() + 2..].split(' ').collect();
    // state ppid pgrp session
    assert_eq!(rest[3], pid.to_string());
}

#[test]
fn closed_stdout_is_not_a_warning() {
    let env = Env::new();
    let mut child = env
        .command()
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let mut stdin = child.stdin.take().unwrap();
    drop(child.stdout.take()); // the application is gone
    writeln!(stdin, "{}", json!({"op": "status"})).unwrap();
    drop(stdin);
    let out = child.wait_with_output().unwrap();
    assert!(out.status.success());
    let stderr = String::from_utf8_lossy(&out.stderr);
    assert!(!stderr.contains("cannot write to stdout"), "{stderr}");
}
