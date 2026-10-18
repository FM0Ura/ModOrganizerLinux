//! A mount session: one FUSE mount per root, hot updates, orderly unmount and state file.

use crate::fs::{Vfs, VfsOptions, VfsShared};
use crate::inodes::Inodes;
use crate::layer::{Layers, Skip, layers_over, open_layers};
use crate::mapping::{Mapping, RootSpec, plan_roots};
use crate::state::{self, StateFile};
use crate::tree::{Kind, NodeId, Tree};
use fuser::{BackgroundSession, INodeNo};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, HashMap, HashSet};
use std::ffi::OsString;
use std::fmt;
use std::io;
use std::os::unix::ffi::OsStringExt;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::mpsc;
use std::time::{Duration, Instant};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ErrorCode {
    FuseUnavailable,
    MountBusy,
    BadMapping,
    Io,
    /// Malformed or out-of-sequence request (used by the line protocol).
    BadRequest,
    /// A program of a `spawn` request could not be started.
    SpawnFailed,
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
    /// Mounts every root of `mappings`, with the state file in `state::state_dir()`. State
    /// files are keyed by pid, so a process holds one `Session` at a time.
    pub fn mount(mappings: &[Mapping], skip: Skip) -> Result<Session, SessionError> {
        let dir = state::ensure_state_dir()
            .map_err(|e| SessionError::new(ErrorCode::Io, format!("state directory: {e}"), None))?;
        Session::mount_in(&dir, mappings, skip)
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
            // Freeing a big tree takes a while and nothing waits for it.
            let dropped = std::thread::Builder::new()
                .name("mo2-vfs-drop".into())
                .spawn(move || drop((old_tree, old_layers)));
            if let Err(e) = dropped {
                tracing::debug!(error = %e, "old tree freed in place");
            }
        }
        Ok(())
    }

    /// Unmounts every root, waits for the filesystem threads, removes the mountpoints this
    /// session created (when empty) and the state file.
    pub fn unmount(mut self) -> Result<(), SessionError> {
        self.shutdown()
    }

    /// The directory of the state file.
    pub fn state_dir(&self) -> &Path {
        &self.state_dir
    }

    /// A root that cannot be unmounted stays in the state file, so that `--cleanup` can
    /// still find it once this process is gone.
    fn shutdown(&mut self) -> Result<(), SessionError> {
        let mut first_err: Option<SessionError> = None;
        let mut failed = Vec::new();
        let mounted = state::mounted_mo2_vfs();
        for root in std::mem::take(&mut self.roots) {
            let mut lazy = false;
            if mounted.contains(&root.mountpoint) {
                match unmount_impl(&root.mountpoint) {
                    Ok(l) => lazy = l,
                    Err(m) => {
                        failed.push(root.mountpoint.clone());
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
        if failed.is_empty() {
            StateFile::remove_in(&self.state_dir, std::process::id());
        } else if let Err(e) = StateFile::write_in(&self.state_dir, std::process::id(), &failed) {
            tracing::warn!(error = %e, "cannot rewrite the state file");
        }
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

/// Builds the tree for `layers` (carrying the current whiteouts) and swaps both in. The swap
/// only happens if no handler changed the tree since the scan started.
fn swap_in(shared: &VfsShared, layers: Arc<Layers>, skip: &Skip) -> (Tree, Arc<Layers>) {
    for _ in 0..3 {
        let (generation, whiteouts) = {
            let tree = shared.tree.read();
            (shared.generation(), tree.whiteouts().clone())
        };
        let tree = Tree::build_with(&layers, skip, &whiteouts);
        if let Ok(old) = shared.swap_if(tree, layers.clone(), generation) {
            return old;
        }
    }
    tracing::warn!("tree changed during every rebuild attempt; rebuilding under the lock");
    shared.rebuild_locked(layers.clone(), |w| Tree::build_with(&layers, skip, w))
}

#[derive(PartialEq, Eq, Hash, Debug)]
enum Inval {
    Inode(u64),
    Entry(u64, OsString),
}

/// The known inode numbers of a folded path: `Inodes::ino_of` plus the aliases.
struct Known<'a> {
    inodes: &'a Inodes,
    aliases: HashMap<&'a [u8], Vec<u64>>,
}

impl<'a> Known<'a> {
    fn new(inodes: &'a Inodes) -> Known<'a> {
        let mut aliases: HashMap<&[u8], Vec<u64>> = HashMap::new();
        for (ino, key) in inodes.aliases() {
            aliases.entry(key.0.as_slice()).or_default().push(ino);
        }
        Known { inodes, aliases }
    }

    fn of(&self, key: &[u8]) -> impl Iterator<Item = u64> + '_ {
        let aliases = if self.aliases.is_empty() {
            None
        } else {
            self.aliases.get(key)
        };
        self.inodes
            .ino_of(key)
            .into_iter()
            .chain(aliases.into_iter().flatten().copied())
    }
}

/// Walks the old and the new tree together (every path of either, once) and collects the
/// kernel cache entries to drop: for each known inode whose path now resolves to another
/// kind, layer or real path, the inode, its parent and the parent's entries for the old,
/// new and folded name; for each known directory, the names it gained (they may be cached
/// as negative entries); for each name shown with another spelling, the parent's entries.
struct ChangeWalk<'a> {
    old: &'a Tree,
    old_layers: &'a Layers,
    new: &'a Tree,
    layers: &'a Layers,
    known: &'a Known<'a>,
    /// Folded path of the node being visited.
    key: Vec<u8>,
    out: HashSet<Inval>,
    any: bool,
    /// Paths at this depth are not visited but left in `deferred`, to be walked in
    /// parallel.
    defer_at: Option<usize>,
    deferred: Vec<Deferred>,
}

/// A subtree left for later by `ChangeWalk::visit`: its key, old and new node and the
/// length of its parent's key.
type Deferred = (Vec<u8>, Option<NodeId>, Option<NodeId>, usize);

/// What a node resolves to: kind, layer source and real path inside it.
type Resolution<'t> = (Kind, &'t Path, &'t Path);

fn resolution<'t>(tree: &'t Tree, layers: &'t Layers, id: NodeId) -> Resolution<'t> {
    let node = tree.node(id);
    let source = layers
        .list
        .get(node.origin.layer)
        .map_or(Path::new(""), |l| l.source.as_path());
    (node.kind, source, node.origin.rel.as_path())
}

/// Equal resolutions. Paths are equal by components (as `PathBuf ==`); equal bytes are
/// the quick, common case.
fn same(a: Option<Resolution>, b: Option<Resolution>) -> bool {
    let path_eq = |x: &Path, y: &Path| x.as_os_str() == y.as_os_str() || x == y;
    match (a, b) {
        (None, None) => true,
        (Some(a), Some(b)) => a.0 == b.0 && path_eq(a.1, b.1) && path_eq(a.2, b.2),
        _ => false,
    }
}

impl ChangeWalk<'_> {
    /// Visits the path in `self.key`, at `depth` (the root is 0); `parent_len` is the length
    /// of its parent's key.
    fn visit(&mut self, old: Option<NodeId>, new: Option<NodeId>, parent_len: usize, depth: usize) {
        let (otree, ntree) = (self.old, self.new);
        let key = self.key.as_slice();
        let inos: Vec<u64> = self.known.of(key).collect();
        if !inos.is_empty() {
            let before = old.map(|o| resolution(otree, self.old_layers, o));
            let after = new.map(|n| resolution(ntree, self.layers, n));
            if !same(before, after) {
                self.any = true;
                self.out.extend(inos.iter().map(|&i| Inval::Inode(i)));
                if !key.is_empty() {
                    // The folded name (past the separator).
                    let start = if parent_len == 0 { 0 } else { parent_len + 1 };
                    let last = OsString::from_vec(key[start..].to_vec());
                    let names: Vec<OsString> =
                        [old.map(|o| otree.node(o)), new.map(|n| ntree.node(n))]
                            .into_iter()
                            .flatten()
                            .map(|n| n.name.clone())
                            .chain([last])
                            .collect();
                    for pino in self.known.of(&key[..parent_len]) {
                        self.out.insert(Inval::Inode(pino));
                        for name in &names {
                            self.out.insert(Inval::Entry(pino, name.clone()));
                        }
                    }
                }
            }
            // Names that did not exist before may be cached as negative entries in known
            // directories.
            if let Some(n) = new
                && ntree.node(n).kind == Kind::Dir
            {
                let old_children = old.map(|o| &otree.node(o).children);
                for (ck, &cid) in &ntree.node(n).children {
                    if !old_children.is_some_and(|c| c.contains_key(ck)) {
                        self.any = true;
                        for &ino in &inos {
                            self.out
                                .insert(Inval::Entry(ino, ntree.node(cid).name.clone()));
                            self.out.insert(Inval::Inode(ino));
                        }
                    }
                }
            }
        }
        // A name shown with another spelling over the same real file (a target chain of a
        // new layer respells an existing directory): the parent's cached listing and the
        // entry under the old spelling must go, whether this path is known or not.
        if let (Some(o), Some(n)) = (old, new)
            && !key.is_empty()
            && otree.node(o).name != ntree.node(n).name
        {
            let names = [otree.node(o).name.clone(), ntree.node(n).name.clone()];
            for pino in self.known.of(&key[..parent_len]) {
                self.any = true;
                self.out.insert(Inval::Inode(pino));
                for name in &names {
                    self.out.insert(Inval::Entry(pino, name.clone()));
                }
            }
        }
        // Children of either tree, in key order.
        let empty = BTreeMap::new();
        let oc = old.map_or(&empty, |o| &otree.node(o).children);
        let nc = new.map_or(&empty, |n| &ntree.node(n).children);
        let mut oi = oc.iter().peekable();
        let mut ni = nc.iter().peekable();
        let len = self.key.len();
        loop {
            let (ck, o, n) = match (oi.peek(), ni.peek()) {
                (None, None) => break,
                (Some((ok, _)), Some((nk, _))) if ok == nk => {
                    let (ck, &o) = oi.next().expect("peeked");
                    let (_, &n) = ni.next().expect("peeked");
                    (ck, Some(o), Some(n))
                }
                (Some((ok, _)), Some((nk, _))) if ok < nk => {
                    let (ck, &o) = oi.next().expect("peeked");
                    (ck, Some(o), None)
                }
                (Some(_), None) => {
                    let (ck, &o) = oi.next().expect("peeked");
                    (ck, Some(o), None)
                }
                _ => {
                    let (ck, &n) = ni.next().expect("peeked");
                    (ck, None, Some(n))
                }
            };
            if len > 0 {
                self.key.push(b'/');
            }
            self.key.extend_from_slice(&ck.0);
            if self.defer_at == Some(depth + 1) {
                self.deferred.push((self.key.clone(), o, n, len));
            } else {
                self.visit(o, n, len, depth + 1);
            }
            self.key.truncate(len);
        }
    }
}

/// Compares what the known inodes resolved to before and after the swap and returns the
/// kernel cache entries to drop. Takes the locks itself (tree, then inodes) and releases
/// them before returning. Each path of the two trees is visited once; the subtrees two
/// levels down are walked in parallel.
fn changes(shared: &VfsShared, old_tree: &Tree, old_layers: &Layers) -> Vec<Inval> {
    changes_on(shared, old_tree, old_layers, 64)
}

/// `changes` with at least `per_worker` subtrees per thread.
fn changes_on(
    shared: &VfsShared,
    old_tree: &Tree,
    old_layers: &Layers,
    per_worker: usize,
) -> Vec<Inval> {
    let tree = shared.tree.read();
    let layers = shared.layers();
    // Held for the whole walk (about 27 ms at 2000 mods / 320000 entries): handlers that
    // need an inode number wait that long during an update.
    let inodes = shared.inodes.lock();
    let known = Known::new(&inodes);
    let walker = |key: Vec<u8>, defer_at| ChangeWalk {
        old: old_tree,
        old_layers,
        new: &tree,
        layers: &layers,
        known: &known,
        key,
        out: HashSet::new(),
        any: false,
        defer_at,
        deferred: Vec::new(),
    };
    let mut top = walker(Vec::new(), Some(2));
    top.visit(Some(old_tree.root()), Some(tree.root()), 0, 0);
    let deferred = std::mem::take(&mut top.deferred);
    let (mut out, mut any) = (top.out, top.any);
    let workers = std::thread::available_parallelism()
        .map_or(1, |n| n.get())
        .min(deferred.len().div_ceil(per_worker))
        .max(1);
    let next = AtomicUsize::new(0);
    let work = || {
        let mut w = walker(Vec::new(), None);
        while let Some((key, o, n, parent_len)) = deferred.get(next.fetch_add(1, Ordering::Relaxed))
        {
            w.key.clone_from(key);
            w.visit(*o, *n, *parent_len, 2);
        }
        (w.out, w.any)
    };
    let parts: Vec<(HashSet<Inval>, bool)> = if workers == 1 {
        vec![work()]
    } else {
        std::thread::scope(|s| {
            let handles: Vec<_> = (0..workers).map(|_| s.spawn(work)).collect();
            handles
                .into_iter()
                .map(|h| h.join().expect("change walk thread panicked"))
                .collect()
        })
    };
    for (o, a) in parts {
        out.extend(o);
        any |= a;
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

/// `fusermount3 -uz` (lazy) at once.
pub fn unmount_lazy(path: &Path) -> Result<(), String> {
    let out = Command::new("fusermount3")
        .arg("-uz")
        .arg(path)
        .output()
        .map_err(|e| format!("cannot run fusermount3: {e}"))?;
    if out.status.success() {
        Ok(())
    } else {
        Err(String::from_utf8_lossy(&out.stderr).trim().to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fs::Vfs;
    use crate::mapping::LayerSpec;
    use crate::name::fold_path;
    use tempfile::TempDir;

    /// `changes` as it was before the joint walk (a lookup of every known inode in both
    /// trees); the tests compare the walk against it.
    mod reference {
        use super::super::*;
        use crate::name::{Key, fold};

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

        /// Compares what the known inodes resolved to before and after the swap and returns the
        /// kernel cache entries to drop. Takes the locks itself (tree, then inodes) and releases
        /// them before returning.
        pub(super) fn changes(
            shared: &VfsShared,
            old_tree: &Tree,
            old_layers: &Layers,
        ) -> Vec<Inval> {
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
                let last =
                    OsString::from_vec(key.0.rsplit(|&b| b == b'/').next().unwrap_or(&[]).to_vec());
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
    }

    struct Fx {
        tmp: TempDir,
        dirs: Vec<PathBuf>,
    }

    /// Pseudo-random layers over a small vocabulary (conflicts, case variants,
    /// directory/file clashes).
    fn layers_fixture(n: usize, seed: u64) -> Fx {
        let tmp = TempDir::new().unwrap();
        let mut seed = seed;
        let mut next = move || {
            seed = seed
                .wrapping_mul(6364136223846793005)
                .wrapping_add(1442695040888963407);
            (seed >> 33) as usize
        };
        let words = ["Tex", "tex", "MESH", "a", "B", "b.dds", "B.DDS", "c"];
        let mut dirs = Vec::new();
        for l in 0..n {
            let base = tmp.path().join(format!("L{l:02}"));
            std::fs::create_dir_all(&base).unwrap();
            for _ in 0..12 {
                let depth = 1 + next() % 3;
                let parts: Vec<&str> = (0..depth).map(|_| words[next() % words.len()]).collect();
                let p = base.join(parts.join("/"));
                if std::fs::create_dir_all(p.parent().unwrap()).is_ok() && !p.is_dir() {
                    let _ = std::fs::write(&p, b"x");
                }
            }
            dirs.push(base);
        }
        std::fs::create_dir_all(tmp.path().join("orig/tex")).unwrap();
        std::fs::write(tmp.path().join("orig/tex/o.dds"), b"o").unwrap();
        Fx { tmp, dirs }
    }

    fn open(fx: &Fx, order: &[usize]) -> Layers {
        let root = RootSpec {
            mountpoint: fx.tmp.path().join("orig"),
            layers: order
                .iter()
                .enumerate()
                .map(|(i, &l)| LayerSpec {
                    source: fx.dirs[l].clone(),
                    target: PathBuf::new(),
                    is_directory: true,
                    create_target: false,
                    priority: i,
                })
                .collect(),
        };
        open_layers(&root).unwrap()
    }

    fn all_paths(tree: &Tree) -> Vec<PathBuf> {
        let mut out = Vec::new();
        let mut todo = vec![tree.root()];
        while let Some(id) = todo.pop() {
            out.push(tree.path_of(id));
            todo.extend(tree.children(id).map(|(c, _)| c));
        }
        out
    }

    /// Swaps `before` for `after` and returns (reference, walk) results.
    fn compare(
        fx: &Fx,
        before: &[usize],
        after: &[usize],
        known: impl Fn(&Tree, &mut Inodes),
    ) -> (HashSet<Inval>, HashSet<Inval>) {
        let layers = open(fx, before);
        let tree = Tree::build(&layers, &Skip::default());
        let shared = Vfs::new(layers, tree, VfsOptions::default()).shared();
        {
            let tree = shared.tree.read();
            known(&tree, &mut shared.inodes.lock());
        }
        let new_layers = Arc::new(open(fx, after));
        let new_tree = Tree::build(&new_layers, &Skip::default());
        let (old_tree, old_layers) = shared
            .swap_if(new_tree, new_layers, shared.generation())
            .ok()
            .unwrap();
        let mut r: HashSet<Inval> = reference::changes(&shared, &old_tree, &old_layers)
            .into_iter()
            .collect();
        // What the reference missed: a path shown with another spelling over the same real
        // file drops the parent's entries for both spellings (worked out by brute force).
        let mut respelled = HashSet::new();
        {
            let tree = shared.tree.read();
            let inodes = shared.inodes.lock();
            for p in all_paths(&old_tree) {
                let (Some(parent), Some(o), Some(n)) = (
                    p.parent(),
                    old_tree.lookup_folded(&fold_path(&p)),
                    tree.lookup_folded(&fold_path(&p)),
                ) else {
                    continue;
                };
                let (on, nn) = (&old_tree.node(o).name, &tree.node(n).name);
                if on == nn {
                    continue;
                }
                let pk = fold_path(parent);
                for (ino, _) in inodes.known().filter(|(_, k)| **k == pk) {
                    respelled.insert(Inval::Inode(ino));
                    respelled.insert(Inval::Entry(ino, on.clone()));
                    respelled.insert(Inval::Entry(ino, nn.clone()));
                }
            }
        }
        if !respelled.is_empty() {
            respelled.insert(Inval::Inode(crate::inodes::ROOT_INO));
        }
        r.extend(respelled);
        let w: HashSet<Inval> = changes(&shared, &old_tree, &old_layers)
            .into_iter()
            .collect();
        // the same with every subtree on its own thread
        let p: HashSet<Inval> = changes_on(&shared, &old_tree, &old_layers, 1)
            .into_iter()
            .collect();
        assert_eq!(w, p);
        (r, w)
    }

    #[test]
    fn change_walk_equals_reference() {
        let fx = layers_fixture(12, 3);
        let every = |t: &Tree, i: &mut Inodes| {
            for p in all_paths(t) {
                i.ino_for(&fold_path(&p));
            }
            // known names that exist in neither tree
            i.ino_for(&fold_path(Path::new("nope/x")));
            i.ino_for(&fold_path(Path::new("tex/zz")));
        };
        let some = |t: &Tree, i: &mut Inodes| {
            for p in all_paths(t).into_iter().step_by(3) {
                i.ino_for(&fold_path(&p));
            }
        };
        let cases: [(&[usize], &[usize]); 6] = [
            (&[0, 1, 2, 3], &[0, 1, 2, 3]),
            (&[0, 1, 2, 3], &[0, 1, 3]),
            (&[0, 1, 2], &[0, 1, 2, 4, 5]),
            (&[0, 1, 2, 3, 4], &[4, 3, 2, 1, 0]),
            (&[5, 6, 7], &[8, 9, 10, 11]),
            (&[], &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]),
        ];
        for (before, after) in cases {
            for known in [&every as &dyn Fn(&Tree, &mut Inodes), &some] {
                let (r, w) = compare(&fx, before, after, known);
                assert_eq!(r, w, "{before:?} -> {after:?}");
            }
            // and the other way round
            let (r, w) = compare(&fx, after, before, every);
            assert_eq!(r, w, "{after:?} -> {before:?}");
        }
        // the cases do change things
        let (r, _) = compare(&fx, &[0, 1, 2, 3], &[0, 1, 3], every);
        assert!(r.len() > 3, "{r:?}");
    }

    #[test]
    fn change_walk_with_aliases_covers_reference() {
        // Aliases (several numbers for one path, left by a rename that replaced a name):
        // the reference invalidates through one of a parent's numbers, picked by hash
        // order; the walk through all of them.
        let fx = layers_fixture(6, 11);
        let known = |t: &Tree, i: &mut Inodes| {
            for p in all_paths(t) {
                i.ino_for(&fold_path(&p));
            }
            let a = fold_path(Path::new("moved"));
            i.ino_for(&a);
            for p in all_paths(t) {
                if !p.as_os_str().is_empty() {
                    i.rekey_prefix(&a, &fold_path(&p));
                    break;
                }
            }
        };
        let (r, w) = compare(&fx, &[0, 1, 2], &[0, 2, 3, 4], known);
        assert!(
            w.is_superset(&r),
            "{:?}",
            r.difference(&w).collect::<Vec<_>>()
        );
    }
}
