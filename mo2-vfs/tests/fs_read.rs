mod common;

use common::Fixture;
use mo2_vfs::fs::VfsOptions;
use std::fs;
use std::os::unix::fs::FileExt;
use std::sync::atomic::Ordering;

#[test]
fn reads_winner_content() {
    if !common::require_fuse() {
        return;
    }
    let m = Fixture::skyrim().mount();
    assert_eq!(
        fs::read_to_string(m.path().join("textures/rock.dds")).unwrap(),
        "B:textures/rock.dds"
    );
    assert_eq!(
        fs::read_to_string(m.path().join("Skyrim.esm")).unwrap(),
        "original:Skyrim.esm"
    );
    assert_eq!(
        fs::read_to_string(m.path().join("meshes/tree.nif")).unwrap(),
        "A:meshes/tree.nif"
    );
}

#[test]
fn lists_merged_directory() {
    if !common::require_fuse() {
        return;
    }
    let m = Fixture::skyrim().mount();
    let mut names: Vec<_> = fs::read_dir(m.path())
        .unwrap()
        .map(|e| e.unwrap().file_name())
        .collect();
    names.sort();
    assert_eq!(names, ["Skyrim.esm", "meshes", "textures"]);
    let sub: Vec<_> = fs::read_dir(m.path().join("textures"))
        .unwrap()
        .map(|e| e.unwrap().file_name())
        .collect();
    assert_eq!(sub, ["rock.dds"]);
}

#[test]
fn lookup_any_case() {
    if !common::require_fuse() {
        return;
    }
    let m = Fixture::skyrim().mount();
    assert!(m.path().join("TEXTURES/ROCK.DDS").exists());
    assert!(!m.path().join("TEXTURES/NOPE.DDS").exists());
}

#[test]
fn original_is_visible_under_the_mount() {
    if !common::require_fuse() {
        return;
    }
    // The mountpoint held Skyrim.esm before mounting: it stays visible through layer 0.
    let f = Fixture::skyrim();
    assert!(f.dir("original").join("Skyrim.esm").exists());
    let m = f.mount();
    assert_eq!(
        fs::read_to_string(m.path().join("Skyrim.esm")).unwrap(),
        "original:Skyrim.esm"
    );
}

#[test]
fn metadata_comes_from_real_file() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let real = fs::metadata(f.dir("B").join("textures/rock.dds")).unwrap();
    let m = f.mount();
    let md = fs::metadata(m.path().join("textures/rock.dds")).unwrap();
    assert_eq!(md.len(), real.len());
    assert_eq!(md.modified().unwrap(), real.modified().unwrap());
    assert!(md.is_file());
    assert!(fs::metadata(m.path().join("textures")).unwrap().is_dir());
}

#[test]
fn symlinks_are_followed() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::new(&[
        ("original", &[]),
        ("A", &["real/target.txt"]),
        ("overwrite", &[]),
    ]);
    std::os::unix::fs::symlink(
        f.dir("A").join("real/target.txt"),
        f.dir("A").join("link.txt"),
    )
    .unwrap();
    let m = f.mount();
    let md = fs::symlink_metadata(m.path().join("link.txt")).unwrap();
    assert!(md.file_type().is_file());
    assert_eq!(
        fs::read_to_string(m.path().join("link.txt")).unwrap(),
        "A:real/target.txt"
    );
}

#[test]
fn large_file_read_at_offsets() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim();
    let data: Vec<u8> = (0..8u32 * 1024 * 1024)
        .map(|i| (i.wrapping_mul(2654435761) >> 13) as u8)
        .collect();
    fs::write(f.dir("A").join("big.bin"), &data).unwrap();
    let m = f.mount();
    let file = fs::File::open(m.path().join("big.bin")).unwrap();
    let mut x: u64 = 88172645463325252;
    for _ in 0..50 {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        let off = (x % data.len() as u64) as usize;
        let len = ((x >> 20) % 200_000) as usize + 1;
        let end = (off + len).min(data.len());
        let mut buf = vec![0u8; end - off];
        file.read_exact_at(&mut buf, off as u64).unwrap();
        assert_eq!(buf, &data[off..end], "offset {off}");
    }
    assert_eq!(fs::read(m.path().join("big.bin")).unwrap(), data);
}

#[test]
fn passthrough_falls_back_to_pread() {
    if !common::require_fuse() {
        return;
    }
    let f = Fixture::skyrim().with_options(VfsOptions {
        try_passthrough: true,
        ..VfsOptions::default()
    });
    let m = f.mount();
    assert_eq!(
        fs::read_to_string(m.path().join("textures/rock.dds")).unwrap(),
        "B:textures/rock.dds"
    );
    assert_eq!(
        fs::read_to_string(m.path().join("Skyrim.esm")).unwrap(),
        "original:Skyrim.esm"
    );
    // Reads are correct whichever way they were served; a refusal turns passthrough off.
    let refused = m.shared.passthrough_refused.load(Ordering::Relaxed);
    if refused {
        assert!(!m.shared.passthrough.load(Ordering::Relaxed));
    } else {
        assert!(m.shared.passthrough.load(Ordering::Relaxed));
    }
    // Registering a backing file needs CAP_SYS_ADMIN: without privilege the kernel refuses.
    if rustix::process::geteuid().as_raw() != 0 {
        assert!(refused, "unprivileged passthrough must be refused");
    }
    assert_eq!(
        fs::read_to_string(m.path().join("textures/rock.dds")).unwrap(),
        "B:textures/rock.dds"
    );
}

#[test]
fn statfs_reports_the_create_layer() {
    if !common::require_fuse() {
        return;
    }
    let m = Fixture::skyrim().mount();
    let st = rustix::fs::statvfs(m.path()).unwrap();
    assert!(st.f_blocks > 0);
}
