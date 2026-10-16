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

use mo2_vfs::fs::{Vfs, VfsOptions, VfsShared};
use mo2_vfs::layer::{Skip, open_layers};
use mo2_vfs::mapping::{Mapping, plan_roots};
use mo2_vfs::tree::Tree;
use std::path::PathBuf;
use std::sync::Arc;

/// Layer directories under a tempdir. The first layer is the original (the mountpoint); the
/// others are mapped onto it in order, the last one with `create: true`. Every file's content
/// is "<layer>:<relative path>".
pub struct Fixture {
    pub tmp: tempfile::TempDir,
    pub names: Vec<String>,
    pub options: VfsOptions,
}

impl Fixture {
    pub fn new(layers: &[(&str, &[&str])]) -> Fixture {
        let tmp = tempdir();
        for (name, files) in layers {
            let base = tmp.path().join(name);
            std::fs::create_dir_all(&base).unwrap();
            for f in *files {
                let p = base.join(f);
                std::fs::create_dir_all(p.parent().unwrap()).unwrap();
                std::fs::write(&p, format!("{name}:{f}")).unwrap();
            }
        }
        Fixture {
            tmp,
            names: layers.iter().map(|(n, _)| n.to_string()).collect(),
            options: VfsOptions::default(),
        }
    }

    pub fn skyrim() -> Fixture {
        Fixture::new(&[
            ("original", &["Skyrim.esm", "textures/rock.dds"]),
            ("A", &["textures/rock.dds", "meshes/tree.nif"]),
            ("B", &["textures/rock.dds"]),
            ("overwrite", &[]),
        ])
    }

    pub fn dir(&self, layer: &str) -> PathBuf {
        self.tmp.path().join(layer)
    }

    pub fn with_options(mut self, options: VfsOptions) -> Fixture {
        self.options = options;
        self
    }

    pub fn mount(self) -> Mounted {
        let mountpoint = self.dir(&self.names[0]);
        let last = self.names.len() - 1;
        let mappings: Vec<Mapping> = self.names[1..]
            .iter()
            .enumerate()
            .map(|(i, n)| Mapping {
                source: self.dir(n),
                destination: mountpoint.clone(),
                is_directory: true,
                create_target: i + 1 == last,
            })
            .collect();
        let roots = plan_roots(&mappings).expect("plan");
        assert_eq!(roots.len(), 1);
        let layers = open_layers(&roots[0]).expect("layers");
        let tree = Tree::build(&layers, &Skip::default());
        let vfs = Vfs::new(layers, tree, self.options.clone());
        let shared = vfs.shared();

        let mut config = fuser::Config::default();
        config.mount_options = vec![
            fuser::MountOption::FSName("mo2-vfs".to_string()),
            fuser::MountOption::Subtype("mo2-vfs".to_string()),
            fuser::MountOption::DefaultPermissions,
        ];
        config.n_threads = Some(4);
        let session = fuser::spawn_mount(vfs, &mountpoint, &config).expect("mount");
        let _ = shared.notifier.set(session.notifier());
        Mounted {
            fixture: self,
            session: Some(session),
            mountpoint,
            shared,
        }
    }
}

/// A live mount; owns the fixture so the layer directories outlive it.
pub struct Mounted {
    pub fixture: Fixture,
    session: Option<fuser::BackgroundSession>,
    mountpoint: PathBuf,
    pub shared: Arc<VfsShared>,
}

impl Mounted {
    pub fn path(&self) -> &std::path::Path {
        &self.mountpoint
    }
}

impl Drop for Mounted {
    fn drop(&mut self) {
        if let Some(s) = self.session.take() {
            let _ = s.umount_and_join();
        }
    }
}
