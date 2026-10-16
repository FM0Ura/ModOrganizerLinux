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

/// Canonical destination: directories are resolved fully; for files only the
/// parent is resolved, so a symlinked file does not move the root elsewhere.
fn canonical_destination(m: &Mapping) -> PathBuf {
    if m.is_directory {
        return canonicalize_lenient(&m.destination);
    }
    match (m.destination.parent(), m.destination.file_name()) {
        (Some(parent), Some(name)) => canonicalize_lenient(parent).join(name),
        _ => canonicalize_lenient(&m.destination),
    }
}

/// Groups mappings into mount roots (spec section 3.1).
pub fn plan_roots(mappings: &[Mapping]) -> Result<Vec<RootSpec>, MappingError> {
    for m in mappings {
        validate(&m.source)?;
        validate(&m.destination)?;
    }
    let dests: Vec<PathBuf> = mappings.iter().map(canonical_destination).collect();

    // Candidates: directory destinations, plus the parent of every file mapping
    // that is not under a directory destination.
    let dir_dests: Vec<&PathBuf> = mappings
        .iter()
        .zip(&dests)
        .filter(|(m, _)| m.is_directory)
        .map(|(_, d)| d)
        .collect();
    let mut candidates: Vec<PathBuf> = dir_dests.iter().map(|d| (*d).clone()).collect();
    for (m, d) in mappings.iter().zip(&dests) {
        if !m.is_directory
            && !dir_dests.iter().any(|r| d.starts_with(r))
            && let Some(parent) = d.parent()
        {
            candidates.push(parent.to_path_buf());
        }
    }
    // Roots never nest: keep a candidate only if no other candidate is a strict
    // ancestor of it; identical candidates collapse into one root.
    let mut roots: Vec<PathBuf> = Vec::new();
    for c in &candidates {
        let nested = candidates.iter().any(|o| o != c && c.starts_with(o));
        if !nested && !roots.contains(c) {
            roots.push(c.clone());
        }
    }

    let mut specs: Vec<RootSpec> = roots
        .into_iter()
        .map(|mountpoint| RootSpec {
            mountpoint,
            layers: Vec::new(),
        })
        .collect();
    for (priority, (m, d)) in mappings.iter().zip(&dests).enumerate() {
        // Roots never nest, so exactly one root is a prefix of any destination.
        let spec = specs
            .iter_mut()
            .find(|s| d.starts_with(&s.mountpoint))
            .expect("every destination lies under exactly one root");
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

    #[test]
    fn file_mapping_parent_does_not_nest_over_directory_root() {
        let t = tempdir();
        let p = t.path().canonicalize().unwrap();
        let tex = m(
            "/mods/B",
            &format!("{}/Data/textures", p.display()),
            true,
            false,
        );
        let esp = m(
            "/x.esp",
            &format!("{}/Data/x.esp", p.display()),
            false,
            false,
        );
        for input in [[tex.clone(), esp.clone()], [esp, tex]] {
            let roots = plan_roots(&input).unwrap();
            assert_eq!(roots.len(), 1);
            assert_eq!(roots[0].mountpoint, p.join("Data"));
            let by_src = |s: &str| {
                roots[0]
                    .layers
                    .iter()
                    .find(|l| l.source == Path::new(s))
                    .unwrap()
            };
            assert_eq!(by_src("/mods/B").target, PathBuf::from("textures"));
            assert_eq!(by_src("/x.esp").target, PathBuf::from("x.esp"));
        }
    }

    #[test]
    fn symlinked_file_destination_keeps_its_parent_as_root() {
        let t = tempdir();
        let p = t.path().canonicalize().unwrap();
        fs::create_dir_all(p.join("Other")).unwrap();
        fs::create_dir_all(p.join("Elsewhere")).unwrap();
        fs::write(p.join("Elsewhere/real.ini"), b"").unwrap();
        std::os::unix::fs::symlink(p.join("Elsewhere/real.ini"), p.join("Other/y.ini")).unwrap();
        let roots = plan_roots(&[m(
            "/y.ini",
            &format!("{}/Other/y.ini", p.display()),
            false,
            false,
        )])
        .unwrap();
        assert_eq!(roots[0].mountpoint, p.join("Other"));
        assert_eq!(roots[0].layers[0].target, PathBuf::from("y.ini"));
    }

    #[test]
    fn rejects_relative_destination() {
        assert!(matches!(
            plan_roots(&[m("/mods/A", "g/Data", true, false)]),
            Err(MappingError::Relative(_))
        ));
    }
}
