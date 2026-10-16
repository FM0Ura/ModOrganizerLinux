//! Merged virtual tree built from the layers.

use crate::layer::{Layer, LayerId, Layers, Skip};
use crate::name::{Key, fold, fold_path};
use rustix::fs::{AtFlags, Dir, FileType, Mode, OFlags, openat, statat};
use std::collections::{BTreeMap, BTreeSet};
use std::ffi::{OsStr, OsString};
use std::os::fd::OwnedFd;
use std::os::unix::ffi::OsStrExt;
use std::path::{Component, Path, PathBuf};

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

pub struct Tree {
    nodes: Vec<Node>,
    masks: BTreeSet<Key>,
}

impl Tree {
    /// Builds the merged tree: layers in increasing priority, the last one wins.
    pub fn build(layers: &Layers, skip: &Skip) -> Tree {
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
            masks: BTreeSet::new(),
        };
        for layer in &layers.list {
            tree.add_layer(layer, skip);
        }
        tree
    }

    fn add_layer(&mut self, layer: &Layer, skip: &Skip) {
        let mut parent = 0;
        let mut comps: Vec<&OsStr> = layer
            .target
            .components()
            .filter_map(|c| match c {
                Component::Normal(n) => Some(n),
                _ => None,
            })
            .collect();
        if let Some(file) = &layer.file {
            // A file mapping places one node; its name is the last target component.
            let Some(name) = comps.pop() else { return };
            for c in comps {
                parent = self.ensure_dir(parent, c, layer.id);
            }
            match entry_kind(&layer.dir, file, &layer.source) {
                Some(kind) => {
                    let origin = Origin {
                        layer: layer.id,
                        rel: PathBuf::from(file),
                    };
                    self.put(parent, name, kind, origin);
                }
                None => tracing::warn!(source = %layer.source.display(), "mapped file unavailable"),
            }
            return;
        }
        for c in comps {
            parent = self.ensure_dir(parent, c, layer.id);
        }
        // The layer root is the target directory.
        let origin = Origin {
            layer: layer.id,
            rel: PathBuf::from("."),
        };
        if parent != 0 {
            self.nodes[parent].origin = origin.clone();
            self.nodes[parent].dir_origins.push(origin);
        } else if layer.original {
            self.nodes[0].dir_origins.push(origin);
        } else {
            self.nodes[0].origin = origin.clone();
            self.nodes[0].dir_origins.push(origin);
        }
        let mut stack = Vec::new();
        if let Ok(st) = rustix::fs::fstat(&layer.dir) {
            stack.push((st.st_dev, st.st_ino));
        }
        // The original folder is shown as-is: skip rules only apply to mapping layers.
        let skip = if layer.original {
            &Skip::default()
        } else {
            skip
        };
        self.scan(layer, &layer.dir, Path::new(""), parent, skip, &mut stack);
    }

    /// Returns the directory child `name` of `parent`, creating a synthetic one (no real
    /// directory behind it in this layer) when absent or when it is a file.
    fn ensure_dir(&mut self, parent: NodeId, name: &OsStr, layer: LayerId) -> NodeId {
        let key = fold(name);
        if let Some(&id) = self.nodes[parent].children.get(&key)
            && self.nodes[id].kind == Kind::Dir
        {
            self.nodes[id].name = name.to_os_string();
            return id;
        }
        let origin = Origin {
            layer,
            rel: PathBuf::from("."),
        };
        let id = self.put(parent, name, Kind::Dir, origin);
        self.nodes[id].dir_origins.clear();
        id
    }

    /// Inserts or overwrites the child `name` of `parent`. A directory over a directory
    /// merges (keeping the children); any other combination replaces the node.
    fn put(&mut self, parent: NodeId, name: &OsStr, kind: Kind, origin: Origin) -> NodeId {
        let key = fold(name);
        if let Some(&id) = self.nodes[parent].children.get(&key) {
            let node = &mut self.nodes[id];
            node.name = name.to_os_string();
            if kind == Kind::Dir && node.kind == Kind::Dir {
                node.origin = origin.clone();
                node.dir_origins.push(origin);
            } else {
                node.kind = kind;
                node.origin = origin.clone();
                node.children.clear(); // the old subtree becomes unreachable
                node.dir_origins = if kind == Kind::Dir {
                    vec![origin]
                } else {
                    Vec::new()
                };
            }
            return id;
        }
        let id = self.nodes.len();
        self.nodes.push(Node {
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
        self.nodes[parent].children.insert(key, id);
        id
    }

    fn scan(
        &mut self,
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
            Err(e) => {
                tracing::warn!(layer = %layer.source.display(), rel = %rel.display(), error = %e, "cannot list directory");
                return;
            }
        };
        names.sort();
        for name in names {
            let child_rel = rel.join(&name);
            let st = match statat(dirfd, name.as_bytes(), AtFlags::empty()) {
                Ok(st) => st,
                Err(e) => {
                    tracing::warn!(path = %layer.source.join(&child_rel).display(), error = %e, "skipping unreadable entry or broken link");
                    continue;
                }
            };
            let origin = Origin {
                layer: layer.id,
                rel: child_rel.clone(),
            };
            if FileType::from_raw_mode(st.st_mode) == FileType::Directory {
                if skip.skips_dir(&name) {
                    continue;
                }
                let id_key = (st.st_dev as u64, st.st_ino as u64);
                if stack.contains(&id_key) {
                    tracing::warn!(path = %layer.source.join(&child_rel).display(), "symlink loop, skipping directory");
                    continue;
                }
                let sub = match openat(
                    dirfd,
                    name.as_bytes(),
                    OFlags::RDONLY | OFlags::DIRECTORY | OFlags::CLOEXEC,
                    Mode::empty(),
                ) {
                    Ok(fd) => fd,
                    Err(e) => {
                        tracing::warn!(path = %layer.source.join(&child_rel).display(), error = %e, "cannot open directory");
                        continue;
                    }
                };
                let id = self.put(node, &name, Kind::Dir, origin);
                stack.push(id_key);
                self.scan(layer, &sub, &child_rel, id, skip, stack);
                stack.pop();
            } else {
                if skip.skips_file(&name) {
                    continue;
                }
                self.put(node, &name, Kind::File, origin);
            }
        }
    }

    /// Paths hidden by the VFS itself (deletions and renames), folded.
    pub fn masks(&self) -> &BTreeSet<Key> {
        &self.masks
    }

    /// Hides `vpath` on rebuilds (see `apply_masks`). Does not touch the nodes.
    pub fn mask(&mut self, vpath: &Path) {
        let key = fold_path(vpath);
        if !key.0.is_empty() {
            self.masks.insert(key);
        }
    }

    /// Adopts `masks` and detaches the nodes at those paths.
    pub fn apply_masks(&mut self, masks: &BTreeSet<Key>) {
        for key in masks {
            if key.0.is_empty() {
                continue;
            }
            self.masks.insert(key.clone());
            let mut cur = 0;
            let mut found = true;
            for comp in key.0.split(|&b| b == b'/') {
                match self.nodes[cur].children.get(&Key(comp.to_vec())) {
                    Some(&id) => cur = id,
                    None => {
                        found = false;
                        break;
                    }
                }
            }
            if found {
                self.remove(cur);
            }
        }
    }

    /// Adds `node` as a child of `parent`, replacing a child with the same folded name
    /// (the replaced node keeps its id). Clears the mask on the resulting path.
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
    /// already has that name there is detached. Clears masks on the moved paths.
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
        if self.masks.is_empty() {
            return;
        }
        let mut todo = vec![(id, self.path_of(id))];
        while let Some((n, path)) = todo.pop() {
            self.masks.remove(&fold_path(&path));
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
}
