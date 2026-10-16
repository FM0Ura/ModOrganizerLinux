#![allow(dead_code)]
use std::path::Path;

/// true when FUSE can be used; false (skip) when it can't, unless MO2_REQUIRE_FUSE=1
pub fn require_fuse() -> bool {
    let available = Path::new("/dev/fuse").exists()
        && std::process::Command::new("fusermount3")
            .arg("--version")
            .output()
            .is_ok();
    if !available {
        if std::env::var("MO2_REQUIRE_FUSE").as_deref() == Ok("1") {
            panic!("FUSE is required (MO2_REQUIRE_FUSE=1) but /dev/fuse or fusermount3 is missing");
        }
        eprintln!("skipped: FUSE is not available");
    }
    available
}

pub fn tempdir() -> tempfile::TempDir {
    tempfile::Builder::new()
        .prefix("mo2-vfs-test")
        .tempdir()
        .unwrap()
}
