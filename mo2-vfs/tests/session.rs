mod common;

use common::{require_fuse, tempdir};
use mo2_vfs::layer::Skip;
use mo2_vfs::mapping::Mapping;
use mo2_vfs::session::{ErrorCode, Session};
use mo2_vfs::state::{self, StateFile};
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

struct Env {
    _tmp: tempfile::TempDir,
    root: PathBuf,
    state: PathBuf,
}

impl Env {
    fn new() -> Env {
        let tmp = tempdir();
        let root = tmp.path().canonicalize().unwrap();
        let state = root.join("state");
        Env {
            _tmp: tmp,
            root,
            state,
        }
    }

    fn write(&self, rel: &str, content: &str) {
        let p = self.root.join(rel);
        fs::create_dir_all(p.parent().unwrap()).unwrap();
        fs::write(p, content).unwrap();
    }

    fn p(&self, rel: &str) -> PathBuf {
        self.root.join(rel)
    }

    fn dir_map(&self, source: &str, dest: &str, create: bool) -> Mapping {
        Mapping {
            source: self.p(source),
            destination: self.p(dest),
            is_directory: true,
            create_target: create,
        }
    }

    fn mount(&self, mappings: &[Mapping]) -> Session {
        Session::mount_in(&self.state, mappings, Skip::default()).expect("mount")
    }
}

fn is_mounted(p: &Path) -> bool {
    state::mounted_mo2_vfs().iter().any(|m| m == p)
}

/// Data/ holds the original; A and B both ship textures/rock.dds, B wins.
fn rock_env() -> Env {
    let e = Env::new();
    e.write("Data/Skyrim.esm", "orig");
    e.write("A/textures/rock.dds", "A-rock-is-longer");
    e.write("B/textures/rock.dds", "B-rock");
    e
}

fn rock_maps(e: &Env, with_b: bool) -> Vec<Mapping> {
    let mut m = vec![e.dir_map("A", "Data", false)];
    if with_b {
        m.push(e.dir_map("B", "Data", false));
    }
    m
}

#[test]
fn mounts_every_root() {
    if !require_fuse() {
        return;
    }
    let e = Env::new();
    e.write("Data/orig.esm", "o");
    e.write("Saves/old.ess", "s");
    e.write("mod/a.txt", "a");
    e.write("prof/saves/new.ess", "n");
    let s = e.mount(&[
        e.dir_map("mod", "Data", false),
        e.dir_map("prof/saves", "Saves", true),
    ]);
    assert_eq!(s.roots().len(), 2);
    assert!(is_mounted(&e.p("Data")) && is_mounted(&e.p("Saves")));
    assert_eq!(fs::read_to_string(e.p("Data/a.txt")).unwrap(), "a");
    assert_eq!(fs::read_to_string(e.p("Saves/new.ess")).unwrap(), "n");
    assert_eq!(fs::read_to_string(e.p("Saves/old.ess")).unwrap(), "s");
    assert!(s.stats().nodes >= 5);
    s.unmount().unwrap();
    assert!(!is_mounted(&e.p("Data")) && !is_mounted(&e.p("Saves")));
}

#[test]
fn missing_root_is_created_and_removed() {
    if !require_fuse() {
        return;
    }
    let e = Env::new();
    e.write("mod/a.txt", "a");
    let s = e.mount(&[e.dir_map("mod", "NewDir", false)]);
    assert_eq!(fs::read_to_string(e.p("NewDir/a.txt")).unwrap(), "a");
    s.unmount().unwrap();
    assert!(!e.p("NewDir").exists());
}

#[test]
fn update_swaps_tree_and_invalidates() {
    if !require_fuse() {
        return;
    }
    let e = rock_env();
    let mut s = e.mount(&rock_maps(&e, true));
    let rock = e.p("Data/textures/rock.dds");
    assert_eq!(fs::read_to_string(&rock).unwrap(), "B-rock");
    assert_eq!(fs::metadata(&rock).unwrap().len(), 6); // now cached by the kernel for 60 s
    let before = Instant::now();
    s.update(&rock_maps(&e, false), Skip::default()).unwrap();
    assert_eq!(fs::metadata(&rock).unwrap().len(), 16);
    assert_eq!(fs::read_to_string(&rock).unwrap(), "A-rock-is-longer");
    assert!(
        before.elapsed() < Duration::from_secs(10),
        "waited for the TTL"
    );
    // a file that appears with the new mapping is visible at once, a vanished one is gone
    e.write("C/new.txt", "c");
    let mut maps = rock_maps(&e, false);
    maps.push(e.dir_map("C", "Data", false));
    assert!(!e.p("Data/new.txt").exists()); // caches a negative entry
    s.update(&maps, Skip::default()).unwrap();
    assert_eq!(fs::read_to_string(e.p("Data/new.txt")).unwrap(), "c");
    s.update(&rock_maps(&e, false), Skip::default()).unwrap();
    assert!(!e.p("Data/new.txt").exists());
    s.unmount().unwrap();
}

#[test]
fn masks_survive_update() {
    if !require_fuse() {
        return;
    }
    let e = rock_env();
    let mut s = e.mount(&rock_maps(&e, true));
    fs::remove_file(e.p("Data/Skyrim.esm")).unwrap();
    assert!(!e.p("Data/Skyrim.esm").exists());
    assert_eq!(s.stats().masks, 1);
    s.update(&rock_maps(&e, false), Skip::default()).unwrap();
    assert!(!e.p("Data/Skyrim.esm").exists());
    assert_eq!(s.stats().masks, 1);
    s.unmount().unwrap();
}

#[test]
fn open_handle_survives_update() {
    if !require_fuse() {
        return;
    }
    let e = rock_env();
    let mut s = e.mount(&rock_maps(&e, true));
    let rock = e.p("Data/textures/rock.dds");
    let mut old = fs::File::open(&rock).unwrap();
    s.update(&rock_maps(&e, false), Skip::default()).unwrap();
    let mut text = String::new();
    old.read_to_string(&mut text).unwrap();
    assert_eq!(text, "B-rock");
    assert_eq!(fs::read_to_string(&rock).unwrap(), "A-rock-is-longer");
    drop(old);
    s.unmount().unwrap();
}

#[test]
fn update_with_different_roots_is_bad_mapping() {
    if !require_fuse() {
        return;
    }
    let e = rock_env();
    e.write("Other/x", "x");
    let mut s = e.mount(&rock_maps(&e, true));
    let mut maps = rock_maps(&e, true);
    maps.push(e.dir_map("B", "Other", false));
    let err = s.update(&maps, Skip::default()).unwrap_err();
    assert_eq!(err.code, ErrorCode::BadMapping);
    assert_eq!(
        err.message,
        "the set of mount roots changed; unmount and mount again"
    );
    // the running mount is untouched
    assert_eq!(
        fs::read_to_string(e.p("Data/textures/rock.dds")).unwrap(),
        "B-rock"
    );
    s.unmount().unwrap();
}

#[test]
fn busy_unmount_falls_back_to_lazy() {
    if !require_fuse() {
        return;
    }
    let e = rock_env();
    let s = e.mount(&rock_maps(&e, true));
    let mut child = std::process::Command::new("sleep")
        .arg("30")
        .current_dir(e.p("Data"))
        .spawn()
        .unwrap();
    let t = Instant::now();
    let r = s.unmount();
    let took = t.elapsed();
    let still = is_mounted(&e.p("Data"));
    let _ = child.kill();
    let _ = child.wait();
    r.expect("unmount");
    assert!(took <= Duration::from_secs(6), "took {took:?}");
    assert!(!still);
}

#[test]
fn mount_over_already_mounted_root_is_busy() {
    if !require_fuse() {
        return;
    }
    let e = rock_env();
    let s = e.mount(&rock_maps(&e, true));
    let err = Session::mount_in(&e.state, &rock_maps(&e, true), Skip::default())
        .err()
        .expect("second mount must fail");
    assert_eq!(err.code, ErrorCode::MountBusy);
    assert_eq!(err.path.as_deref(), Some(e.p("Data").as_path()));
    s.unmount().unwrap();
}

#[test]
fn bad_mapping_error() {
    // needs no FUSE
    let e = Env::new();
    let err = Session::mount_in(
        &e.state,
        &[Mapping {
            source: PathBuf::from("relative/dir"),
            destination: e.p("Data"),
            is_directory: true,
            create_target: false,
        }],
        Skip::default(),
    )
    .err()
    .expect("must fail");
    assert_eq!(err.code, ErrorCode::BadMapping);
    assert_eq!(serde_json::to_string(&err.code).unwrap(), "\"bad_mapping\"");
}

#[test]
fn state_file_written_and_removed() {
    if !require_fuse() {
        return;
    }
    let e = rock_env();
    let s = e.mount(&rock_maps(&e, true));
    let file = e.state.join(format!("state-{}.json", std::process::id()));
    let v: serde_json::Value = serde_json::from_str(&fs::read_to_string(&file).unwrap()).unwrap();
    assert_eq!(v["pid"], std::process::id());
    assert_eq!(v["roots"], serde_json::json!([e.p("Data")]));
    s.unmount().unwrap();
    assert!(!file.exists());
}

#[test]
fn symlinked_mountpoint() {
    if !require_fuse() {
        return;
    }
    let e = Env::new();
    e.write("Data/orig.txt", "o");
    e.write("mod/arquivo-do-mod", "m");
    std::os::unix::fs::symlink(e.p("Data"), e.p("link")).unwrap();
    let s = e.mount(&[e.dir_map("mod", "link", false)]);
    assert!(is_mounted(&e.p("Data")));
    assert_eq!(fs::read_to_string(e.p("link/arquivo-do-mod")).unwrap(), "m");
    s.unmount().unwrap();
}

#[test]
fn stale_state_of_a_dead_process_is_cleaned() {
    if !require_fuse() {
        return;
    }
    let e = rock_env();
    // a "crashed" instance: mounted, state file of a pid that no longer exists
    let s = e.mount(&rock_maps(&e, true));
    let mut dead = std::process::Command::new("true").spawn().unwrap();
    let dead_pid = dead.id();
    dead.wait().unwrap();
    StateFile::write_in(&e.state, dead_pid, &[e.p("Data"), e.p("NotMounted")]).unwrap();
    let results = state::cleanup_stale_in(&e.state);
    assert_eq!(results.len(), 2);
    assert!(results.iter().all(|(_, r)| r.is_ok()), "{results:?}");
    assert!(!is_mounted(&e.p("Data")));
    assert!(!e.state.join(format!("state-{dead_pid}.json")).exists());
    // our own state file (live pid) is left alone
    assert!(
        e.state
            .join(format!("state-{}.json", std::process::id()))
            .exists()
    );
    drop(s); // the filesystem thread has ended; Drop must cope with the missing mount
    let _ = fs::remove_dir_all(&e.state);
}

#[test]
fn parent_dir_in_destination_is_bad_mapping() {
    let e = Env::new();
    e.write("mod/a.txt", "a");
    let mut m = e.dir_map("mod", "Data", false);
    m.destination = e.p("Data/../Other");
    let err = Session::mount_in(&e.state, &[m], Skip::default())
        .err()
        .expect("must fail");
    assert_eq!(err.code, ErrorCode::BadMapping);
}

#[test]
fn changes_during_updates_are_not_lost() {
    use std::sync::atomic::{AtomicUsize, Ordering};
    if !require_fuse() {
        return;
    }
    let e = rock_env();
    // B first, A (the create layer) last
    let both = vec![e.dir_map("B", "Data", false), e.dir_map("A", "Data", true)];
    let only_a = vec![e.dir_map("A", "Data", true)];
    let mut s = e.mount(&both);
    let data = e.p("Data");
    let done = std::sync::Arc::new(AtomicUsize::new(0));
    const N: usize = 150;
    let worker = {
        let (data, done) = (data.clone(), done.clone());
        std::thread::spawn(move || {
            for i in 0..N {
                let tmp = data.join(format!("tmp-{i}.part"));
                fs::write(&tmp, format!("{i}")).unwrap();
                fs::create_dir_all(data.join(format!("d{}", i % 5))).unwrap();
                fs::rename(&tmp, data.join(format!("d{}/final-{i}.txt", i % 5))).unwrap();
                done.store(i + 1, Ordering::Release);
            }
        })
    };
    let check = |upto: usize| {
        for i in 0..upto {
            let f = data.join(format!("d{}/final-{i}.txt", i % 5));
            assert_eq!(fs::read_to_string(&f).unwrap(), format!("{i}"), "{f:?}");
            assert!(!data.join(format!("tmp-{i}.part")).exists());
        }
    };
    let mut flip = false;
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(60);
    while done.load(Ordering::Acquire) < N {
        if worker.is_finished() {
            break; // a finished worker that did not reach N panicked; join reports it below
        }
        if std::time::Instant::now() > deadline {
            panic!(
                "changes_during_updates_are_not_lost: worker did not finish in 60 s ({} of {N} done)",
                done.load(Ordering::Acquire)
            );
        }
        let before = done.load(Ordering::Acquire);
        flip = !flip;
        s.update(if flip { &only_a } else { &both }, Skip::default())
            .unwrap();
        check(before);
    }
    worker.join().expect("worker thread panicked");
    s.update(&both, Skip::default()).unwrap();
    check(N);
    s.unmount().unwrap();
}

fn listing(dir: &Path) -> Vec<String> {
    let mut names: Vec<String> = fs::read_dir(dir)
        .unwrap()
        .map(|e| e.unwrap().file_name().to_string_lossy().into_owned())
        .collect();
    names.sort();
    names
}

#[test]
fn update_refreshes_cached_listings() {
    if !require_fuse() {
        return;
    }
    // The kernel caches directory listings (no opendir): updates and changes through the
    // mount must still show at once.
    let e = rock_env();
    e.write("A/textures/a.dds", "a");
    e.write("B/textures/b.dds", "b");
    e.write("C/textures/c.dds", "c");
    e.write("C/new.txt", "n");
    e.write("D/TEXTURES/d.dds", "d");
    let mut s = e.mount(&rock_maps(&e, true));
    let data = e.p("Data");
    let tex = e.p("Data/textures");
    for _ in 0..2 {
        assert_eq!(listing(&data), ["Skyrim.esm", "textures"]);
        assert_eq!(listing(&tex), ["a.dds", "b.dds", "rock.dds"]);
    }
    let a_c = vec![e.dir_map("A", "Data", false), e.dir_map("C", "Data", false)];
    s.update(&a_c, Skip::default()).unwrap();
    assert_eq!(listing(&data), ["Skyrim.esm", "new.txt", "textures"]);
    assert_eq!(listing(&tex), ["a.dds", "c.dds", "rock.dds"]);
    // another spelling of the directory wins
    let a_d = vec![e.dir_map("A", "Data", false), e.dir_map("D", "Data", true)];
    s.update(&a_d, Skip::default()).unwrap();
    assert_eq!(listing(&data), ["Skyrim.esm", "TEXTURES"]);
    assert_eq!(listing(&tex), ["a.dds", "d.dds", "rock.dds"]);
    // changes made through the mount
    fs::write(tex.join("made.dds"), "m").unwrap();
    assert_eq!(listing(&tex), ["a.dds", "d.dds", "made.dds", "rock.dds"]);
    fs::remove_file(tex.join("a.dds")).unwrap();
    fs::rename(tex.join("made.dds"), tex.join("moved.dds")).unwrap();
    assert_eq!(listing(&tex), ["d.dds", "moved.dds", "rock.dds"]);
    s.unmount().unwrap();
}

#[test]
fn kept_page_cache_stays_coherent() {
    if !require_fuse() {
        return;
    }
    // The kernel keeps a file's cached pages across opens while the real file is unchanged;
    // every way the content behind a name can change must still show at the next open.
    // Same size and same mtime, so neither can tell the versions apart.
    let e = rock_env();
    e.write("A/textures/same.dds", "AAAA");
    e.write("B/textures/same.dds", "BBBB");
    let when = std::time::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    for f in ["A/textures/same.dds", "B/textures/same.dds"] {
        let file = fs::File::options().write(true).open(e.p(f)).unwrap();
        file.set_modified(when).unwrap();
    }
    let mut s = e.mount(&rock_maps(&e, true));
    let same = e.p("Data/textures/same.dds");
    let copy = e.p("Data/copy/textures/same.dds");
    for _ in 0..2 {
        assert_eq!(fs::read_to_string(&same).unwrap(), "BBBB");
    }
    // a hot update puts another real file behind the name
    s.update(&rock_maps(&e, false), Skip::default()).unwrap();
    assert_eq!(fs::read_to_string(&same).unwrap(), "AAAA");
    // a write through the mount
    {
        use std::io::Write;
        let mut f = fs::File::options().write(true).open(&same).unwrap();
        f.write_all(b"CCCC").unwrap();
    }
    assert_eq!(fs::read_to_string(&same).unwrap(), "CCCC");
    // the same real file under two virtual paths: a write through one shows in the other
    let twice = vec![
        e.dir_map("A", "Data", false),
        e.dir_map("A", "Data/copy", false),
    ];
    s.update(&twice, Skip::default()).unwrap();
    for _ in 0..2 {
        assert_eq!(fs::read_to_string(&copy).unwrap(), "CCCC");
    }
    fs::write(&same, "DDDD").unwrap();
    assert_eq!(fs::read_to_string(&copy).unwrap(), "DDDD");
    // a change behind the VFS shows at the next open
    for _ in 0..2 {
        assert_eq!(fs::read_to_string(&same).unwrap(), "DDDD");
    }
    fs::write(e.p("A/textures/same.dds"), "EEEE").unwrap();
    assert_eq!(fs::read_to_string(&same).unwrap(), "EEEE");
    assert_eq!(fs::read_to_string(&copy).unwrap(), "EEEE");
    s.unmount().unwrap();
}
