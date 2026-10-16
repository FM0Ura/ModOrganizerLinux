//! Mapping list parsing and grouping of destinations into mount roots.

use serde::{Deserialize, Serialize};
use std::fmt;
use std::path::{Component, Path, PathBuf};

/// One entry of the mapping list sent by the application.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Mapping {
    pub source: PathBuf,
    pub destination: PathBuf,
    pub is_directory: bool,
    pub create_target: bool,
}

/// A mapping placed inside a root.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LayerSpec {
    pub source: PathBuf,
    /// Relative to the root; empty means the root itself.
    pub target: PathBuf,
    pub is_directory: bool,
    pub create_target: bool,
    /// Index in the original mapping vector.
    pub priority: usize,
}

/// A mount point and the layers that feed it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RootSpec {
    /// Canonical path.
    pub mountpoint: PathBuf,
    /// Increasing priority order.
    pub layers: Vec<LayerSpec>,
}

#[derive(Debug, PartialEq, Eq)]
pub enum MappingError {
    Relative(PathBuf),
    ParentDir(PathBuf),
}

impl fmt::Display for MappingError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MappingError::Relative(p) => write!(f, "path is not absolute: {}", p.display()),
            MappingError::ParentDir(p) => write!(f, "path contains '..': {}", p.display()),
        }
    }
}

impl std::error::Error for MappingError {}

fn validate(p: &Path) -> Result<(), MappingError> {
    if !p.is_absolute() {
        return Err(MappingError::Relative(p.to_path_buf()));
    }
    if p.components().any(|c| c == Component::ParentDir) {
        return Err(MappingError::ParentDir(p.to_path_buf()));
    }
    Ok(())
}

/// Canonicalizes `p` if it exists; otherwise canonicalizes the nearest existing
/// ancestor and appends the remaining components.
fn canonicalize_lenient(p: &Path) -> PathBuf {
    let mut rest: Vec<&std::ffi::OsStr> = Vec::new();
    let mut cur = p;
    loop {
        if let Ok(c) = cur.canonicalize() {
            let mut out = c;
            out.extend(rest.iter().rev());
            return out;
        }
        match (cur.parent(), cur.file_name()) {
            (Some(parent), Some(name)) => {
                rest.push(name);
                cur = parent;
            }
            _ => {
                // Nothing resolvable (e.g. "/" itself failed); keep as is.
                let mut out = cur.to_path_buf();
                out.extend(rest.iter().rev());
                return out;
            }
        }
    }
}

/// Picks `candidates` as roots in order of increasing depth, skipping any that
/// lies under an already chosen root.
fn choose_roots(roots: &mut Vec<PathBuf>, mut candidates: Vec<PathBuf>) {
    candidates.sort_by_key(|p| p.components().count());
    for c in candidates {
        if !roots.iter().any(|r| c.starts_with(r)) {
            roots.push(c);
        }
    }
}

/// Groups mappings into mount roots (spec section 3.1).
pub fn plan_roots(mappings: &[Mapping]) -> Result<Vec<RootSpec>, MappingError> {
    for m in mappings {
        validate(&m.source)?;
        validate(&m.destination)?;
    }
    let dests: Vec<PathBuf> = mappings
        .iter()
        .map(|m| canonicalize_lenient(&m.destination))
        .collect();

    let mut roots: Vec<PathBuf> = Vec::new();
    choose_roots(
        &mut roots,
        mappings
            .iter()
            .zip(&dests)
            .filter(|(m, _)| m.is_directory)
            .map(|(_, d)| d.clone())
            .collect(),
    );
    // File mappings outside every directory root get their parent as a root.
    let parents: Vec<PathBuf> = mappings
        .iter()
        .zip(&dests)
        .filter(|(m, d)| !m.is_directory && !roots.iter().any(|r| d.starts_with(r)))
        .filter_map(|(_, d)| d.parent().map(Path::to_path_buf))
        .collect();
    choose_roots(&mut roots, parents);

    let mut specs: Vec<RootSpec> = roots
        .into_iter()
        .map(|mountpoint| RootSpec {
            mountpoint,
            layers: Vec::new(),
        })
        .collect();
    for (priority, (m, d)) in mappings.iter().zip(&dests).enumerate() {
        // Deepest enclosing root wins (roots never nest, but be explicit).
        let Some(spec) = specs
            .iter_mut()
            .filter(|s| d.starts_with(&s.mountpoint))
            .max_by_key(|s| s.mountpoint.components().count())
        else {
            continue;
        };
        let target = d
            .strip_prefix(&spec.mountpoint)
            .map(Path::to_path_buf)
            .unwrap_or_default();
        spec.layers.push(LayerSpec {
            source: m.source.clone(),
            target,
            is_directory: m.is_directory,
            create_target: m.create_target,
            priority,
        });
    }
    Ok(specs)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn tempdir() -> tempfile::TempDir {
        tempfile::tempdir().unwrap()
    }

    fn m(src: &str, dst: &str, dir: bool, create: bool) -> Mapping {
        Mapping {
            source: src.into(),
            destination: dst.into(),
            is_directory: dir,
            create_target: create,
        }
    }

    #[test]
    fn one_root_per_destination() {
        let t = tempdir();
        let p = t.path().canonicalize().unwrap();
        fs::create_dir(p.join("Data")).unwrap();
        fs::create_dir(p.join("Saves")).unwrap();
        let t = p.display();
        let roots = plan_roots(&[
            m("/mods/A", &format!("{t}/Data"), true, false),
            m("/mods/B", &format!("{t}/Data"), true, false),
            m("/profiles/p/saves", &format!("{t}/Saves"), true, true),
        ])
        .unwrap();
        assert_eq!(roots.len(), 2);
        let data = roots
            .iter()
            .find(|r| r.mountpoint.ends_with("Data"))
            .unwrap();
        assert_eq!(
            data.layers
                .iter()
                .map(|l| l.source.clone())
                .collect::<Vec<_>>(),
            vec![PathBuf::from("/mods/A"), PathBuf::from("/mods/B")]
        );
        assert!(data.layers.iter().all(|l| l.target.as_os_str().is_empty()));
    }

    #[test]
    fn nested_destination_joins_outer_root() {
        let t = tempdir();
        let p = t.path().canonicalize().unwrap();
        fs::create_dir_all(p.join("Data/textures")).unwrap();
        let roots = plan_roots(&[
            m("/mods/A", &format!("{}/Data", p.display()), true, false),
            m(
                "/mods/B",
                &format!("{}/Data/textures", p.display()),
                true,
                false,
            ),
        ])
        .unwrap();
        assert_eq!(roots.len(), 1);
        assert_eq!(roots[0].mountpoint, p.join("Data"));
        assert_eq!(roots[0].layers[0].target, PathBuf::new());
        assert_eq!(roots[0].layers[1].target, PathBuf::from("textures"));
    }

    #[test]
    fn nested_destination_listed_first_still_joins_outer_root() {
        let t = tempdir();
        let p = t.path().canonicalize().unwrap();
        let roots = plan_roots(&[
            m("/b", &format!("{}/Data/textures", p.display()), true, false),
            m("/a", &format!("{}/Data", p.display()), true, false),
        ])
        .unwrap();
        assert_eq!(roots.len(), 1);
        assert_eq!(roots[0].layers[0].priority, 0);
        assert_eq!(roots[0].layers[0].target, PathBuf::from("textures"));
        assert_eq!(roots[0].layers[1].target, PathBuf::new());
    }

    #[test]
    fn file_mapping_inside_root_and_alone() {
        let t = tempdir();
        let p = t.path().canonicalize().unwrap();
        fs::create_dir_all(p.join("Data")).unwrap();
        fs::create_dir_all(p.join("Other")).unwrap();
        let roots = plan_roots(&[
            m("/mods/A", &format!("{}/Data", p.display()), true, false),
            m(
                "/x.esp",
                &format!("{}/Data/x.esp", p.display()),
                false,
                false,
            ),
            m(
                "/y.ini",
                &format!("{}/Other/y.ini", p.display()),
                false,
                false,
            ),
        ])
        .unwrap();
        assert_eq!(roots.len(), 2);
        let data = roots
            .iter()
            .find(|r| r.mountpoint == p.join("Data"))
            .unwrap();
        assert_eq!(data.layers.len(), 2);
        assert_eq!(data.layers[1].target, PathBuf::from("x.esp"));
        assert!(!data.layers[1].is_directory);
        let other = roots
            .iter()
            .find(|r| r.mountpoint == p.join("Other"))
            .unwrap();
        assert_eq!(other.layers.len(), 1);
        assert_eq!(other.layers[0].target, PathBuf::from("y.ini"));
    }

    #[test]
    fn priority_is_input_order() {
        let t = tempdir();
        let p = t.path().canonicalize().unwrap();
        let d = format!("{}/Data", p.display());
        let roots = plan_roots(&[
            m("/a", &d, true, false),
            m("/b", &format!("{}/Saves", p.display()), true, false),
            m("/c", &d, true, false),
            m("/d", &d, true, false),
        ])
        .unwrap();
        let data = roots
            .iter()
            .find(|r| r.mountpoint.ends_with("Data"))
            .unwrap();
        let prios: Vec<usize> = data.layers.iter().map(|l| l.priority).collect();
        assert_eq!(prios, vec![0, 2, 3]);
    }

    #[test]
    fn rejects_relative_and_dotdot() {
        assert!(matches!(
            plan_roots(&[m("mods/A", "/g/Data", true, false)]),
            Err(MappingError::Relative(_))
        ));
        assert!(matches!(
            plan_roots(&[m("/mods/A", "/g/../Data", true, false)]),
            Err(MappingError::ParentDir(_))
        ));
        assert!(matches!(
            plan_roots(&[m("/mods/../A", "/g/Data", true, false)]),
            Err(MappingError::ParentDir(_))
        ));
    }

    #[test]
    fn mountpoint_is_canonical() {
        let t = tempdir();
        let p = t.path().canonicalize().unwrap();
        fs::create_dir(p.join("Data")).unwrap();
        std::os::unix::fs::symlink(p.join("Data"), p.join("link")).unwrap();
        let roots =
            plan_roots(&[m("/mods/A", &format!("{}/link", p.display()), true, false)]).unwrap();
        assert_eq!(roots[0].mountpoint, p.join("Data").canonicalize().unwrap());
    }

    #[test]
    fn missing_destination_uses_nearest_existing_parent() {
        let t = tempdir();
        let p = t.path().canonicalize().unwrap();
        fs::create_dir(p.join("Data")).unwrap();
        std::os::unix::fs::symlink(p.join("Data"), p.join("link")).unwrap();
        let roots = plan_roots(&[m(
            "/mods/A",
            &format!("{}/link/new/sub", p.display()),
            true,
            false,
        )])
        .unwrap();
        assert_eq!(roots[0].mountpoint, p.join("Data/new/sub"));
    }
}
