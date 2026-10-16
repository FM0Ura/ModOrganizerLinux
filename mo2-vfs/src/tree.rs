//! Merged virtual tree built from the layers.

use crate::layer::{Layer, LayerId, Layers, Skip};
use crate::name::{Key, fold, fold_into, fold_path};
use rustix::fs::{AtFlags, Dir, FileType, Mode, OFlags, Stat, openat, statat};
use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::ffi::{OsStr, OsString};
use std::hash::{BuildHasherDefault, Hasher};
use std::os::fd::{BorrowedFd, OwnedFd};
use std::os::unix::ffi::OsStrExt;
use std::path::{Component, Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::mpsc;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    File,
    Dir,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Origin {
    pub layer: LayerId,
    /// Relative to the layer fd.
    pub rel: PathBuf,
}

pub type NodeId = usize;

#[derive(Clone, Debug)]
pub struct Node {
    /// Displayed spelling.
    pub name: OsString,
    pub kind: Kind,
    pub parent: NodeId,
    /// Winning layer.
    pub origin: Origin,
    /// Dir only: every layer that has the directory, lowest to highest priority.
    pub dir_origins: Vec<Origin>,
    pub children: BTreeMap<Key, NodeId>,
}

/// Prefix of the temporary files the VFS creates while copying an original file; never
/// shown from mapping layers.
pub const TMP_PREFIX: &str = ".mo2-vfs-tmp-";

/// Entries hidden by the VFS itself (deletions and renames); they outlive tree rebuilds.
#[derive(Default, Clone, Debug, PartialEq, Eq)]
pub struct Whiteouts {
    /// Hide every layer at this (folded) virtual path.
    pub paths: BTreeSet<Key>,
    /// Hide one layer's entry at a path: (`source` of the layer, folded virtual path).
    pub origins: BTreeSet<(PathBuf, Key)>,
}

pub struct Tree {
    nodes: Vec<Node>,
    whiteouts: Whiteouts,
}

impl Tree {
    /// Builds the merged tree without whiteouts.
    pub fn build(layers: &Layers, skip: &Skip) -> Tree {
        Tree::build_with(layers, skip, &Whiteouts::default())
    }

    /// Builds the merged tree: layers in increasing priority, the last one wins. Origin
    /// whiteouts are applied while scanning (the next layer down then shows through); path
    /// whiteouts remove the node afterwards.
    ///
    /// The layers are scanned in parallel (one task per layer, `available_parallelism`
    /// workers) and merged one by one in priority order on the calling thread, so the result
    /// is the same as scanning and merging them in sequence.
    pub fn build_with(layers: &Layers, skip: &Skip, whiteouts: &Whiteouts) -> Tree {
        let workers = std::thread::available_parallelism().map_or(1, |n| n.get());
        Tree::build_with_workers(layers, skip, whiteouts, workers)
    }

    fn build_with_workers(
        layers: &Layers,
        skip: &Skip,
        whiteouts: &Whiteouts,
        workers: usize,
    ) -> Tree {
        let mut b = Builder::new();
        let list = &layers.list;
        let workers = workers.clamp(1, list.len().max(1));
        if workers == 1 {
            for layer in list {
                b.add_layer(layer, scan_layer(layer, skip, whiteouts));
            }
        } else {
            let next = AtomicUsize::new(0);
            std::thread::scope(|s| {
                let (tx, rx) = mpsc::channel::<(usize, Scan)>();
                for _ in 0..workers {
                    let (tx, next) = (tx.clone(), &next);
                    s.spawn(move || {
                        loop {
                            let i = next.fetch_add(1, Ordering::Relaxed);
                            let Some(layer) = list.get(i) else { break };
                            if tx.send((i, scan_layer(layer, skip, whiteouts))).is_err() {
                                break;
                            }
                        }
                    });
                }
                drop(tx);
                // Scans finish in any order; merge strictly in priority order.
                let mut pending: HashMap<usize, Scan> = HashMap::new();
                let mut want = 0;
                for (i, scan) in rx {
                    pending.insert(i, scan);
                    while let Some(scan) = pending.remove(&want) {
                        b.add_layer(&list[want], scan);
                        want += 1;
                    }
                }
            });
        }
        let mut tree = Tree {
            nodes: b.finish(workers),
            whiteouts: whiteouts.clone(),
        };
        for key in &whiteouts.paths {
            if let Some(id) = tree.lookup_key(key) {
                tree.remove(id);
            }
        }
        tree
    }

    /// What the VFS itself has hidden; pass it to `build_with` when rebuilding.
    pub fn whiteouts(&self) -> &Whiteouts {
        &self.whiteouts
    }

    /// Hides every layer at `vpath` on rebuilds. Does not touch the nodes.
    pub fn whiteout_path(&mut self, vpath: &Path) {
        let key = fold_path(vpath);
        if !key.0.is_empty() {
            self.whiteouts.paths.insert(key);
        }
    }

    /// Hides the entry of the layer whose `source` is given at `vpath` on rebuilds.
    pub fn whiteout_origin(&mut self, source: &Path, vpath: &Path) {
        self.whiteouts
            .origins
            .insert((source.to_path_buf(), fold_path(vpath)));
    }

    /// Node at a folded path key (components joined by `/`).
    fn lookup_key(&self, key: &Key) -> Option<NodeId> {
        if key.0.is_empty() {
            return None;
        }
        let mut cur = 0;
        for comp in key.0.split(|&b| b == b'/') {
            cur = *self.nodes[cur].children.get(comp)?;
        }
        Some(cur)
    }

    /// Adds `node` as a child of `parent`, replacing a child with the same folded name
    /// (the replaced node keeps its id). Clears the path whiteout on the resulting path.
    pub fn insert(&mut self, parent: NodeId, mut node: Node) -> NodeId {
        let key = fold(&node.name);
        node.parent = parent;
        let id = match self.nodes[parent].children.get(&key) {
            Some(&id) => {
                self.nodes[id] = node;
                id
            }
            None => {
                let id = self.nodes.len();
                self.nodes.push(node);
                self.nodes[parent].children.insert(key, id);
                id
            }
        };
        self.unmask_subtree(id);
        id
    }

    /// Detaches `id` from its parent (its subtree becomes unreachable). The root stays.
    pub fn remove(&mut self, id: NodeId) {
        if id == 0 {
            return;
        }
        let parent = self.nodes[id].parent;
        let key = fold(&self.nodes[id].name);
        if self.nodes[parent].children.get(&key) == Some(&id) {
            self.nodes[parent].children.remove(&key);
        }
    }

    /// Moves `id` (keeping its id and subtree) under `new_parent` as `new_name`. A node that
    /// already has that name there is detached. Clears path whiteouts on the moved paths.
    pub fn move_node(&mut self, id: NodeId, new_parent: NodeId, new_name: &OsStr) {
        if id == 0 {
            return;
        }
        self.remove(id);
        let key = fold(new_name);
        self.nodes[new_parent].children.remove(&key);
        let node = &mut self.nodes[id];
        node.name = new_name.to_os_string();
        node.parent = new_parent;
        self.nodes[new_parent].children.insert(key, id);
        self.unmask_subtree(id);
    }

    fn unmask_subtree(&mut self, id: NodeId) {
        if self.whiteouts.paths.is_empty() {
            return;
        }
        let mut todo = vec![(id, self.path_of(id))];
        while let Some((n, path)) = todo.pop() {
            self.whiteouts.paths.remove(&fold_path(&path));
            for &c in self.nodes[n].children.values() {
                todo.push((c, path.join(&self.nodes[c].name)));
            }
        }
    }

    /// Rewrites, for `id` and its descendants, the origins of `layer` whose `rel` starts with
    /// `old` so that they start with `new` (after a real directory rename).
    pub fn rebase_origins(&mut self, id: NodeId, layer: LayerId, old: &Path, new: &Path) {
        let fix = |o: &mut Origin| {
            if o.layer == layer
                && let Ok(rest) = o.rel.strip_prefix(old)
            {
                o.rel = new.join(rest);
            }
        };
        let mut todo = vec![id];
        while let Some(n) = todo.pop() {
            let node = &mut self.nodes[n];
            fix(&mut node.origin);
            node.dir_origins.iter_mut().for_each(fix);
            todo.extend(node.children.values());
        }
    }

    /// Mutable access for operations that retarget a node's origin.
    pub(crate) fn node_mut(&mut self, id: NodeId) -> &mut Node {
        &mut self.nodes[id]
    }

    pub fn root(&self) -> NodeId {
        0
    }

    pub fn node(&self, id: NodeId) -> &Node {
        &self.nodes[id]
    }

    pub fn lookup(&self, vpath: &Path) -> Option<NodeId> {
        let mut cur = 0;
        for c in vpath.components() {
            match c {
                Component::Normal(n) => cur = self.child(cur, n)?,
                Component::ParentDir => return None,
                _ => {}
            }
        }
        Some(cur)
    }

    /// Lookup by folded path (the `fold_path` form); the empty key is the root.
    pub fn lookup_folded(&self, key: &Key) -> Option<NodeId> {
        if key.0.is_empty() {
            Some(0)
        } else {
            self.lookup_key(key)
        }
    }

    pub fn child(&self, dir: NodeId, name: &OsStr) -> Option<NodeId> {
        self.nodes[dir].children.get(&fold(name)).copied()
    }

    pub fn children(&self, dir: NodeId) -> impl Iterator<Item = (NodeId, &Node)> {
        self.nodes[dir]
            .children
            .values()
            .map(|&id| (id, &self.nodes[id]))
    }

    /// Path with the displayed spelling, relative to the root.
    pub fn path_of(&self, id: NodeId) -> PathBuf {
        let mut parts = Vec::new();
        let mut cur = id;
        while cur != 0 {
            parts.push(self.nodes[cur].name.as_os_str());
            cur = self.nodes[cur].parent;
        }
        parts.iter().rev().collect()
    }

    /// Number of nodes reachable from the root (the root included).
    pub fn len(&self) -> usize {
        let mut count = 0;
        let mut todo = vec![0];
        while let Some(id) = todo.pop() {
            count += 1;
            todo.extend(self.nodes[id].children.values());
        }
        count
    }

    pub fn is_empty(&self) -> bool {
        false
    }
}

/// Parent index of the entries directly under the layer root.
const LAYER_ROOT: u32 = u32::MAX;

/// A byte range in `DirScan::bytes`.
#[derive(Clone, Copy)]
struct Span(u32, u32);

impl Span {
    fn of(self, bytes: &[u8]) -> &[u8] {
        &bytes[self.0 as usize..self.1 as usize]
    }
}

/// One entry of a directory layer, as found by `scan_layer`.
struct Scanned {
    /// Index of the parent directory in the same scan, or `LAYER_ROOT`.
    parent: u32,
    kind: Kind,
    name: Span,
    /// The folded name, computed once.
    key: Span,
    /// Path relative to the layer fd.
    rel: Span,
}

/// Every entry of a directory layer in depth-first order (a directory before its contents,
/// names in byte order inside a directory). The strings live in one buffer, so a scan is a
/// few allocations whichever thread frees it.
#[derive(Default)]
struct DirScan {
    bytes: Vec<u8>,
    entries: Vec<Scanned>,
}

impl DirScan {
    fn push(&mut self, bytes: &[u8]) -> Span {
        let start = self.bytes.len() as u32;
        self.bytes.extend_from_slice(bytes);
        Span(start, self.bytes.len() as u32)
    }
}

/// What one layer contributes, before it is merged.
enum Scan {
    /// A file mapping: the kind of the mapped file, `None` when it is hidden or unavailable.
    File(Option<Kind>),
    Dir(DirScan),
}

/// The normal components of the layer's target.
fn target_components(layer: &Layer) -> Vec<&OsStr> {
    layer
        .target
        .components()
        .filter_map(|c| match c {
            Component::Normal(n) => Some(n),
            _ => None,
        })
        .collect()
}

/// Reads one layer from the disk. Touches nothing shared, so layers can be scanned in
/// parallel; the merge (`Builder::add_layer`) then happens in priority order.
fn scan_layer(layer: &Layer, skip: &Skip, whiteouts: &Whiteouts) -> Scan {
    // Folded virtual paths hidden for this layer by origin whiteouts.
    let hidden: HashSet<&[u8]> = whiteouts
        .origins
        .iter()
        .filter(|(source, _)| *source == layer.source)
        .map(|(_, key)| key.0.as_slice())
        .collect();
    let target = fold_path(&layer.target);
    if let Some(file) = &layer.file {
        if target.0.is_empty() || hidden.contains(target.0.as_slice()) {
            return Scan::File(None);
        }
        let kind = entry_kind(&layer.dir, file, &layer.source);
        if kind.is_none() {
            tracing::warn!(source = %layer.source.display(), "mapped file unavailable");
        }
        return Scan::File(kind);
    }
    let mut scanner = Scanner {
        layer,
        skip: if layer.original {
            // The original folder is shown as-is: skip rules only apply to mapping layers.
            &NO_SKIP
        } else {
            skip
        },
        hidden,
        vkey: target.0,
        stack: Vec::new(),
        scan: DirScan::default(),
    };
    let dir = match Dir::read_from(&layer.dir) {
        Ok(dir) => dir,
        Err(e) => {
            tracing::warn!(layer = %layer.source.display(), rel = "", error = %e, "cannot list directory");
            return Scan::Dir(scanner.scan);
        }
    };
    let mut searchable = false;
    if let Ok(st) = rustix::fs::fstat(&layer.dir) {
        scanner.stack.push(dev_ino(&st));
        searchable = all_can_search(st.st_mode);
    }
    scanner.dir(dir, None, LAYER_ROOT, searchable);
    Scan::Dir(scanner.scan)
}

/// Identity of a directory, for loop detection.
#[allow(clippy::unnecessary_cast)] // the integer widths of `Stat` differ per target
fn dev_ino(st: &Stat) -> (u64, u64) {
    (st.st_dev as u64, st.st_ino as u64)
}

static NO_SKIP: Skip = Skip {
    suffixes: Vec::new(),
    directories: Vec::new(),
};

/// Whether everybody may search a directory with this mode. Only then is the kind in a
/// directory entry trusted without a stat: in a directory the process cannot search, the
/// stat of every entry fails and the entry is skipped, as it always was.
fn all_can_search(mode: u32) -> bool {
    mode & 0o111 == 0o111
}

struct Scanner<'a> {
    layer: &'a Layer,
    skip: &'a Skip,
    hidden: HashSet<&'a [u8]>,
    /// Folded virtual path of the directory being scanned (kept only when `hidden` is
    /// not empty).
    vkey: Vec<u8>,
    /// (device, inode) of the directories being scanned, to stop at symlink loops.
    stack: Vec<(u64, u64)>,
    scan: DirScan,
}

impl Scanner<'_> {
    /// Lists `dir`, whose path relative to the layer is `rel` (`None` for the layer root)
    /// and whose entry is `parent`. `searchable`: see `all_can_search`.
    fn dir(&mut self, mut dir: Dir, rel: Option<Span>, parent: u32, searchable: bool) {
        // Names of this directory, sorted by their bytes.
        let mut buf: Vec<u8> = Vec::new();
        let mut names: Vec<(Span, FileType)> = Vec::new();
        while let Some(Ok(e)) = dir.read() {
            let name = e.file_name().to_bytes();
            if name != b"." && name != b".." {
                let start = buf.len() as u32;
                buf.extend_from_slice(name);
                names.push((Span(start, buf.len() as u32), e.file_type()));
            }
        }
        names.sort_unstable_by(|a, b| a.0.of(&buf).cmp(b.0.of(&buf)));
        let Ok(dirfd) = dir.fd() else { return };
        let mut key = Vec::new();
        for (name, dtype) in names {
            let name = name.of(&buf);
            if !self.layer.original && name.starts_with(TMP_PREFIX.as_bytes()) {
                continue;
            }
            key.clear();
            fold_into(name, &mut key);
            let vlen = self.vkey.len();
            if !self.hidden.is_empty() {
                if !self.vkey.is_empty() {
                    self.vkey.push(b'/');
                }
                self.vkey.extend_from_slice(&key);
                if self.hidden.contains(self.vkey.as_slice()) {
                    self.vkey.truncate(vlen);
                    continue;
                }
            }
            let dtype = if searchable { dtype } else { FileType::Unknown };
            self.entry(dirfd, rel, parent, name, &key, dtype);
            self.vkey.truncate(vlen);
        }
    }

    /// The real path of `name` (in the directory at `rel`), for log messages.
    fn shown(&self, rel: Option<Span>, name: &[u8]) -> PathBuf {
        let mut p = self.layer.source.clone();
        if let Some(r) = rel {
            p.push(OsStr::from_bytes(r.of(&self.scan.bytes)));
        }
        p.push(OsStr::from_bytes(name));
        p
    }

    /// Records `name` (in the directory `dirfd`, at `rel`) and, for a directory, its
    /// contents.
    fn entry(
        &mut self,
        dirfd: BorrowedFd<'_>,
        rel: Option<Span>,
        parent: u32,
        name: &[u8],
        key: &[u8],
        dtype: FileType,
    ) {
        // The kind from the directory entry is enough for plain files and directories;
        // anything else (links, unknown) is resolved with a stat that follows links.
        let (is_dir, statted) = match dtype {
            FileType::RegularFile => (false, None),
            FileType::Directory => (true, None),
            _ => match statat(dirfd, name, AtFlags::empty()) {
                Ok(st) => (
                    FileType::from_raw_mode(st.st_mode) == FileType::Directory,
                    Some((dev_ino(&st), st.st_mode)),
                ),
                Err(e) => {
                    tracing::warn!(path = %self.shown(rel, name).display(), error = %e, "skipping unreadable entry or broken link");
                    return;
                }
            },
        };
        if !is_dir {
            if self.skip.skips_file_key(key) {
                return;
            }
            self.record(rel, parent, name, key, Kind::File);
            return;
        }
        if self.skip.skips_dir_key(key) {
            return;
        }
        if let Some((id, _)) = statted
            && self.stack.contains(&id)
        {
            tracing::warn!(path = %self.shown(rel, name).display(), "symlink loop, skipping directory");
            return;
        }
        let sub = match openat(
            dirfd,
            name,
            OFlags::RDONLY | OFlags::DIRECTORY | OFlags::CLOEXEC,
            Mode::empty(),
        ) {
            Ok(fd) => fd,
            Err(e) => {
                tracing::warn!(path = %self.shown(rel, name).display(), error = %e, "cannot open directory");
                return;
            }
        };
        let (id, mode) = match statted {
            Some(st) => st,
            None => match rustix::fs::fstat(&sub) {
                Ok(st) => (dev_ino(&st), st.st_mode),
                Err(e) => {
                    tracing::warn!(path = %self.shown(rel, name).display(), error = %e, "skipping unreadable entry or broken link");
                    return;
                }
            },
        };
        // A plain directory entry can still close a loop (through a bind mount).
        if self.stack.contains(&id) {
            tracing::warn!(path = %self.shown(rel, name).display(), "symlink loop, skipping directory");
            return;
        }
        let index = self.scan.entries.len() as u32;
        let rel = self.record(rel, parent, name, key, Kind::Dir);
        // The fd is ours: read it directly (no reopen of "." as `Dir::read_from` does).
        let Ok(dir) = Dir::new(sub) else { return };
        self.stack.push(id);
        self.dir(dir, Some(rel), index, all_can_search(mode));
        self.stack.pop();
    }

    /// Appends an entry; returns its path relative to the layer.
    fn record(
        &mut self,
        rel: Option<Span>,
        parent: u32,
        name: &[u8],
        key: &[u8],
        kind: Kind,
    ) -> Span {
        let s = &mut self.scan;
        let name_span = s.push(name);
        let key_span = s.push(key);
        let start = s.bytes.len() as u32;
        if let Some(r) = rel {
            s.bytes.extend_from_within(r.0 as usize..r.1 as usize);
            s.bytes.push(b'/');
        }
        s.bytes.extend_from_slice(name);
        let rel_span = Span(start, s.bytes.len() as u32);
        s.entries.push(Scanned {
            parent,
            kind,
            name: name_span,
            key: key_span,
            rel: rel_span,
        });
        rel_span
    }
}

/// A fast hasher (the multiply-rotate scheme of FxHash) for the merge index: the keys are
/// short names and the map only lives during a build.
#[derive(Default)]
struct FxHasher(u64);

impl FxHasher {
    fn add(&mut self, word: u64) {
        self.0 = (self.0.rotate_left(5) ^ word).wrapping_mul(0x51_7c_c1_b7_27_22_0a_95);
    }
}

impl Hasher for FxHasher {
    fn write(&mut self, bytes: &[u8]) {
        let (chunks, rest) = bytes.as_chunks::<8>();
        for c in chunks {
            self.add(u64::from_le_bytes(*c));
        }
        if !rest.is_empty() {
            let mut w = [0u8; 8];
            w[..rest.len()].copy_from_slice(rest);
            self.add(u64::from_le_bytes(w));
        }
    }

    fn write_usize(&mut self, n: usize) {
        self.add(n as u64);
    }

    fn finish(&self) -> u64 {
        self.0
    }
}

/// Builds the node list of a new tree, one layer at a time in priority order.
///
/// Children are found through one hash index instead of the nodes' `BTreeMap`s, which are
/// built in bulk by `finish` from `kids`. The index key is the parent's *slot* (8 bytes)
/// followed by the folded name; replacing a node gives it a new slot and empties its `kids`,
/// which drops all its children at once (as `children.clear()` would).
struct Builder {
    nodes: Vec<Node>,
    /// Current slot of each node.
    slot: Vec<u64>,
    next_slot: u64,
    index: HashMap<Vec<u8>, NodeId, BuildHasherDefault<FxHasher>>,
    /// The index key of the last `find`.
    probe: Vec<u8>,
    /// Children of each node, in no particular order.
    kids: Vec<Vec<NodeId>>,
}

impl Builder {
    fn new() -> Builder {
        let root = Node {
            name: OsString::new(),
            kind: Kind::Dir,
            parent: 0,
            origin: Origin {
                layer: 0,
                rel: PathBuf::from("."),
            },
            dir_origins: Vec::new(),
            children: BTreeMap::new(),
        };
        Builder {
            nodes: vec![root],
            slot: vec![0],
            next_slot: 1,
            index: HashMap::default(),
            probe: Vec::new(),
            kids: vec![Vec::new()],
        }
    }

    /// Merges the scan of `layer` (the next one in priority order).
    fn add_layer(&mut self, layer: &Layer, scan: Scan) {
        let mut parent = 0;
        let mut comps = target_components(layer);
        let scan = match scan {
            Scan::File(kind) => {
                // A file mapping places one node; its name is the last target component.
                let Some(name) = comps.pop() else { return };
                for c in comps {
                    parent = self.ensure_dir(parent, c, layer.id);
                }
                if let Some(kind) = kind {
                    let origin = Origin {
                        layer: layer.id,
                        rel: PathBuf::from(layer.file.as_deref().unwrap_or_default()),
                    };
                    self.put(parent, &fold(name).0, name.as_bytes(), kind, origin);
                }
                return;
            }
            Scan::Dir(scan) => scan,
        };
        for c in comps {
            parent = self.ensure_dir(parent, c, layer.id);
        }
        // The layer root is the target directory (the original layer does not take over
        // the root's origin).
        let origin = Origin {
            layer: layer.id,
            rel: PathBuf::from("."),
        };
        let root = &mut self.nodes[parent];
        if parent != 0 || !layer.original {
            root.origin = origin.clone();
        }
        root.dir_origins.push(origin);
        // `ids[i]` is the node of `entries[i]`; parents come before their contents.
        let mut ids: Vec<NodeId> = Vec::with_capacity(scan.entries.len());
        for e in &scan.entries {
            let at = if e.parent == LAYER_ROOT {
                parent
            } else {
                ids[e.parent as usize]
            };
            let origin = Origin {
                layer: layer.id,
                rel: PathBuf::from(OsStr::from_bytes(e.rel.of(&scan.bytes))),
            };
            let (key, name) = (e.key.of(&scan.bytes), e.name.of(&scan.bytes));
            ids.push(self.put(at, key, name, e.kind, origin));
        }
    }

    /// The child `key` of `parent`; leaves its index key in `probe`.
    fn find(&mut self, parent: NodeId, key: &[u8]) -> Option<NodeId> {
        self.probe.clear();
        self.probe
            .extend_from_slice(&self.slot[parent].to_le_bytes());
        self.probe.extend_from_slice(key);
        self.index.get(self.probe.as_slice()).copied()
    }

    /// Returns the directory child `name` of `parent`, creating a synthetic one (no real
    /// directory behind it in this layer) when absent or when it is a file.
    fn ensure_dir(&mut self, parent: NodeId, name: &OsStr, layer: LayerId) -> NodeId {
        let key = fold(name);
        if let Some(id) = self.find(parent, &key.0)
            && self.nodes[id].kind == Kind::Dir
        {
            self.nodes[id].name = name.to_os_string();
            return id;
        }
        let origin = Origin {
            layer,
            rel: PathBuf::from("."),
        };
        let id = self.put(parent, &key.0, name.as_bytes(), Kind::Dir, origin);
        self.nodes[id].dir_origins.clear();
        id
    }

    /// Inserts or overwrites the child `key` (displayed as `name`) of `parent`. A directory
    /// over a directory merges (keeping the children); any other combination replaces the
    /// node.
    fn put(
        &mut self,
        parent: NodeId,
        key: &[u8],
        name: &[u8],
        kind: Kind,
        origin: Origin,
    ) -> NodeId {
        if let Some(id) = self.find(parent, key) {
            let node = &mut self.nodes[id];
            if node.name.as_bytes() != name {
                node.name = OsStr::from_bytes(name).to_os_string();
            }
            if kind == Kind::Dir && node.kind == Kind::Dir {
                node.origin = origin.clone();
                node.dir_origins.push(origin);
            } else {
                node.kind = kind;
                node.dir_origins = if kind == Kind::Dir {
                    vec![origin.clone()]
                } else {
                    Vec::new()
                };
                node.origin = origin;
                // The old subtree becomes unreachable.
                self.slot[id] = self.next_slot;
                self.next_slot += 1;
                self.kids[id].clear();
            }
            return id;
        }
        let id = self.nodes.len();
        self.nodes.push(Node {
            name: OsStr::from_bytes(name).to_os_string(),
            kind,
            parent,
            dir_origins: if kind == Kind::Dir {
                vec![origin.clone()]
            } else {
                Vec::new()
            },
            origin,
            children: BTreeMap::new(),
        });
        self.slot.push(self.next_slot);
        self.next_slot += 1;
        self.kids.push(Vec::new());
        self.kids[parent].push(id);
        self.index.insert(self.probe.clone(), id);
        id
    }

    /// The nodes, with their child maps (built on `workers` threads).
    fn finish(self, workers: usize) -> Vec<Node> {
        let Builder {
            mut nodes,
            index,
            kids,
            ..
        } = self;
        drop(index);
        // A node's key is the folded form of its (latest) displayed name.
        let maps = |range: std::ops::Range<usize>| -> Vec<(NodeId, BTreeMap<Key, NodeId>)> {
            range
                .filter(|&p| !kids[p].is_empty())
                .map(|p| {
                    let map = kids[p].iter().map(|&c| (fold(&nodes[c].name), c)).collect();
                    (p, map)
                })
                .collect()
        };
        let n = nodes.len();
        let workers = workers.clamp(1, n.div_ceil(4096).max(1));
        let built: Vec<Vec<(NodeId, BTreeMap<Key, NodeId>)>> = if workers == 1 {
            vec![maps(0..n)]
        } else {
            let chunk = n.div_ceil(workers);
            std::thread::scope(|s| {
                let handles: Vec<_> = (0..workers)
                    .map(|w| {
                        let maps = &maps;
                        s.spawn(move || maps(w * chunk..((w + 1) * chunk).min(n)))
                    })
                    .collect();
                handles
                    .into_iter()
                    .map(|h| h.join().expect("tree build thread panicked"))
                    .collect()
            })
        };
        for (p, map) in built.into_iter().flatten() {
            nodes[p].children = map;
        }
        nodes
    }
}

/// Kind of `name` inside `dir`, following links; `None` when missing or a broken link.
fn entry_kind(dir: &OwnedFd, name: &OsStr, source: &Path) -> Option<Kind> {
    match statat(dir, name.as_bytes(), AtFlags::empty()) {
        Ok(st) if FileType::from_raw_mode(st.st_mode) == FileType::Directory => Some(Kind::Dir),
        Ok(_) => Some(Kind::File),
        Err(e) => {
            tracing::warn!(source = %source.display(), error = %e, "cannot stat mapped file");
            None
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::layer::open_layers;
    use crate::mapping::{LayerSpec, RootSpec};
    use std::os::unix::ffi::OsStrExt;
    use tempfile::TempDir;

    struct Fx {
        _tmp: TempDir,
        tree: Tree,
    }

    fn touch(base: &Path, rel: &str) {
        let p = base.join(rel);
        std::fs::create_dir_all(p.parent().unwrap()).unwrap();
        std::fs::write(p, b"x").unwrap();
    }

    fn spec(source: PathBuf, target: &str, is_directory: bool, priority: usize) -> LayerSpec {
        LayerSpec {
            source,
            target: PathBuf::from(target),
            is_directory,
            create_target: false,
            priority,
        }
    }

    /// First entry is the mountpoint/original; the rest are directory layers (targeting the root).
    fn fixture(layers: &[(&str, &[&str])]) -> Fx {
        let tmp = TempDir::new().unwrap();
        let mut specs = Vec::new();
        for (i, (dir, files)) in layers.iter().enumerate() {
            let base = tmp.path().join(dir);
            std::fs::create_dir_all(&base).unwrap();
            for f in *files {
                touch(&base, f);
            }
            if i > 0 {
                specs.push(spec(base, "", true, i));
            }
        }
        let tree = build(&tmp, layers[0].0, specs, &Skip::default());
        Fx { _tmp: tmp, tree }
    }

    fn build(tmp: &TempDir, mount: &str, specs: Vec<LayerSpec>, skip: &Skip) -> Tree {
        let root = RootSpec {
            mountpoint: tmp.path().join(mount),
            layers: specs,
        };
        let layers = open_layers(&root).unwrap();
        Tree::build(&layers, skip)
    }

    fn origin_layer(t: &Tree, p: &str) -> LayerId {
        t.node(t.lookup(Path::new(p)).unwrap()).origin.layer
    }

    fn names(t: &Tree, p: &str) -> Vec<String> {
        let id = t.lookup(Path::new(p)).unwrap();
        t.children(id)
            .map(|(_, n)| n.name.to_string_lossy().into_owned())
            .collect()
    }

    #[test]
    fn lookup_folded_matches_lookup() {
        let fx = fixture(&[("orig", &["Skyrim.esm", "textures/rock.dds"])]);
        let t = &fx.tree;
        assert_eq!(t.lookup_folded(&fold_path(Path::new(""))), Some(t.root()));
        let k = fold_path(Path::new("Textures/ROCK.dds"));
        assert_eq!(
            t.lookup_folded(&k),
            t.lookup(Path::new("textures/rock.dds"))
        );
        assert!(t.lookup_folded(&k).is_some());
        assert_eq!(t.lookup_folded(&fold_path(Path::new("nope/x"))), None);
    }

    #[test]
    fn tree_priority_last_wins() {
        let fx = fixture(&[
            ("orig", &["textures/rock.dds", "Skyrim.esm"]),
            ("A", &["textures/rock.dds", "meshes/tree.nif"]),
            ("B", &["textures/rock.dds"]),
        ]);
        let t = &fx.tree;
        assert_eq!(origin_layer(t, "textures/rock.dds"), 2);
        assert_eq!(origin_layer(t, "meshes/tree.nif"), 1);
        assert_eq!(origin_layer(t, "Skyrim.esm"), 0);
        assert_eq!(names(t, ""), ["meshes", "Skyrim.esm", "textures"]);
        assert_eq!(
            t.path_of(t.lookup(Path::new("meshes/tree.nif")).unwrap()),
            Path::new("meshes/tree.nif")
        );
        assert_eq!(t.len(), 6);
    }

    #[test]
    fn tree_case_insensitive_merge_and_display_spelling() {
        let fx = fixture(&[
            ("orig", &[]),
            ("A", &["Textures/a.dds"]),
            ("B", &["textures/B.DDS"]),
        ]);
        let t = &fx.tree;
        assert_eq!(names(t, ""), ["textures"]);
        let id = t.lookup(Path::new("TEXTURES/b.dds")).unwrap();
        assert_eq!(t.node(id).name, "B.DDS");
        let dir = t.lookup(Path::new("Textures")).unwrap();
        let layers: Vec<_> = t.node(dir).dir_origins.iter().map(|o| o.layer).collect();
        assert_eq!(layers, [1, 2]);
        assert_eq!(t.node(dir).origin.layer, 2);
        assert_eq!(t.node(dir).origin.rel, Path::new("textures"));
        assert_eq!(names(t, "textures"), ["a.dds", "B.DDS"]);
    }

    #[test]
    fn tree_dir_vs_file_higher_wins() {
        let fx = fixture(&[("orig", &[]), ("A", &["foo/x"]), ("B", &["foo"])]);
        let t = &fx.tree;
        let foo = t.lookup(Path::new("foo")).unwrap();
        assert_eq!(t.node(foo).kind, Kind::File);
        assert_eq!(t.node(foo).origin.layer, 2);
        assert!(t.lookup(Path::new("foo/x")).is_none());
        assert_eq!(t.len(), 2);

        // the other way around: a directory replaces a lower file
        let fx = fixture(&[("orig", &[]), ("A", &["foo"]), ("B", &["foo/x"])]);
        let t = &fx.tree;
        assert_eq!(t.node(t.lookup(Path::new("foo")).unwrap()).kind, Kind::Dir);
        assert!(t.lookup(Path::new("foo/x")).is_some());
    }

    #[test]
    fn tree_file_mapping_overrides_one_path() {
        let tmp = TempDir::new().unwrap();
        touch(tmp.path(), "orig/Skyrim.ini");
        touch(tmp.path(), "extra/custom.ini");
        let specs = vec![spec(
            tmp.path().join("extra/custom.ini"),
            "Skyrim.ini",
            false,
            0,
        )];
        let t = build(&tmp, "orig", specs, &Skip::default());
        let id = t.lookup(Path::new("skyrim.ini")).unwrap();
        let n = t.node(id);
        assert_eq!(n.kind, Kind::File);
        assert_eq!(n.origin.layer, 1);
        assert_eq!(n.origin.rel, Path::new("custom.ini"));
        assert_eq!(n.name, "Skyrim.ini");
        assert_eq!(t.len(), 2);
    }

    #[test]
    fn tree_target_subdirectory() {
        let tmp = TempDir::new().unwrap();
        touch(tmp.path(), "orig/textures/o.dds");
        touch(tmp.path(), "mod/a.dds");
        let specs = vec![spec(tmp.path().join("mod"), "Textures", true, 0)];
        let t = build(&tmp, "orig", specs, &Skip::default());
        assert_eq!(names(&t, ""), ["Textures"]);
        assert_eq!(names(&t, "textures"), ["a.dds", "o.dds"]);
        assert_eq!(origin_layer(&t, "textures/a.dds"), 1);
        let dir = t.node(t.lookup(Path::new("textures")).unwrap());
        assert_eq!(dir.dir_origins.len(), 2);

        // target in a directory that only exists through the mapping
        let specs = vec![spec(tmp.path().join("mod"), "new/deep", true, 0)];
        let t = build(&tmp, "orig", specs, &Skip::default());
        assert_eq!(origin_layer(&t, "new/deep/a.dds"), 1);
        assert_eq!(t.node(t.lookup(Path::new("new")).unwrap()).kind, Kind::Dir);
    }

    #[test]
    fn tree_skip_suffixes_and_directories() {
        let tmp = TempDir::new().unwrap();
        touch(tmp.path(), "orig/x");
        touch(tmp.path(), "mod/a.esp.mohidden");
        touch(tmp.path(), "mod/b.esp");
        touch(tmp.path(), "mod/FOMOD/info.xml");
        touch(tmp.path(), "mod/sub/c.MOHIDDEN");
        let specs = vec![spec(tmp.path().join("mod"), "", true, 0)];
        let skip = Skip::new(&[".mohidden".to_string()], &["fomod".to_string()]);
        let t = build(&tmp, "orig", specs, &skip);
        assert_eq!(names(&t, ""), ["b.esp", "sub", "x"]);
        assert!(names(&t, "sub").is_empty());
    }

    #[test]
    fn tree_every_origin_is_statable() {
        let tmp = TempDir::new().unwrap();
        touch(tmp.path(), "orig/textures/o.dds");
        touch(tmp.path(), "mod/a.dds");
        touch(tmp.path(), "mod/sub/b.dds");
        touch(tmp.path(), "extra/custom.ini");
        let specs = vec![
            spec(tmp.path().join("mod"), "new/deep", true, 0),
            spec(tmp.path().join("mod"), "Textures", true, 1),
            spec(tmp.path().join("mod"), "", true, 2),
            spec(
                tmp.path().join("extra/custom.ini"),
                "cfg/Skyrim.ini",
                false,
                3,
            ),
        ];
        let root = RootSpec {
            mountpoint: tmp.path().join("orig"),
            layers: specs,
        };
        let layers = open_layers(&root).unwrap();
        let t = Tree::build(&layers, &Skip::default());
        let mut todo = vec![t.root()];
        let mut seen = 0;
        while let Some(id) = todo.pop() {
            seen += 1;
            let n = t.node(id);
            let st = rustix::fs::statat(
                &layers.list[n.origin.layer].dir,
                &n.origin.rel,
                rustix::fs::AtFlags::empty(),
            )
            .unwrap_or_else(|e| panic!("{:?}: {e}", t.path_of(id)));
            let is_dir =
                rustix::fs::FileType::from_raw_mode(st.st_mode) == rustix::fs::FileType::Directory;
            assert_eq!(is_dir, n.kind == Kind::Dir, "{:?}", t.path_of(id));
            for o in &n.dir_origins {
                rustix::fs::statat(
                    &layers.list[o.layer].dir,
                    &o.rel,
                    rustix::fs::AtFlags::empty(),
                )
                .unwrap();
            }
            todo.extend(t.children(id).map(|(c, _)| c));
        }
        assert_eq!(seen, t.len());
        let new = t.node(t.lookup(Path::new("new")).unwrap());
        assert!(new.dir_origins.is_empty());
        assert_eq!(new.origin.layer, 1);
    }

    #[test]
    fn tree_skip_does_not_apply_to_original() {
        let tmp = TempDir::new().unwrap();
        touch(tmp.path(), "orig/a.esp.mohidden");
        touch(tmp.path(), "mod/b.esp.mohidden");
        let specs = vec![spec(tmp.path().join("mod"), "", true, 0)];
        let skip = Skip::new(&[".mohidden".to_string(), String::new()], &[String::new()]);
        assert_eq!(skip.suffixes.len(), 1);
        assert!(skip.directories.is_empty());
        let t = build(&tmp, "orig", specs, &skip);
        assert_eq!(names(&t, ""), ["a.esp.mohidden"]);
    }

    #[test]
    fn tree_intermediate_dir_takes_winning_spelling_and_lookup_dotdot() {
        let tmp = TempDir::new().unwrap();
        touch(tmp.path(), "orig/Textures/o.dds");
        touch(tmp.path(), "mod/a.dds");
        let specs = vec![spec(tmp.path().join("mod"), "TEXTURES/sub", true, 0)];
        let t = build(&tmp, "orig", specs, &Skip::default());
        assert_eq!(names(&t, ""), ["TEXTURES"]);
        assert!(t.lookup(Path::new("textures/../textures")).is_none());
        assert!(t.lookup(Path::new("./textures/./sub")).is_some());
    }

    #[test]
    fn tree_missing_source_is_skipped() {
        let tmp = TempDir::new().unwrap();
        touch(tmp.path(), "orig/x");
        touch(tmp.path(), "real/y");
        let specs = vec![
            spec(tmp.path().join("nope"), "", true, 0),
            spec(tmp.path().join("real"), "", true, 1),
        ];
        let root = RootSpec {
            mountpoint: tmp.path().join("orig"),
            layers: specs,
        };
        let layers = open_layers(&root).unwrap();
        assert_eq!(layers.list.len(), 2);
        let t = Tree::build(&layers, &Skip::default());
        assert_eq!(origin_layer(&t, "y"), 1);
        assert_eq!(names(&t, ""), ["x", "y"]);
    }

    #[test]
    fn tree_creates_missing_mountpoint() {
        let tmp = TempDir::new().unwrap();
        let root = RootSpec {
            mountpoint: tmp.path().join("made/here"),
            layers: vec![],
        };
        let layers = open_layers(&root).unwrap();
        assert!(layers.created_mountpoint);
        assert!(tmp.path().join("made/here").is_dir());
        let layers = open_layers(&root).unwrap();
        assert!(!layers.created_mountpoint);
        assert!(layers.list[0].original);
    }

    #[test]
    fn tree_non_ascii_and_non_utf8_names() {
        let tmp = TempDir::new().unwrap();
        std::fs::create_dir_all(tmp.path().join("orig")).unwrap();
        touch(tmp.path(), "mod/Ünïcödé Mod/日本語.esp");
        let bad = OsStr::from_bytes(b"bad\xffname");
        std::fs::write(tmp.path().join("mod").join(bad), b"x").unwrap();
        let specs = vec![spec(tmp.path().join("mod"), "", true, 0)];
        let t = build(&tmp, "orig", specs, &Skip::default());
        let id = t.lookup(Path::new("ÜNÏCÖDÉ MOD/日本語.ESP")).unwrap();
        assert_eq!(t.node(id).name, "日本語.esp");
        assert_eq!(t.path_of(id), Path::new("Ünïcödé Mod/日本語.esp"));
        let id = t
            .lookup(Path::new(OsStr::from_bytes(b"BAD\xffNAME")))
            .unwrap();
        assert_eq!(t.node(id).name.as_bytes(), b"bad\xffname");
    }

    #[test]
    fn tree_symlinks() {
        let tmp = TempDir::new().unwrap();
        std::fs::create_dir_all(tmp.path().join("orig")).unwrap();
        touch(tmp.path(), "outside/real.txt");
        touch(tmp.path(), "outdir/inner.txt");
        std::fs::create_dir_all(tmp.path().join("mod")).unwrap();
        let m = tmp.path().join("mod");
        std::os::unix::fs::symlink(tmp.path().join("outside/real.txt"), m.join("link.txt"))
            .unwrap();
        std::os::unix::fs::symlink(tmp.path().join("outdir"), m.join("dirlink")).unwrap();
        std::os::unix::fs::symlink(tmp.path().join("gone"), m.join("broken")).unwrap();
        std::os::unix::fs::symlink(&m, m.join("loop")).unwrap();
        let specs = vec![spec(m, "", true, 0)];
        let t = build(&tmp, "orig", specs, &Skip::default());
        let l = t.lookup(Path::new("link.txt")).unwrap();
        assert_eq!(t.node(l).kind, Kind::File);
        assert_eq!(
            t.node(t.lookup(Path::new("dirlink")).unwrap()).kind,
            Kind::Dir
        );
        assert!(t.lookup(Path::new("dirlink/inner.txt")).is_some());
        assert!(t.lookup(Path::new("broken")).is_none());
        // loop: the link to the layer root is already on the recursion stack
        assert!(t.lookup(Path::new("loop")).is_none());
    }

    /// The tree build as it was before the layers were scanned in parallel (one layer after
    /// the other, `put` straight into the nodes' maps, a stat per entry). The equivalence
    /// tests compare the current build against it.
    mod reference {
        use super::super::*;

        pub fn build_with(layers: &Layers, skip: &Skip, whiteouts: &Whiteouts) -> Tree {
            let root = Node {
                name: OsString::new(),
                kind: Kind::Dir,
                parent: 0,
                origin: Origin {
                    layer: 0,
                    rel: PathBuf::from("."),
                },
                dir_origins: Vec::new(),
                children: BTreeMap::new(),
            };
            let mut tree = Tree {
                nodes: vec![root],
                whiteouts: whiteouts.clone(),
            };
            for layer in &layers.list {
                add_layer(&mut tree, layer, skip);
            }
            for key in &whiteouts.paths {
                if let Some(id) = tree.lookup_key(key) {
                    tree.remove(id);
                }
            }
            tree
        }

        fn add_layer(t: &mut Tree, layer: &Layer, skip: &Skip) {
            let mut parent = 0;
            let mut comps = target_components(layer);
            if let Some(file) = &layer.file {
                let Some(name) = comps.pop() else { return };
                for c in comps {
                    parent = ensure_dir(t, parent, c, layer.id);
                }
                if origin_hidden(t, layer, &t.path_of(parent).join(name)) {
                    return;
                }
                if let Some(kind) = entry_kind(&layer.dir, file, &layer.source) {
                    let origin = Origin {
                        layer: layer.id,
                        rel: PathBuf::from(file),
                    };
                    put(t, parent, name, kind, origin);
                }
                return;
            }
            for c in comps {
                parent = ensure_dir(t, parent, c, layer.id);
            }
            let origin = Origin {
                layer: layer.id,
                rel: PathBuf::from("."),
            };
            if parent != 0 {
                t.nodes[parent].origin = origin.clone();
                t.nodes[parent].dir_origins.push(origin);
            } else if layer.original {
                t.nodes[0].dir_origins.push(origin);
            } else {
                t.nodes[0].origin = origin.clone();
                t.nodes[0].dir_origins.push(origin);
            }
            let mut stack = Vec::new();
            if let Ok(st) = rustix::fs::fstat(&layer.dir) {
                stack.push((st.st_dev, st.st_ino));
            }
            let skip = if layer.original {
                &Skip::default()
            } else {
                skip
            };
            scan(
                t,
                layer,
                &layer.dir,
                Path::new(""),
                parent,
                skip,
                &mut stack,
            );
        }

        fn ensure_dir(t: &mut Tree, parent: NodeId, name: &OsStr, layer: LayerId) -> NodeId {
            let key = fold(name);
            if let Some(&id) = t.nodes[parent].children.get(&key)
                && t.nodes[id].kind == Kind::Dir
            {
                t.nodes[id].name = name.to_os_string();
                return id;
            }
            let origin = Origin {
                layer,
                rel: PathBuf::from("."),
            };
            let id = put(t, parent, name, Kind::Dir, origin);
            t.nodes[id].dir_origins.clear();
            id
        }

        fn put(t: &mut Tree, parent: NodeId, name: &OsStr, kind: Kind, origin: Origin) -> NodeId {
            let key = fold(name);
            if let Some(&id) = t.nodes[parent].children.get(&key) {
                let node = &mut t.nodes[id];
                node.name = name.to_os_string();
                if kind == Kind::Dir && node.kind == Kind::Dir {
                    node.origin = origin.clone();
                    node.dir_origins.push(origin);
                } else {
                    node.kind = kind;
                    node.origin = origin.clone();
                    node.children.clear();
                    node.dir_origins = if kind == Kind::Dir {
                        vec![origin]
                    } else {
                        Vec::new()
                    };
                }
                return id;
            }
            let id = t.nodes.len();
            t.nodes.push(Node {
                name: name.to_os_string(),
                kind,
                parent,
                dir_origins: if kind == Kind::Dir {
                    vec![origin.clone()]
                } else {
                    Vec::new()
                },
                origin,
                children: BTreeMap::new(),
            });
            t.nodes[parent].children.insert(key, id);
            id
        }

        fn origin_hidden(t: &Tree, layer: &Layer, vpath: &Path) -> bool {
            !t.whiteouts.origins.is_empty()
                && t.whiteouts
                    .origins
                    .contains(&(layer.source.clone(), fold_path(vpath)))
        }

        fn scan(
            t: &mut Tree,
            layer: &Layer,
            dirfd: &OwnedFd,
            rel: &Path,
            node: NodeId,
            skip: &Skip,
            stack: &mut Vec<(u64, u64)>,
        ) {
            let mut names: Vec<OsString> = match Dir::read_from(dirfd) {
                Ok(dir) => dir
                    .filter_map(|e| e.ok())
                    .map(|e| OsStr::from_bytes(e.file_name().to_bytes()).to_os_string())
                    .filter(|n| n != "." && n != "..")
                    .collect(),
                Err(_) => return,
            };
            names.sort();
            let vdir = if t.whiteouts.origins.is_empty() {
                PathBuf::new()
            } else {
                t.path_of(node)
            };
            for name in names {
                let child_rel = rel.join(&name);
                if !layer.original && name.as_bytes().starts_with(TMP_PREFIX.as_bytes()) {
                    continue;
                }
                if origin_hidden(t, layer, &vdir.join(&name)) {
                    continue;
                }
                let Ok(st) = statat(dirfd, name.as_bytes(), AtFlags::empty()) else {
                    continue;
                };
                let origin = Origin {
                    layer: layer.id,
                    rel: child_rel.clone(),
                };
                if FileType::from_raw_mode(st.st_mode) == FileType::Directory {
                    if skip.skips_dir(&name) {
                        continue;
                    }
                    let id_key = dev_ino(&st);
                    if stack.contains(&id_key) {
                        continue;
                    }
                    let Ok(sub) = openat(
                        dirfd,
                        name.as_bytes(),
                        OFlags::RDONLY | OFlags::DIRECTORY | OFlags::CLOEXEC,
                        Mode::empty(),
                    ) else {
                        continue;
                    };
                    let id = put(t, node, &name, Kind::Dir, origin);
                    stack.push(id_key);
                    scan(t, layer, &sub, &child_rel, id, skip, stack);
                    stack.pop();
                } else {
                    if skip.skips_file(&name) {
                        continue;
                    }
                    put(t, node, &name, Kind::File, origin);
                }
            }
        }
    }

    /// Asserts that two trees have the same reachable content (node ids may differ).
    fn assert_same(a: &Tree, b: &Tree) {
        assert_eq!(a.whiteouts, b.whiteouts);
        assert_eq!(a.len(), b.len());
        let mut todo = vec![(a.root(), b.root())];
        while let Some((x, y)) = todo.pop() {
            let (nx, ny) = (a.node(x), b.node(y));
            let at = a.path_of(x);
            assert_eq!(nx.name, ny.name, "{at:?}");
            assert_eq!(nx.kind, ny.kind, "{at:?}");
            assert_eq!(nx.origin, ny.origin, "{at:?}");
            assert_eq!(nx.dir_origins, ny.dir_origins, "{at:?}");
            let kx: Vec<&Key> = nx.children.keys().collect();
            let ky: Vec<&Key> = ny.children.keys().collect();
            assert_eq!(kx, ky, "{at:?}");
            for (k, &cx) in &nx.children {
                todo.push((cx, ny.children[k]));
            }
        }
    }

    /// The current build, sequential and parallel, equals the reference build.
    fn assert_equivalent(layers: &Layers, skip: &Skip, whiteouts: &Whiteouts) {
        let reference = reference::build_with(layers, skip, whiteouts);
        for workers in [1, 2, 8] {
            let t = Tree::build_with_workers(layers, skip, whiteouts, workers);
            assert_same(&reference, &t);
        }
        assert_same(&reference, &Tree::build_with(layers, skip, whiteouts));
    }

    fn whiteouts(paths: &[&str], origins: &[(PathBuf, &str)]) -> Whiteouts {
        Whiteouts {
            paths: paths.iter().map(|p| fold_path(Path::new(p))).collect(),
            origins: origins
                .iter()
                .map(|(s, p)| (s.clone(), fold_path(Path::new(p))))
                .collect(),
        }
    }

    #[test]
    fn parallel_build_equals_reference_on_fixtures() {
        let tmp = TempDir::new().unwrap();
        let t = tmp.path();
        for f in [
            "orig/Skyrim.esm",
            "orig/Textures/o.dds",
            "orig/.mo2-vfs-tmp-kept",
            "orig/a.esp.mohidden",
            "orig/meshes",
            "A/textures/rock.dds",
            "A/Textures/Sub/x.dds",
            "A/meshes/tree.nif",
            "A/foo/x",
            "A/a.esp.MOHIDDEN",
            "A/FOMOD/info.xml",
            "A/.mo2-vfs-tmp-hidden",
            "A/hidden/inner.txt",
            "B/TEXTURES/ROCK.DDS",
            "B/foo",
            "B/bar/y",
            "B/Ünïcödé Mod/日本語.esp",
            "C/textures/rock.dds",
            "C/bar",
            "C/hidden/again.txt",
            "D/a.dds",
            "D/sub/b.dds",
            "extra/custom.ini",
            "extra/dirmap/inside.txt",
            "A/locked/f.txt",
            "A/locked/sub/g.txt",
        ] {
            touch(t, f);
        }
        // readable but not searchable: every stat inside fails (unless running as root)
        let locked = t.join("A/locked");
        let mode = |m| std::os::unix::fs::PermissionsExt::from_mode(m);
        std::fs::set_permissions(&locked, mode(0o644)).unwrap();
        std::fs::write(t.join("A").join(OsStr::from_bytes(b"bad\xffname")), b"x").unwrap();
        std::fs::write(t.join("B").join(OsStr::from_bytes(b"BAD\xffNAME")), b"x").unwrap();
        // on a case-sensitive disk one layer may hold two spellings of one name
        touch(t, "D/Dup.txt");
        touch(t, "D/dup.txt");
        let m = t.join("C");
        std::os::unix::fs::symlink(t.join("extra/custom.ini"), m.join("link.ini")).unwrap();
        std::os::unix::fs::symlink(t.join("A/meshes"), m.join("dirlink")).unwrap();
        std::os::unix::fs::symlink(t.join("gone"), m.join("broken")).unwrap();
        std::os::unix::fs::symlink(&m, m.join("loop")).unwrap();
        let specs = vec![
            spec(t.join("A"), "", true, 0),
            spec(t.join("B"), "", true, 1),
            spec(t.join("extra/custom.ini"), "Skyrim.ini", false, 2),
            spec(t.join("C"), "", true, 3),
            spec(t.join("D"), "new/deep", true, 4),
            spec(t.join("D"), "TEXTURES/sub", true, 5),
            spec(t.join("D"), "foo/x", true, 6),
            spec(t.join("extra/custom.ini"), "cfg/a/Skyrim.ini", false, 7),
            spec(t.join("extra/dirmap"), "dm", false, 8),
            spec(t.join("extra/missing.ini"), "missing.ini", false, 9),
            spec(t.join("nope"), "", true, 10),
        ];
        let root = RootSpec {
            mountpoint: t.join("orig"),
            layers: specs,
        };
        let layers = open_layers(&root).unwrap();
        let skip = Skip::new(&[".mohidden".to_string()], &["fomod".to_string()]);
        let none = Whiteouts::default();
        assert_equivalent(&layers, &Skip::default(), &none);
        assert_equivalent(&layers, &skip, &none);
        let w = whiteouts(
            &["textures/rock.dds", "bar", "new/deep/sub", "nothing/here"],
            &[
                (t.join("C"), "textures/rock.dds"),
                (t.join("C"), "hidden"),
                (t.join("B"), "foo"),
                (t.join("orig"), "skyrim.esm"),
                (t.join("D"), "new/deep/a.dds"),
                (t.join("extra/custom.ini"), "skyrim.ini"),
                (t.join("unrelated"), "textures"),
            ],
        );
        assert_equivalent(&layers, &skip, &w);
        // an origin whiteout alone
        assert_equivalent(&layers, &skip, &whiteouts(&[], &[(t.join("A"), "foo/x")]));
        std::fs::set_permissions(&locked, mode(0o755)).unwrap();
    }

    #[test]
    fn parallel_build_equals_reference_on_many_layers() {
        // Pseudo-random layers over a small vocabulary: many conflicts, case variants,
        // directory/file clashes, skipped names and whiteouts.
        let tmp = TempDir::new().unwrap();
        let mut seed: u64 = 7;
        let mut next = move || {
            seed = seed
                .wrapping_mul(6364136223846793005)
                .wrapping_add(1442695040888963407);
            (seed >> 33) as usize
        };
        let words = [
            "Tex",
            "tex",
            "MESH",
            "mesh",
            "a",
            "B",
            "b.dds",
            "B.DDS",
            "x.esp.mohidden",
            "fomod",
        ];
        let mut specs = Vec::new();
        for l in 0..40 {
            let base = tmp.path().join(format!("L{l:02}"));
            std::fs::create_dir_all(&base).unwrap();
            for _ in 0..25 {
                let depth = 1 + next() % 3;
                let parts: Vec<&str> = (0..depth).map(|_| words[next() % words.len()]).collect();
                let p = base.join(parts.join("/"));
                // a path may already exist as a directory or have a file as an ancestor
                if std::fs::create_dir_all(p.parent().unwrap()).is_ok() && !p.is_dir() {
                    let _ = std::fs::write(&p, b"x");
                }
            }
            let target = match next() % 4 {
                0 => "tex",
                1 => "Mesh/a",
                _ => "",
            };
            specs.push(spec(base, target, true, l));
        }
        touch(tmp.path(), "orig/TEX/b.dds");
        let root = RootSpec {
            mountpoint: tmp.path().join("orig"),
            layers: specs,
        };
        let layers = open_layers(&root).unwrap();
        let skip = Skip::new(&[".mohidden".to_string()], &["FOMOD".to_string()]);
        assert_equivalent(&layers, &Skip::default(), &Whiteouts::default());
        assert_equivalent(&layers, &skip, &Whiteouts::default());
        let w = whiteouts(
            &["tex/a", "mesh"],
            &[
                (tmp.path().join("L03"), "tex"),
                (tmp.path().join("L17"), "b.dds"),
                (tmp.path().join("L39"), "mesh/a/tex"),
            ],
        );
        assert_equivalent(&layers, &skip, &w);
    }

    #[test]
    fn fold_into_matches_char_lowercasing() {
        for s in [
            "Textures",
            "ÜNÏCÖDÉ",
            "ΟΔΥΣΣΕΥΣ",
            "İstanbul",
            "ẞ",
            "日本語.ESP",
            "ﬁle",
            "",
        ] {
            let expected: String = s.chars().flat_map(char::to_lowercase).collect();
            assert_eq!(fold(OsStr::new(s)).0, expected.into_bytes(), "{s}");
        }
        assert_eq!(fold(OsStr::from_bytes(b"AB\xffC")).0, b"ab\xffc".to_vec());
    }
}
