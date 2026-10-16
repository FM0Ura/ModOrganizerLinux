//! A mount session: one FUSE mount per root, hot updates, orderly unmount and state file.

use crate::fs::{Vfs, VfsOptions, VfsShared};
use crate::layer::{Layers, Skip, layers_over, open_layers};
use crate::mapping::{Mapping, RootSpec, plan_roots};
use crate::name::{Key, fold};
use crate::state::{self, StateFile};
use crate::tree::{Kind, Tree};
use fuser::{BackgroundSession, INodeNo};
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
use std::ffi::OsString;
use std::fmt;
use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::Arc;
use std::sync::mpsc;
use std::time::{Duration, Instant};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ErrorCode {
    FuseUnavailable,
    MountBusy,
    BadMapping,
    Io,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct SessionError {
    pub code: ErrorCode,
    pub message: String,
    pub path: Option<PathBuf>,
}

impl SessionError {
    fn new(code: ErrorCode, message: impl Into<String>, path: Option<PathBuf>) -> SessionError {
        SessionError {
            code,
            message: message.into(),
            path,
        }
    }

    fn io(what: &str, path: &Path, e: &dyn fmt::Display) -> SessionError {
        SessionError::new(
            ErrorCode::Io,
            format!("{what} {}: {e}", path.display()),
            Some(path.to_path_buf()),
        )
    }
}

impl fmt::Display for SessionError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}: {}", self.code, self.message)
    }
}

impl std::error::Error for SessionError {}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Stats {
    pub nodes: usize,
    pub masks: usize,
}

struct RootMount {
    mountpoint: PathBuf,
    shared: Arc<VfsShared>,
    bg: Option<BackgroundSession>,
    /// The mountpoint directory did not exist and was created by this session.
    created: bool,
}

pub struct Session {
    state_dir: PathBuf,
    roots: Vec<RootMount>,
}

/// Fails with `FuseUnavailable` when `/dev/fuse` or `fusermount3` is missing.
fn check_fuse() -> Result<(), SessionError> {
    let mut missing = Vec::new();
    if !Path::new("/dev/fuse").exists() {
        missing.push("/dev/fuse");
    }
    if Command::new("fusermount3")
        .arg("--version")
        .output()
        .is_err()
    {
        missing.push("fusermount3");
    }
    if missing.is_empty() {
        return Ok(());
    }
    let mut message = format!(
        "FUSE is not available: {} is missing.",
        missing.join(" and ")
    );
    if Path::new("/run/.containerenv").exists() || Path::new("/.dockerenv").exists() {
        message.push_str(
            " Running inside a container (distrobox/podman/docker): the container must \
             expose /dev/fuse and have fuse3 (fusermount3) installed.",
        );
    }
    Err(SessionError::new(ErrorCode::FuseUnavailable, message, None))
}

impl Session {
    /// Mounts every root of `mappings`, with the state file in `state::state_dir()`.
    pub fn mount(mappings: &[Mapping], skip: Skip) -> Result<Session, SessionError> {
        Session::mount_in(&state::state_dir(), mappings, skip)
    }

    pub fn mount_in(
        state_dir: &Path,
        mappings: &[Mapping],
        skip: Skip,
    ) -> Result<Session, SessionError> {
        Session::mount_with(state_dir, mappings, skip, VfsOptions::default())
    }

    pub fn mount_with(
        state_dir: &Path,
        mappings: &[Mapping],
        skip: Skip,
        options: VfsOptions,
    ) -> Result<Session, SessionError> {
        let specs = plan_roots(mappings)
            .map_err(|e| SessionError::new(ErrorCode::BadMapping, e.to_string(), None))?;
        check_fuse()?;
        let busy = state::mounted_mo2_vfs();
        if let Some(spec) = specs.iter().find(|s| busy.contains(&s.mountpoint)) {
            return Err(SessionError::new(
                ErrorCode::MountBusy,
                format!("{} is already mounted", spec.mountpoint.display()),
                Some(spec.mountpoint.clone()),
            ));
        }
        // A failure part-way drops the session, which unmounts what is already up.
        let mut session = Session {
            state_dir: state_dir.to_path_buf(),
            roots: Vec::new(),
        };
        for spec in &specs {
            let root = mount_root(spec, &skip, &options)?;
            session.roots.push(root);
        }
        let paths = session.roots();
        StateFile::write_in(state_dir, std::process::id(), &paths)
            .map_err(|e| SessionError::io("cannot write the state file in", state_dir, &e))?;
        Ok(session)
    }

    pub fn roots(&self) -> Vec<PathBuf> {
        self.roots.iter().map(|r| r.mountpoint.clone()).collect()
    }

    pub fn stats(&self) -> Stats {
        let mut s = Stats::default();
        for r in &self.roots {
            let tree = r.shared.tree.read();
            s.nodes += tree.len();
            let w = tree.whiteouts();
            s.masks += w.paths.len() + w.origins.len();
        }
        s
    }

    /// Replaces the layers and trees of the mounted roots. Mounts, inodes and open handles
    /// stay; whiteouts carry over; the kernel is told about every name that changed.
    pub fn update(&mut self, mappings: &[Mapping], skip: Skip) -> Result<(), SessionError> {
        let specs = plan_roots(mappings)
            .map_err(|e| SessionError::new(ErrorCode::BadMapping, e.to_string(), None))?;
        let same_set = specs.len() == self.roots.len()
            && specs
                .iter()
                .all(|s| self.roots.iter().any(|r| r.mountpoint == s.mountpoint));
        if !same_set {
            return Err(SessionError::new(
                ErrorCode::BadMapping,
                "the set of mount roots changed; unmount and mount again",
                None,
            ));
        }
        // Open every new layer set first: a failure leaves all roots untouched.
        let mut prepared = Vec::new();
        for spec in &specs {
            let root = self
                .roots
                .iter()
                .find(|r| r.mountpoint == spec.mountpoint)
                .expect("checked above");
            let old = root.shared.layers();
            // The mountpoint path leads into the mount itself: reuse the original fd.
            let original = old.list[0].dir.try_clone().map_err(|e| {
                SessionError::io(
                    "cannot duplicate the original layer of",
                    &spec.mountpoint,
                    &e,
                )
            })?;
            let layers = layers_over(spec, original, old.created_mountpoint)
                .map_err(|e| SessionError::io("cannot open the layers of", &spec.mountpoint, &e))?;
            prepared.push((root, Arc::new(layers)));
        }
        for (root, layers) in prepared {
            let (old_tree, old_layers) = swap_in(&root.shared, layers, &skip);
            notify_changes(&root.shared, &old_tree, &old_layers);
        }
        Ok(())
    }

    /// Unmounts every root, waits for the filesystem threads, removes the mountpoints this
    /// session created (when empty) and the state file.
    pub fn unmount(mut self) -> Result<(), SessionError> {
        self.shutdown()
    }

    fn shutdown(&mut self) -> Result<(), SessionError> {
        let mut first_err: Option<SessionError> = None;
        let mounted = state::mounted_mo2_vfs();
        for root in std::mem::take(&mut self.roots) {
            let mut lazy = false;
            if mounted.contains(&root.mountpoint) {
                match unmount_impl(&root.mountpoint) {
                    Ok(l) => lazy = l,
                    Err(m) => {
                        first_err.get_or_insert(SessionError::new(
                            ErrorCode::Io,
                            format!("cannot unmount {}: {m}", root.mountpoint.display()),
                            Some(root.mountpoint.clone()),
                        ));
                    }
                }
            }
            if let Some(bg) = root.bg {
                // After a lazy unmount the filesystem lives on until the last user leaves.
                join_within(
                    bg,
                    if lazy {
                        Duration::from_millis(200)
                    } else {
                        Duration::from_secs(3)
                    },
                );
            }
            if root.created {
                match std::fs::remove_dir(&root.mountpoint) {
                    Ok(()) => {}
                    Err(e) if e.kind() == io::ErrorKind::NotFound => {}
                    Err(e) => tracing::warn!(
                        path = %root.mountpoint.display(), error = %e,
                        "created mountpoint left behind"
                    ),
                }
            }
        }
        StateFile::remove_in(&self.state_dir, std::process::id());
        first_err.map_or(Ok(()), Err)
    }
}

impl Drop for Session {
    fn drop(&mut self) {
        if !self.roots.is_empty() {
            let _ = self.shutdown();
        }
    }
}

fn mount_root(
    spec: &RootSpec,
    skip: &Skip,
    options: &VfsOptions,
) -> Result<RootMount, SessionError> {
    // Layer 0 is opened here, before the mount hides the directory behind it.
    let layers = open_layers(spec)
        .map_err(|e| SessionError::io("cannot open the layers of", &spec.mountpoint, &e))?;
    let created = layers.created_mountpoint;
    let tree = Tree::build(&layers, skip);
    let vfs = Vfs::new(layers, tree, options.clone());
    let shared = vfs.shared();

    let mut config = fuser::Config::default();
    config.mount_options = vec![
        fuser::MountOption::FSName("mo2-vfs".to_string()),
        fuser::MountOption::Subtype("mo2-vfs".to_string()),
        fuser::MountOption::DefaultPermissions,
    ];
    config.n_threads = Some(4);
    match fuser::spawn_mount(vfs, &spec.mountpoint, &config) {
        Ok(bg) => {
            let _ = shared.notifier.set(bg.notifier());
            Ok(RootMount {
                mountpoint: spec.mountpoint.clone(),
                shared,
                bg: Some(bg),
                created,
            })
        }
        Err(e) => {
            if created {
                let _ = std::fs::remove_dir(&spec.mountpoint);
            }
            Err(SessionError::io("cannot mount", &spec.mountpoint, &e))
        }
    }
}

/// Builds the tree for `layers` (carrying the current whiteouts) and swaps both in.
fn swap_in(shared: &VfsShared, layers: Arc<Layers>, skip: &Skip) -> (Tree, Arc<Layers>) {
    for _ in 0..3 {
        let whiteouts = shared.tree.read().whiteouts().clone();
        let tree = Tree::build_with(&layers, skip, &whiteouts);
        if let Ok(old) = shared.swap_if(tree, layers.clone(), &whiteouts) {
            return old;
        }
    }
    // Handlers keep adding whiteouts; stop racing them.
    shared.rebuild_locked(layers.clone(), |w| Tree::build_with(&layers, skip, w))
}

/// What a name resolves to: kind, layer source and real path inside it.
type Resolution = (Kind, PathBuf, PathBuf);

fn resolve(tree: &Tree, layers: &Layers, key: &Key) -> Option<(Resolution, OsString)> {
    let id = tree.lookup_folded(key)?;
    let node = tree.node(id);
    let source = layers
        .list
        .get(node.origin.layer)
        .map(|l| l.source.clone())
        .unwrap_or_default();
    Some((
        (node.kind, source, node.origin.rel.clone()),
        node.name.clone(),
    ))
}

fn parent_key(key: &Key) -> Key {
    match key.0.iter().rposition(|&b| b == b'/') {
        Some(i) => Key(key.0[..i].to_vec()),
        None => Key(Vec::new()),
    }
}

fn child_key(parent: &Key, name: &std::ffi::OsStr) -> Key {
    let mut bytes = parent.0.clone();
    if !bytes.is_empty() {
        bytes.push(b'/');
    }
    bytes.extend_from_slice(&fold(name).0);
    Key(bytes)
}

#[derive(PartialEq, Eq, Hash)]
enum Inval {
    Inode(u64),
    Entry(u64, OsString),
}

/// Compares what the known inodes resolved to before and after the swap and returns the
/// kernel cache entries to drop. Takes the locks itself (tree, then inodes) and releases
/// them before returning.
fn changes(shared: &VfsShared, old_tree: &Tree, old_layers: &Layers) -> Vec<Inval> {
    let tree = shared.tree.read();
    let layers = shared.layers();
    let known: Vec<(u64, Key)> = shared
        .inodes
        .lock()
        .known()
        .map(|(i, k)| (i, k.clone()))
        .collect();
    let ino_of: HashMap<&Key, u64> = known.iter().map(|(i, k)| (k, *i)).collect();

    let mut out: HashSet<Inval> = HashSet::new();
    let mut any = false;
    for (ino, key) in &known {
        let before = resolve(old_tree, old_layers, key);
        let after = resolve(&tree, &layers, key);
        if before.as_ref().map(|b| &b.0) == after.as_ref().map(|a| &a.0) {
            continue;
        }
        any = true;
        out.insert(Inval::Inode(*ino));
        if key.0.is_empty() {
            continue;
        }
        let Some(&pino) = ino_of.get(&parent_key(key)) else {
            continue;
        };
        out.insert(Inval::Inode(pino));
        let last = OsString::from(
            String::from_utf8_lossy(key.0.rsplit(|&b| b == b'/').next().unwrap_or(&[]))
                .into_owned(),
        );
        for name in [before.map(|b| b.1), after.map(|a| a.1)]
            .into_iter()
            .flatten()
        {
            out.insert(Inval::Entry(pino, name));
        }
        out.insert(Inval::Entry(pino, last));
    }
    // Names that did not exist before may be cached as negative entries in known
    // directories.
    for (ino, key) in &known {
        let Some(id) = tree.lookup_folded(key) else {
            continue;
        };
        if tree.node(id).kind != Kind::Dir {
            continue;
        }
        for (_, child) in tree.children(id) {
            if old_tree
                .lookup_folded(&child_key(key, &child.name))
                .is_none()
            {
                any = true;
                out.insert(Inval::Entry(*ino, child.name.clone()));
                out.insert(Inval::Inode(*ino));
            }
        }
    }
    if any {
        out.insert(Inval::Inode(crate::inodes::ROOT_INO));
    }
    out.into_iter().collect()
}

fn notify_changes(shared: &VfsShared, old_tree: &Tree, old_layers: &Layers) {
    let list = changes(shared, old_tree, old_layers);
    let Some(notifier) = shared.notifier.get() else {
        return;
    };
    for item in list {
        let r = match &item {
            Inval::Inode(i) => notifier.inval_inode(INodeNo(*i), 0, 0),
            Inval::Entry(p, n) => notifier.inval_entry(INodeNo(*p), n),
        };
        if let Err(e) = r {
            tracing::debug!(error = %e, "cache invalidation ignored");
        }
    }
}

/// Joins the filesystem thread, giving up (the thread is left to finish by itself) after
/// `limit`.
fn join_within(bg: BackgroundSession, limit: Duration) {
    let (tx, rx) = mpsc::channel();
    let spawned = std::thread::Builder::new()
        .name("mo2-vfs-join".into())
        .spawn(move || {
            let _ = tx.send(bg.join());
        });
    if spawned.is_err() {
        return;
    }
    match rx.recv_timeout(limit) {
        Ok(Err(e)) => tracing::warn!(error = %e, "filesystem thread ended with an error"),
        Ok(Ok(())) => {}
        Err(_) => tracing::info!("filesystem thread still busy after unmount; not waiting"),
    }
}

/// Returns whether the lazy fallback was needed.
fn unmount_impl(path: &Path) -> Result<bool, String> {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        let out = Command::new("fusermount3")
            .arg("-u")
            .arg(path)
            .output()
            .map_err(|e| format!("cannot run fusermount3: {e}"))?;
        if out.status.success() {
            return Ok(false);
        }
        let err = String::from_utf8_lossy(&out.stderr).trim().to_string();
        if !err.to_lowercase().contains("busy") {
            return Err(err);
        }
        if Instant::now() >= deadline {
            break;
        }
        std::thread::sleep(Duration::from_millis(250));
    }
    let out = Command::new("fusermount3")
        .arg("-uz")
        .arg(path)
        .output()
        .map_err(|e| format!("cannot run fusermount3: {e}"))?;
    if out.status.success() {
        Ok(true)
    } else {
        Err(String::from_utf8_lossy(&out.stderr).trim().to_string())
    }
}

/// `fusermount3 -u`, retried every 250 ms for up to 5 s while the mount is busy; then
/// `fusermount3 -uz` (lazy).
pub fn unmount_path(path: &Path) -> Result<(), String> {
    unmount_impl(path).map(|_| ())
}
