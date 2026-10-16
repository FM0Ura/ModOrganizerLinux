//! Layers: directory fds of the real directories that feed a mount root, and skip rules.

use crate::mapping::RootSpec;
use crate::name::{Key, fold};
use rustix::fs::{CWD, Mode, OFlags, openat};
use std::ffi::{OsStr, OsString};
use std::io;
use std::os::fd::OwnedFd;
use std::os::unix::ffi::OsStrExt;
use std::path::{Path, PathBuf};

/// Index into `Layers::list`; 0 is the original layer (the content under the mountpoint).
pub type LayerId = usize;

pub struct Layer {
    pub id: LayerId,
    /// `O_RDONLY | O_DIRECTORY | O_CLOEXEC`. For file mappings this is the parent directory.
    pub dir: OwnedFd,
    /// File mapping: name of the file inside `dir`.
    pub file: Option<OsString>,
    /// Placement inside the root (empty for the original layer).
    pub target: PathBuf,
    pub create: bool,
    pub original: bool,
    /// For logging only.
    pub source: PathBuf,
}

pub struct Layers {
    /// `list[0]` is the original layer; mapping layers follow in increasing priority.
    pub list: Vec<Layer>,
    /// True when the mountpoint did not exist and was created.
    pub created_mountpoint: bool,
}

fn open_dir(path: &Path) -> io::Result<OwnedFd> {
    Ok(openat(
        CWD,
        path,
        OFlags::RDONLY | OFlags::DIRECTORY | OFlags::CLOEXEC,
        Mode::empty(),
    )?)
}

/// Opens the mountpoint (creating it if missing) as layer 0 and one fd per mapping layer.
/// Missing sources are skipped with a warning and do not consume an id.
pub fn open_layers(root: &RootSpec) -> io::Result<Layers> {
    let mut created_mountpoint = false;
    if !root.mountpoint.exists() {
        std::fs::create_dir_all(&root.mountpoint)?;
        created_mountpoint = true;
    }
    let original = open_dir(&root.mountpoint)?;
    layers_over(root, original, created_mountpoint)
}

/// Opens the mapping layers of `root` on top of an already open original layer. Used by hot
/// updates, when the mountpoint path leads into the mount itself.
pub fn layers_over(
    root: &RootSpec,
    original: OwnedFd,
    created_mountpoint: bool,
) -> io::Result<Layers> {
    let mut list = vec![Layer {
        id: 0,
        dir: original,
        file: None,
        target: PathBuf::new(),
        create: false,
        original: true,
        source: root.mountpoint.clone(),
    }];
    for spec in &root.layers {
        let (dir_path, file) = if spec.is_directory {
            (spec.source.as_path(), None)
        } else {
            match (spec.source.parent(), spec.source.file_name()) {
                (Some(p), Some(n)) => (p, Some(n.to_os_string())),
                _ => {
                    tracing::warn!(source = %spec.source.display(), "invalid file mapping source, skipping layer");
                    continue;
                }
            }
        };
        let dir = match open_dir(dir_path) {
            Ok(fd) => fd,
            Err(e) => {
                tracing::warn!(source = %spec.source.display(), error = %e, "mapping source unavailable, skipping layer");
                continue;
            }
        };
        if let Some(name) = &file
            && let Err(e) = rustix::fs::statat(
                &dir,
                name.as_os_str().as_bytes(),
                rustix::fs::AtFlags::empty(),
            )
        {
            tracing::warn!(source = %spec.source.display(), error = %e, "mapping source file unavailable, skipping layer");
            continue;
        }
        list.push(Layer {
            id: list.len(),
            dir,
            file,
            target: spec.target.clone(),
            create: spec.create_target,
            original: false,
            source: spec.source.clone(),
        });
    }
    Ok(Layers {
        list,
        created_mountpoint,
    })
}

/// Names hidden from the merged tree.
#[derive(Default, Clone, Debug)]
pub struct Skip {
    /// Folded file name suffixes.
    pub suffixes: Vec<String>,
    /// Folded directory names.
    pub directories: Vec<Key>,
}

impl Skip {
    pub fn new(suffixes: &[String], directories: &[String]) -> Skip {
        Skip {
            suffixes: suffixes
                .iter()
                .filter(|s| !s.is_empty())
                .map(|s| String::from_utf8_lossy(&fold(OsStr::new(s)).0).into_owned())
                .collect(),
            directories: directories
                .iter()
                .filter(|d| !d.is_empty())
                .map(|d| fold(OsStr::new(d)))
                .collect(),
        }
    }

    pub fn skips_file(&self, name: &OsStr) -> bool {
        let folded = fold(name).0;
        self.suffixes.iter().any(|s| folded.ends_with(s.as_bytes()))
    }

    pub fn skips_dir(&self, name: &OsStr) -> bool {
        self.directories.contains(&fold(name))
    }
}
