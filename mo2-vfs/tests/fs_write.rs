mod common;

use common::Fixture;
use std::fs;
use std::io::{Read, Seek, SeekFrom, Write};
use std::os::unix::fs::{MetadataExt, OpenOptionsExt, PermissionsExt};
use std::process::Command;

fn read(p: impl AsRef<std::path::Path>) -> String {
    fs::read_to_string(p).unwrap()
}

#[test]
fn edit_mod_file_changes_mod() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let real = f.dir("A").join("meshes/tree.nif");
    let m = f.mount();
    fs::write(m.path().join("meshes/tree.nif"), "edited").unwrap();
    assert_eq!(read(&real), "edited");
    assert_eq!(read(m.path().join("meshes/tree.nif")), "edited");
    assert!(!m.real("overwrite").join("meshes").exists());
}

#[test]
fn edit_original_file_changes_original() {
    if !common::require_fuse() {
        return;
    }
    // Spec 1.1: editing the content of a game file still edits the original.
    let f = Fixture::skyrim();
    let m = f.mount();
    let real = m.real("original").join("Skyrim.esm");
    fs::write(m.path().join("Skyrim.esm"), "patched").unwrap();
    assert_eq!(read(&real), "patched");
    assert!(!m.real("overwrite").join("Skyrim.esm").exists());
}

#[test]
fn write_at_offsets_and_append() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let real = f.dir("A").join("meshes/tree.nif");
    let m = f.mount();
    let path = m.path().join("meshes/tree.nif");
    let mut file = fs::OpenOptions::new().write(true).open(&path).unwrap();
    file.seek(SeekFrom::Start(2)).unwrap();
    file.write_all(b"XY").unwrap();
    file.sync_all().unwrap();
    drop(file);
    assert_eq!(read(&real), "A:XYshes/tree.nif");
    let mut app = fs::OpenOptions::new().append(true).open(&path).unwrap();
    app.write_all(b"++").unwrap();
    drop(app);
    assert!(read(&real).ends_with("tree.nif++"));
}

#[test]
fn truncate_and_chmod_reach_real_file() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let real = f.dir("A").join("meshes/tree.nif");
    let m = f.mount();
    let path = m.path().join("meshes/tree.nif");
    // truncate(2) by path (no handle), then ftruncate through a handle
    fs::OpenOptions::new()
        .write(true)
        .open(&path)
        .unwrap()
        .set_len(3)
        .unwrap();
    assert_eq!(fs::metadata(&real).unwrap().len(), 3);
    let h = fs::OpenOptions::new().write(true).open(&path).unwrap();
    h.set_len(10).unwrap();
    assert_eq!(fs::metadata(&real).unwrap().len(), 10);
    drop(h);
    fs::set_permissions(&path, fs::Permissions::from_mode(0o600)).unwrap();
    assert_eq!(fs::metadata(&real).unwrap().mode() & 0o7777, 0o600);
    let t = std::time::UNIX_EPOCH + std::time::Duration::from_secs(1_000_000_000);
    let h = fs::OpenOptions::new().write(true).open(&path).unwrap();
    h.set_modified(t).unwrap();
    drop(h);
    assert_eq!(fs::metadata(&real).unwrap().modified().unwrap(), t);
    // the original is untouched by an edit of a mod file
    assert_eq!(
        read(m.real("original").join("Skyrim.esm")),
        "original:Skyrim.esm"
    );
}

#[test]
fn new_file_goes_to_overwrite() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let ow = f.dir("overwrite");
    let m = f.mount();
    fs::write(m.path().join("novo.txt"), "hello").unwrap();
    assert_eq!(read(ow.join("novo.txt")), "hello");
    assert!(!m.real("original").join("novo.txt").exists());
    assert!(!m.real("A").join("novo.txt").exists());
    assert_eq!(read(m.path().join("novo.txt")), "hello");
    // a file created in a directory that exists in several layers
    fs::write(m.path().join("textures/new.dds"), "n").unwrap();
    assert_eq!(read(ow.join("textures/new.dds")), "n");
    assert!(!m.real("B").join("textures/new.dds").exists());
    // exclusive creation of an existing name fails
    let e = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(m.path().join("novo.txt"))
        .unwrap_err();
    assert_eq!(e.kind(), std::io::ErrorKind::AlreadyExists);
    // creation mode is honoured
    let h = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o640)
        .open(m.path().join("modo.txt"))
        .unwrap();
    drop(h);
    assert_eq!(
        fs::metadata(ow.join("modo.txt")).unwrap().mode() & 0o777,
        0o640 & !current_umask()
    );
}

fn current_umask() -> u32 {
    let m = rustix::process::umask(rustix::fs::Mode::from_raw_mode(0o022));
    rustix::process::umask(m);
    m.as_raw_mode() as u32
}

#[test]
fn mkdir_p_goes_to_overwrite() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let ow = f.dir("overwrite");
    let m = f.mount();
    fs::create_dir(m.path().join("meshes/new")).unwrap();
    assert!(ow.join("meshes/new").is_dir());
    assert!(!m.real("A").join("meshes/new").exists());
    fs::create_dir_all(m.path().join("a/b/c")).unwrap();
    assert!(ow.join("a/b/c").is_dir());
    assert!(!m.real("original").join("a").exists());
    fs::write(m.path().join("a/b/c/f.txt"), "x").unwrap();
    assert_eq!(read(ow.join("a/b/c/f.txt")), "x");
    assert_eq!(
        fs::create_dir(m.path().join("a")).unwrap_err().kind(),
        std::io::ErrorKind::AlreadyExists
    );
    // rmdir of an empty directory made through the mount
    fs::remove_dir(m.path().join("meshes/new")).unwrap();
    assert!(!ow.join("meshes/new").exists());
    assert!(!m.path().join("meshes/new").exists());
    // rmdir of a non-empty one fails
    assert!(fs::remove_dir(m.path().join("a/b")).is_err());
}

#[test]
fn delete_mod_file_and_lower_stays_hidden() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let m = f.mount();
    fs::remove_file(m.path().join("textures/rock.dds")).unwrap();
    // the winner (B) is deleted for real; the lower versions do not reappear
    assert!(!m.real("B").join("textures/rock.dds").exists());
    assert!(!m.path().join("textures/rock.dds").exists());
    assert!(
        !fs::read_dir(m.path().join("textures"))
            .unwrap()
            .any(|e| e.unwrap().file_name() == "rock.dds")
    );
    // ...but are left alone on disk
    assert_eq!(
        read(m.real("A").join("textures/rock.dds")),
        "A:textures/rock.dds"
    );
    assert_eq!(
        read(m.real("original").join("textures/rock.dds")),
        "original:textures/rock.dds"
    );
    // deleting a missing file
    assert_eq!(
        fs::remove_file(m.path().join("textures/rock.dds"))
            .unwrap_err()
            .kind(),
        std::io::ErrorKind::NotFound
    );
}

#[test]
fn delete_original_keeps_game_file() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let m = f.mount();
    let real = m.real("original").join("Skyrim.esm");
    fs::remove_file(m.path().join("Skyrim.esm")).unwrap();
    assert!(!m.path().join("Skyrim.esm").exists());
    assert_eq!(read(&real), "original:Skyrim.esm");
    // a new file with the same name lands in the create layer and shadows the hidden one
    fs::write(m.path().join("Skyrim.esm"), "mine").unwrap();
    assert_eq!(read(m.real("overwrite").join("Skyrim.esm")), "mine");
    assert_eq!(read(&real), "original:Skyrim.esm");
    assert_eq!(read(m.path().join("Skyrim.esm")), "mine");
}

#[test]
fn save_tmp_rename_pattern() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let ow = f.dir("overwrite");
    let m = f.mount();
    fs::create_dir(m.path().join("saves")).unwrap();
    fs::write(m.path().join("saves/SaveX.tmp"), "save data").unwrap();
    fs::rename(
        m.path().join("saves/SaveX.tmp"),
        m.path().join("saves/SaveX.ess"),
    )
    .unwrap();
    assert_eq!(read(ow.join("saves/SaveX.ess")), "save data");
    assert!(!ow.join("saves/SaveX.tmp").exists());
    assert!(!m.path().join("saves/SaveX.tmp").exists());
    assert_eq!(read(m.path().join("saves/SaveX.ess")), "save data");
    // overwriting a previous save
    fs::write(m.path().join("saves/SaveX.tmp"), "newer").unwrap();
    fs::rename(
        m.path().join("saves/SaveX.tmp"),
        m.path().join("saves/SaveX.ess"),
    )
    .unwrap();
    assert_eq!(read(ow.join("saves/SaveX.ess")), "newer");
    assert_eq!(fs::read_dir(ow.join("saves")).unwrap().count(), 1);
    for l in ["original", "A", "B"] {
        assert!(!m.real(l).join("saves").exists(), "{l}");
    }
}

#[test]
fn rename_original_copies() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let m = f.mount();
    let real = m.real("original").join("Skyrim.esm");
    fs::rename(m.path().join("Skyrim.esm"), m.path().join("Moved.esm")).unwrap();
    assert_eq!(read(&real), "original:Skyrim.esm");
    assert_eq!(
        read(m.real("overwrite").join("Moved.esm")),
        "original:Skyrim.esm"
    );
    assert!(!m.path().join("Skyrim.esm").exists());
    assert_eq!(read(m.path().join("Moved.esm")), "original:Skyrim.esm");
}

#[test]
fn rename_mod_file_moves_inside_its_layer() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let m = f.mount();
    fs::rename(
        m.path().join("meshes/tree.nif"),
        m.path().join("meshes/oak.nif"),
    )
    .unwrap();
    assert!(!m.real("A").join("meshes/tree.nif").exists());
    assert_eq!(
        read(m.real("A").join("meshes/oak.nif")),
        "A:meshes/tree.nif"
    );
    assert!(!m.path().join("meshes/tree.nif").exists());
    assert_eq!(read(m.path().join("MESHES/OAK.NIF")), "A:meshes/tree.nif");
}

#[test]
fn rename_created_dir_keeps_content_and_numbers() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let ow = f.dir("overwrite");
    let m = f.mount();
    fs::create_dir_all(m.path().join("d/sub")).unwrap();
    fs::write(m.path().join("d/sub/f.txt"), "keep").unwrap();
    let ino = fs::metadata(m.path().join("d/sub/f.txt")).unwrap().ino();
    fs::rename(m.path().join("d"), m.path().join("e")).unwrap();
    assert!(!ow.join("d").exists());
    assert_eq!(read(ow.join("e/sub/f.txt")), "keep");
    assert!(!m.path().join("d").exists());
    assert_eq!(read(m.path().join("e/sub/f.txt")), "keep");
    assert_eq!(
        fs::metadata(m.path().join("e/sub/f.txt")).unwrap().ino(),
        ino
    );
}

#[test]
fn rename_over_existing_with_other_spelling() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let ow = f.dir("overwrite");
    let m = f.mount();
    fs::write(m.path().join("x.txt"), "x").unwrap();
    fs::write(m.path().join("Y.TXT"), "old").unwrap();
    assert_eq!(read(m.path().join("y.txt")), "old");
    fs::rename(m.path().join("x.txt"), m.path().join("y.txt")).unwrap();
    assert_eq!(read(m.path().join("Y.TXT")), "x");
    assert_eq!(read(m.path().join("y.txt")), "x");
    let names: Vec<_> = fs::read_dir(&ow)
        .unwrap()
        .map(|e| e.unwrap().file_name().into_string().unwrap())
        .collect();
    assert_eq!(names.len(), 1, "{names:?}");
    assert_eq!(read(ow.join(&names[0])), "x");
}

#[test]
fn rename_merged_dir_is_exdev_and_mv_still_works() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let m = f.mount();
    let e = fs::rename(m.path().join("textures"), m.path().join("textures2")).unwrap_err();
    assert_eq!(e.raw_os_error(), Some(libc::EXDEV));
    assert!(m.path().join("textures/rock.dds").exists());

    let out = Command::new("mv")
        .arg(m.path().join("textures"))
        .arg(m.path().join("textures2"))
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "mv failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    assert_eq!(
        read(m.real("overwrite").join("textures2/rock.dds")),
        "B:textures/rock.dds"
    );
    assert_eq!(
        read(m.path().join("textures2/rock.dds")),
        "B:textures/rock.dds"
    );
    assert!(!m.path().join("textures").exists());
    // the game's own file is still on disk
    assert_eq!(
        read(m.real("original").join("textures/rock.dds")),
        "original:textures/rock.dds"
    );
}

#[test]
fn same_file_two_spellings() {
    if !common::require_fuse() {
        return;
    }
    let m = Fixture::skyrim().mount();
    let mut w = fs::OpenOptions::new()
        .write(true)
        .truncate(true)
        .open(m.path().join("SKYRIM.ESM"))
        .unwrap();
    let mut r = fs::File::open(m.path().join("skyrim.esm")).unwrap();
    assert_eq!(w.metadata().unwrap().ino(), r.metadata().unwrap().ino());
    w.write_all(b"two spellings").unwrap();
    w.flush().unwrap();
    let mut s = String::new();
    r.read_to_string(&mut s).unwrap();
    assert_eq!(s, "two spellings");
    assert_eq!(
        fs::metadata(m.path().join("Skyrim.esm")).unwrap().ino(),
        r.metadata().unwrap().ino()
    );
}

#[test]
fn directory_alias_by_spelling() {
    if !common::require_fuse() {
        return;
    }
    let m = Fixture::skyrim().mount();
    let a = fs::metadata(m.path().join("Textures")).unwrap().ino();
    let b = fs::metadata(m.path().join("TEXTURES")).unwrap().ino();
    assert_eq!(a, b);
    let names: Vec<_> = fs::read_dir(m.path().join("textures"))
        .unwrap()
        .map(|e| e.unwrap().file_name())
        .collect();
    assert_eq!(names, ["rock.dds"]);
    assert_eq!(fs::metadata(m.path().join("textures")).unwrap().ino(), a);
    assert!(m.path().join("TeXtUrEs/ROCK.dds").exists());
    assert_eq!(fs::read_dir(m.path().join("Textures")).unwrap().count(), 1);
}

#[test]
fn symlink_and_hardlink_refused() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let m = f.mount();
    let e = std::os::unix::fs::symlink("Skyrim.esm", m.path().join("link")).unwrap_err();
    assert_eq!(e.raw_os_error(), Some(libc::EPERM));
    let e = fs::hard_link(m.path().join("Skyrim.esm"), m.path().join("hard")).unwrap_err();
    assert_eq!(e.raw_os_error(), Some(libc::EPERM));
    for l in ["original", "A", "B", "overwrite"] {
        let d = m.real(l);
        assert!(!d.join("link").exists() && !d.join("hard").exists(), "{l}");
        assert!(fs::symlink_metadata(d.join("link")).is_err(), "{l}");
    }
}

#[test]
fn xattrs_are_not_supported() {
    if !common::require_fuse() {
        return;
    }
    let m = Fixture::skyrim().mount();
    let p = m.path().join("Skyrim.esm");
    let e = rustix::fs::setxattr(&p, "user.x", b"1", rustix::fs::XattrFlags::empty()).unwrap_err();
    assert_eq!(e.raw_os_error(), libc::ENOTSUP);
    let mut buf = [0u8; 8];
    let e = rustix::fs::getxattr(&p, "user.x", &mut buf).unwrap_err();
    assert_eq!(e.raw_os_error(), libc::ENOTSUP);
    let e = rustix::fs::listxattr(&p, &mut buf).unwrap_err();
    assert_eq!(e.raw_os_error(), libc::ENOTSUP);
    let e = rustix::fs::removexattr(&p, "user.x").unwrap_err();
    assert_eq!(e.raw_os_error(), libc::ENOTSUP);
}

#[test]
fn unlinked_open_file_stays_readable() {
    if !common::require_fuse() {
        return;
    }
    let m = Fixture::skyrim().mount();
    fs::write(m.path().join("tmp.bin"), "data").unwrap();
    let mut h = fs::File::open(m.path().join("tmp.bin")).unwrap();
    fs::remove_file(m.path().join("tmp.bin")).unwrap();
    assert!(!m.real("overwrite").join("tmp.bin").exists());
    let mut s = String::new();
    h.read_to_string(&mut s).unwrap();
    assert_eq!(s, "data");
}

#[test]
fn deleted_and_renamed_files_vanish_under_every_spelling() {
    if !common::require_fuse() {
        return;
    }
    let m = Fixture::skyrim().mount();
    fs::write(m.path().join("Foo.txt"), "foo").unwrap();
    // The kernel now caches both spellings.
    assert!(m.path().join("FOO.TXT").exists());
    assert!(m.path().join("foo.txt").exists());
    fs::remove_file(m.path().join("Foo.txt")).unwrap();
    assert!(!m.path().join("FOO.TXT").exists());
    assert!(!m.path().join("foo.txt").exists());
    assert!(!m.path().join("Foo.txt").exists());

    fs::write(m.path().join("Bar.txt"), "bar").unwrap();
    assert!(m.path().join("BAR.TXT").exists());
    fs::rename(m.path().join("Bar.txt"), m.path().join("Baz.txt")).unwrap();
    assert!(!m.path().join("BAR.TXT").exists());
    assert!(!m.path().join("bar.txt").exists());
    assert_eq!(read(m.path().join("BAZ.TXT")), "bar");
}
