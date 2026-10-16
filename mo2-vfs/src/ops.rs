//! Operations on the merged tree and the real disk (spec section 4), without FUSE.
//!
//! Every disk access is an `*at` call relative to a layer fd. Original-layer files are
//! never deleted or renamed: deletions only mask, renames copy to the create layer.

use crate::layer::{LayerId, Layers};
use crate::name::fold;
use crate::tree::{Kind, Node, NodeId, Origin, Tree};
use rustix::fs::FileType;
use rustix::fs::{AtFlags, Mode, OFlags, RenameFlags, Timestamps, fchmod, futimens};
use rustix::fs::{fstat, mkdirat, openat, renameat, statat, unlinkat};
use rustix::io::Errno;
use std::collections::BTreeMap;
use std::ffi::{OsStr, OsString};
use std::os::fd::{AsFd, OwnedFd};
use std::os::unix::ffi::OsStrExt;
use std::path::{Component, Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

fn valid_name(name: &OsStr) -> Result<(), Errno> {
    let b = name.as_bytes();
    if b.is_empty() || b == b"." || b == b".." || b.contains(&b'/') {
        return Err(Errno::INVAL);
    }
    Ok(())
}

/// A relative path made of normal components only: safe to hand to a destructive call.
fn safe_rel(rel: &Path) -> Result<(), Errno> {
    let mut any = false;
    for c in rel.components() {
        match c {
            Component::Normal(_) => any = true,
            _ => return Err(Errno::INVAL),
        }
    }
    if any { Ok(()) } else { Err(Errno::INVAL) }
}

fn normal_components(p: &Path) -> Vec<&OsStr> {
    p.components()
        .filter_map(|c| match c {
            Component::Normal(n) => Some(n),
            _ => None,
        })
        .collect()
}

/// True when the layer's target is a strict prefix of `vpath` (compared folded).
fn under_target(layers: &Layers, layer: LayerId, vpath: &Path) -> bool {
    let target = normal_components(&layers.list[layer].target);
    let comps = normal_components(vpath);
    target.len() < comps.len() && target.iter().zip(&comps).all(|(t, c)| fold(t) == fold(c))
}

/// The create layer (highest priority with `create`, a directory mapping) whose target is a
/// prefix of `vpath`.
pub fn create_layer_for(_tree: &Tree, layers: &Layers, vpath: &Path) -> Option<LayerId> {
    layers
        .list
        .iter()
        .rev()
        .find(|l| l.create && !l.original && l.file.is_none() && under_target(layers, l.id, vpath))
        .map(|l| l.id)
}

/// Real directory (relative to the layer fd) that corresponds to the virtual directory
/// `dir` in `layer`; it may not exist yet.
fn real_dir_rel(tree: &Tree, layers: &Layers, layer: LayerId, dir: NodeId) -> Option<PathBuf> {
    let tlen = normal_components(&layers.list[layer].target).len();
    let total = normal_components(&tree.path_of(dir)).len();
    let mut names: Vec<OsString> = Vec::new();
    let mut cur = dir;
    let mut depth = total;
    loop {
        let node = tree.node(cur);
        let base = if let Some(o) = node.dir_origins.iter().find(|o| o.layer == layer) {
            Some(o.rel.clone())
        } else if depth == tlen {
            Some(PathBuf::from("."))
        } else {
            None
        };
        if let Some(base) = base {
            let mut out = if base == Path::new(".") {
                PathBuf::new()
            } else {
                base
            };
            for n in names.iter().rev() {
                out.push(n);
            }
            return Some(out);
        }
        if cur == 0 || depth <= tlen {
            return None;
        }
        names.push(node.name.clone());
        cur = node.parent;
        depth -= 1;
    }
}

/// `mkdir -p` of `rel_dir` inside the layer (mode 0o755 before umask); existing entries are
/// accepted.
pub fn ensure_parents(layers: &Layers, layer: LayerId, rel_dir: &Path) -> Result<(), Errno> {
    let dir = &layers.list.get(layer).ok_or(Errno::INVAL)?.dir;
    let mut acc = PathBuf::new();
    for c in rel_dir.components() {
        match c {
            Component::Normal(n) => acc.push(n),
            Component::CurDir => continue,
            _ => return Err(Errno::INVAL),
        }
        match mkdirat(dir, &acc, Mode::from_raw_mode(0o755)) {
            Ok(()) | Err(Errno::EXIST) => {}
            Err(e) => return Err(e),
        }
    }
    Ok(())
}

/// Opens the real file behind `node` with `flags`.
pub fn open(layers: &Layers, node: &Node, flags: OFlags) -> Result<OwnedFd, Errno> {
    let layer = layers.list.get(node.origin.layer).ok_or(Errno::IO)?;
    openat(
        &layer.dir,
        &node.origin.rel,
        flags | OFlags::CLOEXEC,
        Mode::empty(),
    )
}

/// Validation shared by create and mkdir: returns the create layer and the real directory.
fn prepare_create(
    tree: &Tree,
    layers: &Layers,
    parent: NodeId,
    name: &OsStr,
) -> Result<(LayerId, PathBuf), Errno> {
    valid_name(name)?;
    if tree.node(parent).kind != Kind::Dir {
        return Err(Errno::NOTDIR);
    }
    if tree.child(parent, name).is_some() {
        return Err(Errno::EXIST);
    }
    let vpath = tree.path_of(parent).join(name);
    let layer = create_layer_for(tree, layers, &vpath).ok_or_else(|| {
        tracing::warn!(path = %vpath.display(), "no create layer covers this path");
        Errno::ROFS
    })?;
    let rel_dir = real_dir_rel(tree, layers, layer, parent).ok_or(Errno::ROFS)?;
    ensure_parents(layers, layer, &rel_dir)?;
    Ok((layer, rel_dir))
}

pub fn create_file(
    tree: &mut Tree,
    layers: &Layers,
    parent: NodeId,
    name: &OsStr,
    mode: Mode,
    flags: OFlags,
) -> Result<(NodeId, OwnedFd), Errno> {
    let (layer, rel_dir) = prepare_create(tree, layers, parent, name)?;
    let rel = rel_dir.join(name);
    let fd = openat(
        &layers.list[layer].dir,
        &rel,
        flags | OFlags::CREATE | OFlags::EXCL | OFlags::CLOEXEC,
        mode,
    )?;
    let node = Node {
        name: name.to_os_string(),
        kind: Kind::File,
        parent,
        origin: Origin { layer, rel },
        dir_origins: Vec::new(),
        children: BTreeMap::new(),
    };
    Ok((tree.insert(parent, node), fd))
}

pub fn mkdir(
    tree: &mut Tree,
    layers: &Layers,
    parent: NodeId,
    name: &OsStr,
    mode: Mode,
) -> Result<NodeId, Errno> {
    let (layer, rel_dir) = prepare_create(tree, layers, parent, name)?;
    let rel = rel_dir.join(name);
    let dir = &layers.list[layer].dir;
    match mkdirat(dir, &rel, mode) {
        Ok(()) => {}
        // A real directory can survive an earlier rmdir (it only held hidden entries).
        Err(Errno::EXIST) => {
            let st = statat(dir, &rel, AtFlags::SYMLINK_NOFOLLOW)?;
            if FileType::from_raw_mode(st.st_mode) != FileType::Directory {
                return Err(Errno::EXIST);
            }
        }
        Err(e) => return Err(e),
    }
    let origin = Origin { layer, rel };
    let node = Node {
        name: name.to_os_string(),
        kind: Kind::Dir,
        parent,
        origin: origin.clone(),
        dir_origins: vec![origin],
        children: BTreeMap::new(),
    };
    Ok(tree.insert(parent, node))
}

pub fn unlink(tree: &mut Tree, layers: &Layers, parent: NodeId, name: &OsStr) -> Result<(), Errno> {
    valid_name(name)?;
    let id = tree.child(parent, name).ok_or(Errno::NOENT)?;
    let node = tree.node(id);
    if node.kind == Kind::Dir {
        return Err(Errno::ISDIR);
    }
    let layer = layers.list.get(node.origin.layer).ok_or(Errno::IO)?;
    if !layer.original {
        safe_rel(&node.origin.rel)?;
        match unlinkat(&layer.dir, &node.origin.rel, AtFlags::empty()) {
            Ok(()) | Err(Errno::NOENT) => {}
            Err(e) => return Err(e),
        }
    }
    let vpath = tree.path_of(id);
    tree.remove(id);
    tree.mask(&vpath);
    Ok(())
}

/// Removes the empty real directories behind the virtual directory `id`, in every layer
/// except the original one. A directory that still holds (hidden) entries is left alone.
fn remove_real_dirs(tree: &Tree, layers: &Layers, id: NodeId) -> Result<(), Errno> {
    for o in &tree.node(id).dir_origins {
        let Some(layer) = layers.list.get(o.layer) else {
            continue;
        };
        if layer.original || layer.file.is_some() || safe_rel(&o.rel).is_err() {
            continue;
        }
        match unlinkat(&layer.dir, &o.rel, AtFlags::REMOVEDIR) {
            Ok(()) | Err(Errno::NOTEMPTY) | Err(Errno::NOENT) => {}
            Err(e) => return Err(e),
        }
    }
    Ok(())
}

pub fn rmdir(tree: &mut Tree, layers: &Layers, parent: NodeId, name: &OsStr) -> Result<(), Errno> {
    valid_name(name)?;
    let id = tree.child(parent, name).ok_or(Errno::NOENT)?;
    if tree.node(id).kind != Kind::Dir {
        return Err(Errno::NOTDIR);
    }
    if !tree.node(id).children.is_empty() {
        return Err(Errno::NOTEMPTY);
    }
    remove_real_dirs(tree, layers, id)?;
    let vpath = tree.path_of(id);
    tree.remove(id);
    tree.mask(&vpath);
    Ok(())
}

static TMP_COUNTER: AtomicU64 = AtomicU64::new(0);

/// Copies the file `src_rel` of `src_layer` to `dst_rel` of `dst_layer` (which must not
/// exist): content, mode and times. Removes the partial copy on failure.
fn copy_file(
    layers: &Layers,
    src_layer: LayerId,
    src_rel: &Path,
    dst_layer: LayerId,
    dst_rel: &Path,
) -> Result<(), Errno> {
    let input = openat(
        &layers.list[src_layer].dir,
        src_rel,
        OFlags::RDONLY | OFlags::CLOEXEC,
        Mode::empty(),
    )?;
    let st = fstat(&input)?;
    let dst_dir = &layers.list[dst_layer].dir;
    let out = openat(
        dst_dir,
        dst_rel,
        OFlags::WRONLY | OFlags::CREATE | OFlags::EXCL | OFlags::CLOEXEC,
        Mode::from_raw_mode(0o600),
    )?;
    let result = (|| {
        copy_data(&input, &out)?;
        fchmod(&out, Mode::from_bits_truncate(st.st_mode & 0o7777))?;
        futimens(
            &out,
            &Timestamps {
                last_access: rustix::fs::Timespec {
                    tv_sec: st.st_atime,
                    tv_nsec: st.st_atime_nsec as _,
                },
                last_modification: rustix::fs::Timespec {
                    tv_sec: st.st_mtime,
                    tv_nsec: st.st_mtime_nsec as _,
                },
            },
        )
    })();
    if result.is_err() {
        let _ = unlinkat(dst_dir, dst_rel, AtFlags::empty());
    }
    result
}

fn copy_data(input: &OwnedFd, out: &OwnedFd) -> Result<(), Errno> {
    let mut use_cfr = true;
    let mut buf: Vec<u8> = Vec::new();
    loop {
        if use_cfr {
            match rustix::fs::copy_file_range(input, None, out, None, 1 << 30) {
                Ok(0) => return Ok(()),
                Ok(_) => continue,
                Err(Errno::INTR) => continue,
                Err(
                    Errno::XDEV
                    | Errno::NOSYS
                    | Errno::INVAL
                    | Errno::OPNOTSUPP
                    | Errno::PERM
                    | Errno::BADF,
                ) => use_cfr = false,
                Err(e) => return Err(e),
            }
        }
        if buf.is_empty() {
            buf.resize(128 * 1024, 0);
        }
        let n = match rustix::io::read(input.as_fd(), &mut buf) {
            Ok(0) => return Ok(()),
            Ok(n) => n,
            Err(Errno::INTR) => continue,
            Err(e) => return Err(e),
        };
        let mut done = 0;
        while done < n {
            match rustix::io::write(out.as_fd(), &buf[done..n]) {
                Ok(w) => done += w,
                Err(Errno::INTR) => {}
                Err(e) => return Err(e),
            }
        }
    }
}

/// True when every origin in the subtree of `id` belongs to `layer` and every directory has
/// a real directory behind it.
fn subtree_only_in(tree: &Tree, id: NodeId, layer: LayerId) -> bool {
    let n = tree.node(id);
    if n.origin.layer != layer {
        return false;
    }
    if n.kind == Kind::Dir
        && (n.dir_origins.is_empty() || n.dir_origins.iter().any(|o| o.layer != layer))
    {
        return false;
    }
    tree.children(id)
        .all(|(c, _)| subtree_only_in(tree, c, layer))
}

enum Plan {
    /// `renameat` inside `layer`.
    Move { layer: LayerId },
    /// Copy an original-layer file into the create layer.
    CopyOriginal { create: LayerId },
}

#[allow(clippy::too_many_arguments)]
pub fn rename(
    tree: &mut Tree,
    layers: &Layers,
    parent: NodeId,
    name: &OsStr,
    new_parent: NodeId,
    new_name: &OsStr,
    flags: RenameFlags,
) -> Result<(), Errno> {
    if !flags.difference(RenameFlags::NOREPLACE).is_empty() {
        return Err(Errno::INVAL);
    }
    valid_name(name)?;
    valid_name(new_name)?;
    let src = tree.child(parent, name).ok_or(Errno::NOENT)?;
    if tree.node(new_parent).kind != Kind::Dir {
        return Err(Errno::NOTDIR);
    }
    let src_kind = tree.node(src).kind;
    if src_kind == Kind::Dir {
        let mut cur = new_parent;
        loop {
            if cur == src {
                return Err(Errno::INVAL);
            }
            if cur == 0 {
                break;
            }
            cur = tree.node(cur).parent;
        }
    }
    let same = tree.child(new_parent, new_name) == Some(src);
    if same && new_parent == parent && tree.node(src).name == new_name {
        return if flags.contains(RenameFlags::NOREPLACE) {
            Err(Errno::EXIST)
        } else {
            Ok(())
        };
    }
    let dest = tree.child(new_parent, new_name).filter(|&d| d != src);
    if let Some(d) = dest {
        if flags.contains(RenameFlags::NOREPLACE) {
            return Err(Errno::EXIST);
        }
        match (src_kind, tree.node(d).kind) {
            (Kind::File, Kind::Dir) => return Err(Errno::ISDIR),
            (Kind::Dir, Kind::File) => return Err(Errno::NOTDIR),
            (Kind::Dir, Kind::Dir) if !tree.node(d).children.is_empty() => {
                return Err(Errno::NOTEMPTY);
            }
            _ => {}
        }
    }

    let new_vpath = tree.path_of(new_parent).join(new_name);
    let old_vpath = tree.path_of(src);
    let src_origin = tree.node(src).origin.clone();
    let src_layer = layers.list.get(src_origin.layer).ok_or(Errno::IO)?;

    // Plan: everything that can fail without side effects comes first.
    let plan = match src_kind {
        Kind::File => {
            if src_layer.file.is_some() {
                return Err(Errno::XDEV);
            }
            if src_layer.original {
                let create = create_layer_for(tree, layers, &new_vpath).ok_or(Errno::ROFS)?;
                Plan::CopyOriginal { create }
            } else {
                if !under_target(layers, src_layer.id, &new_vpath) {
                    return Err(Errno::XDEV);
                }
                Plan::Move {
                    layer: src_layer.id,
                }
            }
        }
        Kind::Dir => {
            let origins = &tree.node(src).dir_origins;
            if origins.len() != 1
                || src_layer.original
                || src_layer.file.is_some()
                || origins[0].layer != src_layer.id
                || origins[0].rel != src_origin.rel
                || safe_rel(&src_origin.rel).is_err()
                || !subtree_only_in(tree, src, src_layer.id)
                || !under_target(layers, src_layer.id, &new_vpath)
            {
                return Err(Errno::XDEV);
            }
            Plan::Move {
                layer: src_layer.id,
            }
        }
    };
    let dest_layer = match plan {
        Plan::Move { layer } => layer,
        Plan::CopyOriginal { create } => create,
    };
    let rel_dir = real_dir_rel(tree, layers, dest_layer, new_parent).ok_or(Errno::XDEV)?;
    let new_rel = rel_dir.join(new_name);
    safe_rel(&new_rel)?;
    if matches!(plan, Plan::Move { .. }) {
        safe_rel(&src_origin.rel)?;
    }

    // Side effects.
    if let Some(d) = dest
        && tree.node(d).kind == Kind::Dir
    {
        remove_real_dirs(tree, layers, d)?;
    }
    ensure_parents(layers, dest_layer, &rel_dir)?;
    let dir = &layers.list[dest_layer].dir;
    match plan {
        Plan::Move { layer } => {
            renameat(dir, &src_origin.rel, dir, &new_rel)?;
            if src_kind == Kind::Dir {
                tree.rebase_origins(src, layer, &src_origin.rel, &new_rel);
            } else {
                tree.node_mut(src).origin.rel = new_rel.clone();
            }
        }
        Plan::CopyOriginal { create } => {
            let tmp = rel_dir.join(format!(
                ".mo2-vfs-tmp-{}-{}",
                std::process::id(),
                TMP_COUNTER.fetch_add(1, Ordering::Relaxed)
            ));
            copy_file(layers, src_origin.layer, &src_origin.rel, create, &tmp)?;
            if let Err(e) = renameat(dir, &tmp, dir, &new_rel) {
                let _ = unlinkat(dir, &tmp, AtFlags::empty());
                return Err(e);
            }
            let node = tree.node_mut(src);
            node.origin = Origin {
                layer: create,
                rel: new_rel.clone(),
            };
        }
    }
    // A replaced file of the same layer that was spelled differently is still on disk.
    if let Some(d) = dest {
        let dn = tree.node(d);
        if dn.kind == Kind::File
            && dn.origin.layer == dest_layer
            && dn.origin.rel != new_rel
            && safe_rel(&dn.origin.rel).is_ok()
        {
            let _ = unlinkat(dir, &dn.origin.rel, AtFlags::empty());
        }
    }
    tree.mask(&old_vpath);
    tree.move_node(src, new_parent, new_name);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::layer::{Skip, open_layers};
    use crate::mapping::{LayerSpec, RootSpec};
    use tempfile::TempDir;

    struct Fx {
        tmp: TempDir,
        tree: Tree,
        layers: Layers,
    }

    impl Fx {
        fn p(&self, rel: &str) -> PathBuf {
            self.tmp.path().join(rel)
        }
        fn id(&self, vpath: &str) -> NodeId {
            self.tree.lookup(Path::new(vpath)).unwrap()
        }
        fn gone(&self, vpath: &str) -> bool {
            self.tree.lookup(Path::new(vpath)).is_none()
        }
        fn create(&mut self, dir: &str, name: &str) -> Result<NodeId, Errno> {
            let parent = self.id(dir);
            create_file(
                &mut self.tree,
                &self.layers,
                parent,
                OsStr::new(name),
                Mode::from_raw_mode(0o644),
                OFlags::WRONLY,
            )
            .map(|(id, _)| id)
        }
        fn unlink(&mut self, dir: &str, name: &str) -> Result<(), Errno> {
            let parent = self.id(dir);
            unlink(&mut self.tree, &self.layers, parent, OsStr::new(name))
        }
        fn rmdir(&mut self, dir: &str, name: &str) -> Result<(), Errno> {
            let parent = self.id(dir);
            rmdir(&mut self.tree, &self.layers, parent, OsStr::new(name))
        }
        fn rename(&mut self, dir: &str, name: &str, ndir: &str, nname: &str) -> Result<(), Errno> {
            self.rename_with(dir, name, ndir, nname, RenameFlags::empty())
        }
        fn rename_with(
            &mut self,
            dir: &str,
            name: &str,
            ndir: &str,
            nname: &str,
            flags: RenameFlags,
        ) -> Result<(), Errno> {
            let (p, np) = (self.id(dir), self.id(ndir));
            rename(
                &mut self.tree,
                &self.layers,
                p,
                OsStr::new(name),
                np,
                OsStr::new(nname),
                flags,
            )
        }
        fn layer_of(&self, vpath: &str) -> LayerId {
            self.tree.node(self.id(vpath)).origin.layer
        }
    }

    fn write(base: &Path, rel: &str, content: &str) {
        let p = base.join(rel);
        std::fs::create_dir_all(p.parent().unwrap()).unwrap();
        std::fs::write(p, content).unwrap();
    }

    fn spec(source: PathBuf, target: &str, is_directory: bool, create: bool) -> LayerSpec {
        LayerSpec {
            source,
            target: PathBuf::from(target),
            is_directory,
            create_target: create,
            priority: 0,
        }
    }

    fn assemble(tmp: TempDir, specs: Vec<LayerSpec>) -> Fx {
        let root = RootSpec {
            mountpoint: tmp.path().join("orig"),
            layers: specs,
        };
        let layers = open_layers(&root).unwrap();
        let tree = Tree::build(&layers, &Skip::default());
        Fx { tmp, tree, layers }
    }

    /// `orig` plus the named directory layers in order; the last is the create layer when
    /// `with_create`. File content is "<layer>:<path>".
    fn fixture(layers: &[(&str, &[&str])], with_create: bool) -> Fx {
        let tmp = TempDir::new().unwrap();
        let mut specs = Vec::new();
        let n = layers.len();
        for (i, (dir, files)) in layers.iter().enumerate() {
            let base = tmp.path().join(dir);
            std::fs::create_dir_all(&base).unwrap();
            for f in *files {
                write(&base, f, &format!("{dir}:{f}"));
            }
            if i > 0 {
                specs.push(spec(base, "", true, with_create && i == n - 1));
            }
        }
        assemble(tmp, specs)
    }

    fn std_fixture() -> Fx {
        fixture(
            &[
                ("orig", &["Skyrim.esm", "textures/rock.dds"]),
                ("A", &["textures/rock.dds", "a.txt", "meshes/tree.nif"]),
                ("B", &["textures/rock.dds", "b.txt"]),
                ("overwrite", &[]),
            ],
            true,
        )
    }

    #[test]
    fn create_goes_to_create_layer_with_parents() {
        let mut fx = std_fixture();
        let (id, _fd) = mkdir_then_create(&mut fx);
        assert!(fx.p("overwrite/new/dir/novo.txt").is_file());
        let n = fx.tree.node(fx.id("NEW/Dir/NOVO.txt"));
        assert_eq!(n.origin.layer, 3);
        assert_eq!(n.origin.rel, Path::new("new/dir/novo.txt"));
        assert_eq!(fx.tree.path_of(id), Path::new("new/dir/novo.txt"));
    }

    /// Creates new/dir/novo.txt; every level goes to the create layer.
    fn mkdir_then_create(fx: &mut Fx) -> (NodeId, OwnedFd) {
        let root = fx.tree.root();
        // `mkdir` for each level creates real parents in the create layer.
        let new = mkdir(
            &mut fx.tree,
            &fx.layers,
            root,
            OsStr::new("new"),
            Mode::from_raw_mode(0o755),
        )
        .unwrap();
        let dir = mkdir(
            &mut fx.tree,
            &fx.layers,
            new,
            OsStr::new("dir"),
            Mode::from_raw_mode(0o755),
        )
        .unwrap();
        create_file(
            &mut fx.tree,
            &fx.layers,
            dir,
            OsStr::new("novo.txt"),
            Mode::from_raw_mode(0o644),
            OFlags::WRONLY,
        )
        .unwrap()
    }

    #[test]
    fn create_in_other_layer_only_dir_makes_real_parents() {
        let mut fx = std_fixture();
        // meshes exists only in layer A: the new file goes to overwrite/meshes/
        fx.create("meshes", "new.nif").unwrap();
        assert!(fx.p("overwrite/meshes/new.nif").is_file());
        assert!(fx.p("A/meshes/tree.nif").is_file());
        assert_eq!(fx.layer_of("meshes/new.nif"), 3);
        assert_eq!(
            fx.tree.node(fx.id("meshes/new.nif")).origin.rel,
            Path::new("meshes/new.nif")
        );
    }

    #[test]
    fn create_existing_is_eexist_and_unlink_rmdir_kinds() {
        let mut fx = std_fixture();
        assert_eq!(fx.create("", "A.TXT").err(), Some(Errno::EXIST));
        assert_eq!(fx.create("", "textures").err(), Some(Errno::EXIST));
        assert_eq!(fx.unlink("", "textures").err(), Some(Errno::ISDIR));
        assert_eq!(fx.rmdir("", "a.txt").err(), Some(Errno::NOTDIR));
        assert_eq!(fx.unlink("", "nope").err(), Some(Errno::NOENT));
        assert_eq!(fx.create("", "a/b").err(), Some(Errno::INVAL));
        assert!(fx.p("A/a.txt").is_file());
        assert!(!fx.p("overwrite/a.txt").exists());
    }

    #[test]
    fn create_without_create_layer_is_erofs() {
        let mut fx = fixture(&[("orig", &[]), ("A", &["a.txt"])], false);
        assert_eq!(fx.create("", "x.txt").err(), Some(Errno::ROFS));
        assert!(!fx.p("A/x.txt").exists());
        assert!(!fx.p("orig/x.txt").exists());
        assert!(fx.gone("x.txt"));
        let root = fx.tree.root();
        let r = mkdir(
            &mut fx.tree,
            &fx.layers,
            root,
            OsStr::new("d"),
            Mode::from_raw_mode(0o755),
        );
        assert_eq!(r.err(), Some(Errno::ROFS));
    }

    #[test]
    fn create_layer_with_target_only_covers_its_target() {
        let tmp = TempDir::new().unwrap();
        std::fs::create_dir_all(tmp.path().join("orig")).unwrap();
        write(tmp.path(), "saves/old.ess", "o");
        let specs = vec![spec(tmp.path().join("saves"), "Saves", true, true)];
        let mut fx = assemble(tmp, specs);
        assert_eq!(
            create_layer_for(&fx.tree, &fx.layers, Path::new("Saves/x.ess")),
            Some(1)
        );
        assert_eq!(
            create_layer_for(&fx.tree, &fx.layers, Path::new("saves")),
            None
        );
        assert_eq!(
            create_layer_for(&fx.tree, &fx.layers, Path::new("other/x")),
            None
        );
        fx.create("saves", "new.ess").unwrap();
        // the real path is relative to the layer, not to the virtual root
        assert!(fx.p("saves/new.ess").is_file());
        assert!(!fx.p("saves/Saves").exists());
        assert_eq!(fx.create("", "top.txt").err(), Some(Errno::ROFS));
    }

    #[test]
    fn unlink_mod_file_deletes_and_masks() {
        let mut fx = std_fixture();
        fx.unlink("textures", "rock.dds").unwrap();
        assert!(!fx.p("B/textures/rock.dds").exists());
        assert!(fx.p("A/textures/rock.dds").exists());
        assert!(fx.p("orig/textures/rock.dds").exists());
        assert!(fx.gone("textures/rock.dds"));
        assert!(
            fx.tree
                .masks()
                .contains(&crate::name::fold_path(Path::new("Textures/rock.dds")))
        );
    }

    #[test]
    fn unlink_original_only_masks() {
        let mut fx = std_fixture();
        fx.unlink("", "skyrim.ESM").unwrap();
        assert_eq!(
            std::fs::read_to_string(fx.p("orig/Skyrim.esm")).unwrap(),
            "orig:Skyrim.esm"
        );
        assert!(fx.gone("Skyrim.esm"));
        assert_eq!(fx.tree.masks().len(), 1);
    }

    #[test]
    fn rmdir_merged_dir_requires_virtual_empty() {
        let mut fx = std_fixture();
        assert_eq!(fx.rmdir("", "textures").err(), Some(Errno::NOTEMPTY));
        assert!(fx.p("A/textures").is_dir());
        assert!(!fx.gone("textures"));
        fx.unlink("textures", "rock.dds").unwrap();
        fx.unlink("Textures", "ROCK.dds").ok(); // already gone from the tree
        assert!(fx.gone("textures/rock.dds"));
        // A's and B's real dirs: A still holds rock.dds (shadowed, masked); B's is empty
        fx.rmdir("", "Textures").unwrap();
        assert!(!fx.p("B/textures").exists(), "empty mod dir removed");
        assert!(
            fx.p("A/textures/rock.dds").exists(),
            "non-empty mod dir untouched"
        );
        assert!(
            fx.p("orig/textures/rock.dds").exists(),
            "original dir untouched"
        );
        assert!(fx.gone("textures"));
        assert!(
            fx.tree
                .masks()
                .contains(&crate::name::fold_path(Path::new("textures")))
        );
    }

    #[test]
    fn rmdir_removes_empty_dirs_of_every_non_original_layer() {
        let mut fx = fixture(
            &[
                ("orig", &["d/x"]),
                ("A", &[]),
                ("B", &[]),
                ("overwrite", &[]),
            ],
            true,
        );
        for l in ["A", "B", "overwrite"] {
            std::fs::create_dir_all(fx.p(&format!("{l}/d"))).unwrap();
        }
        fx.tree = Tree::build(&fx.layers, &Skip::default());
        fx.unlink("d", "x").unwrap();
        fx.rmdir("", "d").unwrap();
        for l in ["A", "B", "overwrite"] {
            assert!(!fx.p(&format!("{l}/d")).exists(), "{l}");
        }
        assert!(fx.p("orig/d/x").exists());
        assert!(fx.gone("d"));
    }

    #[test]
    fn create_after_unlink_clears_mask() {
        let mut fx = std_fixture();
        fx.unlink("textures", "rock.dds").unwrap();
        assert!(!fx.tree.masks().is_empty());
        fx.create("textures", "rock.dds").unwrap();
        assert!(fx.tree.masks().is_empty());
        assert!(fx.p("overwrite/textures/rock.dds").is_file());
        assert_eq!(fx.layer_of("textures/rock.dds"), 3);
        // and a rebuild with the (now empty) masks shows it
        let masks = fx.tree.masks().clone();
        let mut t = Tree::build(&fx.layers, &Skip::default());
        t.apply_masks(&masks);
        assert!(t.lookup(Path::new("textures/rock.dds")).is_some());
    }

    #[test]
    fn rename_within_create_layer() {
        let mut fx = std_fixture();
        fx.create("", "save.tmp").unwrap();
        fx.rename("", "save.tmp", "", "save.ess").unwrap();
        assert!(fx.p("overwrite/save.ess").is_file());
        assert!(!fx.p("overwrite/save.tmp").exists());
        assert!(fx.gone("save.tmp"));
        let n = fx.tree.node(fx.id("SAVE.ESS"));
        assert_eq!(n.name, "save.ess");
        assert_eq!(n.origin.rel, Path::new("save.ess"));
        assert_eq!(fx.tree.path_of(fx.id("save.ess")), Path::new("save.ess"));
    }

    #[test]
    fn rename_mod_file_moves_in_its_layer_and_masks_destination() {
        let mut fx = std_fixture();
        let id = fx.id("a.txt");
        fx.rename("", "a.txt", "", "b.txt").unwrap();
        assert!(fx.p("A/b.txt").is_file());
        assert!(!fx.p("A/a.txt").exists());
        assert_eq!(std::fs::read_to_string(fx.p("B/b.txt")).unwrap(), "B:b.txt");
        assert_eq!(fx.layer_of("b.txt"), 1);
        assert_eq!(fx.id("b.txt"), id, "node id follows the rename");
        assert!(fx.gone("a.txt"));
        assert!(
            fx.tree
                .masks()
                .contains(&crate::name::fold_path(Path::new("a.txt")))
        );
        assert_eq!(fx.tree.node(id).parent, fx.tree.root());
    }

    #[test]
    fn rename_to_other_dir_creates_parents_in_source_layer() {
        let mut fx = std_fixture();
        fx.rename("", "a.txt", "textures", "moved.txt").unwrap();
        assert!(fx.p("A/textures/moved.txt").is_file());
        assert!(!fx.p("A/a.txt").exists());
        assert_eq!(fx.layer_of("textures/moved.txt"), 1);
        assert_eq!(
            fx.tree.path_of(fx.id("textures/moved.txt")),
            Path::new("textures/moved.txt")
        );
        assert!(!fx.p("overwrite/textures").exists());
    }

    #[test]
    fn rename_onto_existing_file_of_same_layer_overwrites() {
        let mut fx = fixture(
            &[
                ("orig", &[]),
                ("A", &["a.txt", "Old.TXT"]),
                ("overwrite", &[]),
            ],
            true,
        );
        fx.rename("", "a.txt", "", "old.txt").unwrap();
        assert!(!fx.p("A/a.txt").exists());
        assert_eq!(
            std::fs::read_to_string(fx.p("A/old.txt")).unwrap(),
            "A:a.txt"
        );
        assert!(
            !fx.p("A/Old.TXT").exists(),
            "differently spelled victim is gone"
        );
        assert_eq!(fx.tree.node(fx.id("OLD.txt")).name, "old.txt");
        assert_eq!(fx.tree.len(), 2);
        // exact same spelling replaces through renameat alone
        write(&fx.p("A"), "c.txt", "c");
        fx.tree = Tree::build(&fx.layers, &Skip::default());
        fx.rename("", "c.txt", "", "old.txt").unwrap();
        assert_eq!(std::fs::read_to_string(fx.p("A/old.txt")).unwrap(), "c");
    }

    #[test]
    fn rename_noreplace_with_existing_destination_is_eexist() {
        let mut fx = std_fixture();
        let r = fx.rename_with("", "a.txt", "", "b.txt", RenameFlags::NOREPLACE);
        assert_eq!(r.err(), Some(Errno::EXIST));
        assert!(fx.p("A/a.txt").is_file());
        assert_eq!(fx.layer_of("b.txt"), 2);
        assert!(fx.tree.masks().is_empty());
        // without an existing destination it works
        fx.rename_with("", "a.txt", "", "free.txt", RenameFlags::NOREPLACE)
            .unwrap();
        assert!(fx.p("A/free.txt").is_file());
    }

    #[test]
    fn rename_original_file_copies_to_create_layer() {
        let mut fx = std_fixture();
        let src = fx.p("orig/Skyrim.esm");
        let f = std::fs::File::options().write(true).open(&src).unwrap();
        let mtime = std::time::UNIX_EPOCH + std::time::Duration::new(1_000_000_000, 123_000_000);
        f.set_modified(mtime).unwrap();
        drop(f);
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&src, std::fs::Permissions::from_mode(0o640)).unwrap();

        fx.rename("", "Skyrim.esm", "", "Skyrim.bak").unwrap();
        assert_eq!(
            std::fs::read_to_string(&src).unwrap(),
            "orig:Skyrim.esm",
            "original intact"
        );
        let dst = fx.p("overwrite/Skyrim.bak");
        assert_eq!(std::fs::read_to_string(&dst).unwrap(), "orig:Skyrim.esm");
        let md = std::fs::metadata(&dst).unwrap();
        assert_eq!(md.permissions().mode() & 0o7777, 0o640);
        assert_eq!(md.modified().unwrap(), mtime);
        assert!(fx.gone("Skyrim.esm"));
        assert_eq!(fx.layer_of("skyrim.bak"), 3);
        assert!(
            fx.tree
                .masks()
                .contains(&crate::name::fold_path(Path::new("skyrim.esm")))
        );
        let leftovers: Vec<_> = std::fs::read_dir(fx.p("overwrite"))
            .unwrap()
            .map(|e| e.unwrap().file_name())
            .collect();
        assert_eq!(leftovers, [OsStr::new("Skyrim.bak")]);
    }

    #[test]
    fn rename_original_file_without_create_layer_is_erofs() {
        let mut fx = fixture(&[("orig", &["x"]), ("A", &[])], false);
        assert_eq!(fx.rename("", "x", "", "y").err(), Some(Errno::ROFS));
        assert!(fx.p("orig/x").exists());
        assert!(fx.tree.lookup(Path::new("x")).is_some());
        assert!(fx.tree.masks().is_empty());
    }

    #[test]
    fn rename_multi_layer_dir_or_original_dir_is_exdev() {
        let mut fx = std_fixture();
        // textures: orig + A + B
        assert_eq!(
            fx.rename("", "textures", "", "tex2").err(),
            Some(Errno::XDEV)
        );
        // dir that only exists in the original layer
        let mut fx2 = fixture(&[("orig", &["od/f"]), ("A", &["x"]), ("o", &[])], true);
        assert_eq!(fx2.rename("", "od", "", "od2").err(), Some(Errno::XDEV));
        assert!(fx2.p("orig/od/f").exists());
        assert!(fx2.tree.lookup(Path::new("od/f")).is_some());
        assert!(fx.p("A/textures").is_dir());
        assert!(fx.tree.masks().is_empty() && fx2.tree.masks().is_empty());
    }

    #[test]
    fn rename_synthetic_dir_is_exdev() {
        let tmp = TempDir::new().unwrap();
        std::fs::create_dir_all(tmp.path().join("orig")).unwrap();
        write(tmp.path(), "mod/a.dds", "a");
        let specs = vec![spec(tmp.path().join("mod"), "new/deep", true, false)];
        let mut fx = assemble(tmp, specs);
        assert!(fx.tree.node(fx.id("new")).dir_origins.is_empty());
        assert_eq!(fx.rename("", "new", "", "n2").err(), Some(Errno::XDEV));
    }

    #[test]
    fn rename_single_layer_dir_moves_and_rewrites_origins() {
        let mut fx = std_fixture();
        // meshes exists only in A
        fx.rename("", "meshes", "", "Models").unwrap();
        assert!(fx.p("A/Models/tree.nif").is_file());
        assert!(!fx.p("A/meshes").exists());
        let dir = fx.tree.node(fx.id("models"));
        assert_eq!(dir.name, "Models");
        assert_eq!(dir.origin.rel, Path::new("Models"));
        assert_eq!(dir.dir_origins[0].rel, Path::new("Models"));
        let f = fx.tree.node(fx.id("models/tree.nif"));
        assert_eq!(f.origin.rel, Path::new("Models/tree.nif"));
        assert!(fx.gone("meshes"));
        // the moved files are readable through their origins
        let fd = open(&fx.layers, f, OFlags::RDONLY).unwrap();
        let mut buf = [0u8; 32];
        let n = rustix::io::read(&fd, &mut buf).unwrap();
        assert_eq!(&buf[..n], b"A:meshes/tree.nif");
    }

    #[test]
    fn rename_dir_onto_empty_virtual_dir_replaces_it() {
        let mut fx = fixture(
            &[("orig", &[]), ("A", &["src/f.txt"]), ("overwrite", &[])],
            true,
        );
        std::fs::create_dir_all(fx.p("A/dst")).unwrap();
        fx.tree = Tree::build(&fx.layers, &Skip::default());
        fx.rename("", "src", "", "dst").unwrap();
        assert!(fx.p("A/dst/f.txt").is_file());
        assert!(!fx.p("A/src").exists());
        assert_eq!(
            fx.tree.node(fx.id("dst/f.txt")).origin.rel,
            Path::new("dst/f.txt")
        );
        // non-empty destination
        let mut fx = fixture(
            &[
                ("orig", &[]),
                ("A", &["src/f.txt", "dst/g.txt"]),
                ("o", &[]),
            ],
            true,
        );
        assert_eq!(fx.rename("", "src", "", "dst").err(), Some(Errno::NOTEMPTY));
        assert!(fx.p("A/src/f.txt").exists() && fx.p("A/dst/g.txt").exists());
    }

    #[test]
    fn rename_kind_mismatch_and_cycles() {
        let mut fx = fixture(
            &[
                ("orig", &[]),
                ("A", &["f.txt", "d/g.txt", "e/h.txt"]),
                ("o", &[]),
            ],
            true,
        );
        assert_eq!(fx.rename("", "f.txt", "", "d").err(), Some(Errno::ISDIR));
        assert_eq!(fx.rename("", "d", "", "f.txt").err(), Some(Errno::NOTDIR));
        assert_eq!(fx.rename("", "d", "d", "inner").err(), Some(Errno::INVAL));
        assert_eq!(fx.rename("", "nope", "", "z").err(), Some(Errno::NOENT));
        assert!(fx.p("A/f.txt").exists() && fx.p("A/d/g.txt").exists());
        // same-name rename is a no-op
        fx.rename("", "f.txt", "", "f.txt").unwrap();
        // case-only rename
        fx.rename("", "f.txt", "", "F.TXT").unwrap();
        assert!(fx.p("A/F.TXT").exists());
        assert_eq!(fx.tree.node(fx.id("f.txt")).name, "F.TXT");
        assert!(fx.tree.masks().is_empty() || fx.tree.lookup(Path::new("f.txt")).is_some());
    }

    #[test]
    fn rename_file_mapping_is_exdev() {
        let tmp = TempDir::new().unwrap();
        write(tmp.path(), "orig/Skyrim.ini", "o");
        write(tmp.path(), "extra/custom.ini", "c");
        write(tmp.path(), "ov/z", "z");
        let specs = vec![
            spec(
                tmp.path().join("extra/custom.ini"),
                "Skyrim.ini",
                false,
                false,
            ),
            spec(tmp.path().join("ov"), "", true, true),
        ];
        let mut fx = assemble(tmp, specs);
        assert_eq!(fx.layer_of("skyrim.ini"), 1);
        assert_eq!(
            fx.rename("", "Skyrim.ini", "", "Skyrim.bak").err(),
            Some(Errno::XDEV)
        );
        assert!(fx.p("extra/custom.ini").exists());
        assert!(fx.tree.lookup(Path::new("skyrim.ini")).is_some());
        // unlink of a file-mapping file deletes the real file (it counts as a mod file)
        fx.unlink("", "skyrim.ini").unwrap();
        assert!(!fx.p("extra/custom.ini").exists());
        assert!(fx.p("orig/Skyrim.ini").exists());
        assert!(fx.gone("skyrim.ini"));
    }

    #[test]
    fn rename_exchange_and_whiteout_are_einval() {
        let mut fx = std_fixture();
        for flags in [
            RenameFlags::EXCHANGE,
            RenameFlags::WHITEOUT,
            RenameFlags::NOREPLACE | RenameFlags::EXCHANGE,
        ] {
            let r = fx.rename_with("", "a.txt", "", "free.txt", flags);
            assert_eq!(r.err(), Some(Errno::INVAL));
        }
        assert!(fx.p("A/a.txt").is_file());
        assert!(!fx.p("A/free.txt").exists());
        assert!(fx.tree.lookup(Path::new("a.txt")).is_some());
    }

    #[test]
    fn masks_survive_rebuild() {
        let mut fx = std_fixture();
        fx.unlink("", "Skyrim.esm").unwrap();
        fx.unlink("textures", "rock.dds").unwrap();
        fx.rename("", "a.txt", "", "moved.txt").unwrap();
        let masks = fx.tree.masks().clone();
        let mut t = Tree::build(&fx.layers, &Skip::default());
        assert!(t.lookup(Path::new("Skyrim.esm")).is_some());
        t.apply_masks(&masks);
        assert!(t.lookup(Path::new("Skyrim.esm")).is_none());
        assert!(t.lookup(Path::new("textures/rock.dds")).is_none());
        assert!(t.lookup(Path::new("a.txt")).is_none());
        assert!(t.lookup(Path::new("moved.txt")).is_some());
        assert!(t.lookup(Path::new("textures")).is_some());
        assert_eq!(t.masks(), &masks);
    }

    #[test]
    fn ensure_parents_is_idempotent_and_rejects_escapes() {
        let fx = std_fixture();
        ensure_parents(&fx.layers, 3, Path::new("a/b/c")).unwrap();
        ensure_parents(&fx.layers, 3, Path::new("a/b/c")).unwrap();
        assert!(fx.p("overwrite/a/b/c").is_dir());
        assert_eq!(
            ensure_parents(&fx.layers, 3, Path::new("../x")).err(),
            Some(Errno::INVAL)
        );
        assert!(!fx.p("x").exists());
    }

    #[test]
    fn mkdir_after_rmdir_reuses_surviving_real_dir() {
        let mut fx = fixture(&[("orig", &[]), ("A", &[]), ("o", &[])], true);
        // a real dir in the create layer that only holds a skipped entry
        write(&fx.p("o"), "d/keep.mohidden", "k");
        let root = fx.tree.root();
        let id = mkdir(
            &mut fx.tree,
            &fx.layers,
            root,
            OsStr::new("d"),
            Mode::from_raw_mode(0o755),
        )
        .unwrap();
        // the pre-existing real dir is adopted, its content untouched
        assert_eq!(fx.tree.node(id).origin.layer, 2);
        assert!(fx.p("o/d/keep.mohidden").exists());
        // a file in the way is still EEXIST
        write(&fx.p("o"), "f", "x");
        let r = mkdir(
            &mut fx.tree,
            &fx.layers,
            root,
            OsStr::new("f"),
            Mode::from_raw_mode(0o755),
        );
        assert_eq!(r.err(), Some(Errno::EXIST));
    }
}
