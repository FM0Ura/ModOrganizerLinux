//! The `fuser::Filesystem` implementation: inode -> node -> real file.

use crate::inodes::Inodes;
use crate::layer::Layers;
use crate::name::fold_path;
use crate::ops;
use crate::tree::{Kind, NodeId, Origin, Tree, Whiteouts};
use fuser::{
    BackingId, Errno, FileAttr, FileHandle, FileType, Filesystem, FopenFlags, Generation, INodeNo,
    InitFlags, KernelConfig, LockOwner, OpenAccMode, OpenFlags, RenameFlags, ReplyAttr,
    ReplyCreate, ReplyData, ReplyDirectory, ReplyEmpty, ReplyEntry, ReplyOpen, ReplyStatfs,
    ReplyWrite, ReplyXattr, Request, TimeOrNow, WriteFlags,
};
use parking_lot::{Mutex, RwLock};
use rustix::fs::{
    AtFlags, Mode, OFlags, Stat, Timespec, Timestamps, UTIME_NOW, UTIME_OMIT, chmodat, fchmod,
    fdatasync, fstat, fsync, ftruncate, futimens, statat, utimensat,
};
use std::collections::HashMap;
use std::ffi::{OsStr, OsString};
use std::os::fd::OwnedFd;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::Path;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, OnceLock};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

#[derive(Clone, Debug)]
pub struct VfsOptions {
    /// Kernel cache time for attributes and entries.
    pub ttl: Duration,
    /// Ask the kernel to read the real file directly (FUSE passthrough) when it allows it.
    pub try_passthrough: bool,
}

impl Default for VfsOptions {
    fn default() -> Self {
        VfsOptions {
            ttl: Duration::from_secs(60),
            try_passthrough: false,
        }
    }
}

/// State shared between the filesystem handlers, the session and the IPC loop.
///
/// Lock order is always `tree`, then `inodes` (never the other way round, and never the tree
/// write lock while holding `inodes`). `handles`/`backings` are leaf locks. The tree write
/// lock is held only around the `ops::*` call and the inode bookkeeping that must be atomic
/// with it, never during `pread`/`pwrite`/`fsync`. Kernel notifications are sent after the
/// reply and with no lock held.
pub struct VfsShared {
    pub tree: RwLock<Tree>,
    pub inodes: Mutex<Inodes>,
    /// Swapped together with the tree (under the tree write lock) by a hot update. Handlers
    /// read it while holding the tree lock, so a node's layer id always matches this list.
    layers: RwLock<Arc<Layers>>,
    /// See `generation()`.
    generation: AtomicU64,
    pub handles: Mutex<HashMap<u64, Arc<OwnedFd>>>,
    /// Kernel passthrough backing ids, alive as long as their file handle.
    backings: Mutex<HashMap<u64, BackingId>>,
    next_fh: AtomicU64,
    /// False once passthrough was found unavailable (or never requested).
    pub passthrough: AtomicBool,
    /// True when an attempt to use passthrough was refused by the kernel.
    pub passthrough_refused: AtomicBool,
    pub options: VfsOptions,
    /// Set by the session once mounted; used for cache invalidation.
    pub notifier: OnceLock<fuser::Notifier>,
}

impl VfsShared {
    /// The current layers. Call it while holding the tree lock to get the list that matches
    /// the tree's node origins.
    pub fn layers(&self) -> Arc<Layers> {
        self.layers.read().clone()
    }

    /// The tree generation: bumped by every operation that may change the tree. Read it while
    /// holding the tree lock (read) together with whatever is copied from the tree.
    pub fn generation(&self) -> u64 {
        self.generation.load(Ordering::Acquire)
    }

    /// Replaces tree and layers together if no operation touched the tree since `generation`
    /// was read (the new tree was built from a scan that missed it); otherwise gives the tree
    /// back. Returns the previous pair.
    pub fn swap_if(
        &self,
        tree: Tree,
        layers: Arc<Layers>,
        generation: u64,
    ) -> Result<(Tree, Arc<Layers>), Tree> {
        let mut t = self.tree.write();
        if self.generation() != generation {
            return Err(tree);
        }
        let old_tree = std::mem::replace(&mut *t, tree);
        let old_layers = std::mem::replace(&mut *self.layers.write(), layers);
        Ok((old_tree, old_layers))
    }

    /// Like `swap_if` but builds the tree under the write lock, from the current whiteouts.
    /// The last resort when `swap_if` keeps losing against concurrent changes.
    pub fn rebuild_locked(
        &self,
        layers: Arc<Layers>,
        build: impl FnOnce(&Whiteouts) -> Tree,
    ) -> (Tree, Arc<Layers>) {
        let mut t = self.tree.write();
        let tree = build(t.whiteouts());
        let old_tree = std::mem::replace(&mut *t, tree);
        let old_layers = std::mem::replace(&mut *self.layers.write(), layers);
        (old_tree, old_layers)
    }
}

pub struct Vfs {
    shared: Arc<VfsShared>,
}

/// What a handler needs of a node after the tree lock is released.
struct Snap {
    kind: Kind,
    origin: Origin,
    layers: Arc<Layers>,
}

type Res<T> = Result<T, Errno>;

fn errno(e: rustix::io::Errno) -> Errno {
    Errno::from_i32(e.raw_os_error())
}

/// Runs a handler body; a panic becomes EIO.
fn guarded<T>(what: &str, f: impl FnOnce() -> Res<T>) -> Res<T> {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(r) => r,
        Err(_) => {
            tracing::error!(handler = what, "panic in FUSE handler");
            Err(Errno::EIO)
        }
    }
}

/// The open flags that matter for the real file. `O_CREAT`/`O_EXCL` are the create path's;
/// `O_TRUNC` is done by the kernel through `setattr`.
fn open_oflags(flags: i32) -> OFlags {
    let keep = libc::O_ACCMODE | libc::O_APPEND | libc::O_SYNC | libc::O_DSYNC;
    OFlags::from_bits_retain((flags & keep) as u32)
}

fn timespec(t: Option<TimeOrNow>) -> Timespec {
    match t {
        None => Timespec {
            tv_sec: 0,
            tv_nsec: UTIME_OMIT as _,
        },
        Some(TimeOrNow::Now) => Timespec {
            tv_sec: 0,
            tv_nsec: UTIME_NOW as _,
        },
        Some(TimeOrNow::SpecificTime(t)) => match t.duration_since(UNIX_EPOCH) {
            Ok(d) => Timespec {
                tv_sec: d.as_secs() as _,
                tv_nsec: d.subsec_nanos() as _,
            },
            Err(e) => {
                let d = e.duration();
                let (mut secs, mut nsec) = (-(d.as_secs() as i64), d.subsec_nanos() as i64);
                if nsec > 0 {
                    secs -= 1;
                    nsec = 1_000_000_000 - nsec;
                }
                Timespec {
                    tv_sec: secs as _,
                    tv_nsec: nsec as _,
                }
            }
        },
    }
}

fn system_time(secs: i64, nsec: u64) -> SystemTime {
    if secs >= 0 {
        UNIX_EPOCH + Duration::new(secs as u64, nsec as u32)
    } else {
        UNIX_EPOCH - Duration::new(secs.unsigned_abs(), 0) + Duration::new(0, nsec as u32)
    }
}

impl Vfs {
    pub fn new(layers: Layers, tree: Tree, options: VfsOptions) -> Vfs {
        let passthrough = options.try_passthrough;
        Vfs {
            shared: Arc::new(VfsShared {
                tree: RwLock::new(tree),
                inodes: Mutex::new(Inodes::new()),
                layers: RwLock::new(Arc::new(layers)),
                generation: AtomicU64::new(0),
                handles: Mutex::new(HashMap::new()),
                backings: Mutex::new(HashMap::new()),
                next_fh: AtomicU64::new(1),
                passthrough: AtomicBool::new(passthrough),
                passthrough_refused: AtomicBool::new(false),
                options,
                notifier: OnceLock::new(),
            }),
        }
    }

    pub fn shared(&self) -> Arc<VfsShared> {
        self.shared.clone()
    }

    /// Resolves an inode under the tree lock; no I/O happens here.
    fn snap(&self, ino: u64) -> Res<Snap> {
        let key = self
            .shared
            .inodes
            .lock()
            .key_of(ino)
            .cloned()
            .ok_or(Errno::ENOENT)?;
        let tree = self.shared.tree.read();
        let id = tree.lookup_folded(&key).ok_or(Errno::ENOENT)?;
        let node = tree.node(id);
        Ok(Snap {
            kind: node.kind,
            origin: node.origin.clone(),
            layers: self.shared.layers(),
        })
    }

    fn attr_of(&self, layers: &Layers, ino: u64, kind: Kind, origin: &Origin) -> Res<FileAttr> {
        let layer = layers.list.get(origin.layer).ok_or(Errno::EIO)?;
        // Symlinks are followed: the node reports the kind of what they point to.
        let st = statat(&layer.dir, &origin.rel, AtFlags::empty()).map_err(errno)?;
        Ok(attr_from_stat(ino, kind, &st))
    }
}

#[allow(clippy::unnecessary_cast)] // the integer widths of `Stat` differ per target
fn attr_from_stat(ino: u64, kind: Kind, st: &Stat) -> FileAttr {
    let (ftype, nlink) = match kind {
        Kind::File => (FileType::RegularFile, st.st_nlink as u32),
        Kind::Dir => (FileType::Directory, 2),
    };
    FileAttr {
        ino: INodeNo(ino),
        size: st.st_size as u64,
        blocks: st.st_blocks as u64,
        atime: system_time(st.st_atime as i64, st.st_atime_nsec as u64),
        mtime: system_time(st.st_mtime as i64, st.st_mtime_nsec as u64),
        ctime: system_time(st.st_ctime as i64, st.st_ctime_nsec as u64),
        crtime: UNIX_EPOCH,
        kind: ftype,
        perm: (st.st_mode & 0o7777) as u16,
        nlink,
        uid: st.st_uid,
        gid: st.st_gid,
        rdev: 0,
        flags: 0,
        blksize: st.st_blksize as u32,
    }
}

impl Vfs {
    /// The attributes of the child, and whether `name` is its displayed spelling.
    fn do_lookup(&self, parent: u64, name: &OsStr) -> Res<(FileAttr, bool)> {
        let pkey = self
            .shared
            .inodes
            .lock()
            .key_of(parent)
            .cloned()
            .ok_or(Errno::ENOENT)?;
        let (kind, origin, layers, key, exact) = {
            let tree = self.shared.tree.read();
            let pid = tree.lookup_folded(&pkey).ok_or(Errno::ENOENT)?;
            if tree.node(pid).kind != Kind::Dir {
                return Err(Errno::ENOTDIR);
            }
            let id = tree.child(pid, name).ok_or(Errno::ENOENT)?;
            let node = tree.node(id);
            (
                node.kind,
                node.origin.clone(),
                self.shared.layers(),
                fold_path(&tree.path_of(id)),
                node.name == name,
            )
        };
        let ino = self.shared.inodes.lock().ino_for(&key);
        Ok((self.attr_of(&layers, ino, kind, &origin)?, exact))
    }

    fn do_getattr(&self, ino: u64) -> Res<FileAttr> {
        let s = self.snap(ino)?;
        self.attr_of(&s.layers, ino, s.kind, &s.origin)
    }

    fn do_readdir(&self, ino: u64, offset: u64, reply: &mut ReplyDirectory) -> Res<()> {
        let key = self
            .shared
            .inodes
            .lock()
            .key_of(ino)
            .cloned()
            .ok_or(Errno::ENOENT)?;
        let tree = self.shared.tree.read();
        let id = tree.lookup_folded(&key).ok_or(Errno::ENOENT)?;
        if tree.node(id).kind != Kind::Dir {
            return Err(Errno::ENOTDIR);
        }
        let mut inodes = self.shared.inodes.lock();
        // Entry i (0 = ".", 1 = "..", 2.. = children in map order) has offset i + 1.
        if offset == 0 && reply.add(INodeNo(ino), 1, FileType::Directory, ".") {
            return Ok(());
        }
        if offset <= 1 {
            let parent = tree.node(id).parent;
            let pino = inodes.ino_for(&fold_path(&tree.path_of(parent)));
            if reply.add(INodeNo(pino), 2, FileType::Directory, "..") {
                return Ok(());
            }
        }
        let skip = offset.saturating_sub(2) as usize;
        for (i, (cid, node)) in tree.children(id).enumerate().skip(skip) {
            let cino = inodes.ino_for(&fold_path(&tree.path_of(cid)));
            let ft = match node.kind {
                Kind::File => FileType::RegularFile,
                Kind::Dir => FileType::Directory,
            };
            if reply.add(INodeNo(cino), (i + 3) as u64, ft, &node.name) {
                break;
            }
        }
        Ok(())
    }

    fn do_open(
        &self,
        ino: u64,
        flags: OpenFlags,
        reply: &ReplyOpen,
    ) -> Res<(u64, Option<BackingId>)> {
        let writable = flags.acc_mode() != OpenAccMode::O_RDONLY;
        let key = self
            .shared
            .inodes
            .lock()
            .key_of(ino)
            .cloned()
            .ok_or(Errno::ENOENT)?;
        let fd = {
            let tree = self.shared.tree.read();
            let id = tree.lookup_folded(&key).ok_or(Errno::ENOENT)?;
            let node = tree.node(id);
            if node.kind == Kind::Dir {
                return Err(Errno::EISDIR);
            }
            // Editing mod and original files in place is allowed (spec 1.1): no copy-up.
            ops::open(&self.shared.layers(), node, open_oflags(flags.0)).map_err(errno)?
        };
        let fd = Arc::new(fd);
        let mut backing = None;
        if !writable
            && self.shared.options.try_passthrough
            && self.shared.passthrough.load(Ordering::Relaxed)
        {
            match reply.open_backing(&*fd) {
                Ok(b) => backing = Some(b),
                Err(e) => {
                    if self.shared.passthrough.swap(false, Ordering::Relaxed) {
                        tracing::info!(error = %e, "kernel passthrough unavailable, using pread");
                    }
                    self.shared
                        .passthrough_refused
                        .store(true, Ordering::Relaxed);
                }
            }
        }
        let fh = self.shared.next_fh.fetch_add(1, Ordering::Relaxed);
        self.shared.handles.lock().insert(fh, fd);
        Ok((fh, backing))
    }

    fn do_read(&self, fh: u64, offset: u64, size: u32) -> Res<Vec<u8>> {
        let fd = self
            .shared
            .handles
            .lock()
            .get(&fh)
            .cloned()
            .ok_or(Errno::EBADF)?;
        let mut buf = vec![0u8; size as usize];
        let mut got = 0;
        while got < buf.len() {
            match rustix::io::pread(&*fd, &mut buf[got..], offset + got as u64) {
                Ok(0) => break,
                Ok(n) => got += n,
                Err(rustix::io::Errno::INTR) => continue,
                Err(e) => return Err(errno(e)),
            }
        }
        buf.truncate(got);
        Ok(buf)
    }

    fn handle(&self, fh: u64) -> Res<Arc<OwnedFd>> {
        self.shared
            .handles
            .lock()
            .get(&fh)
            .cloned()
            .ok_or(Errno::EBADF)
    }

    fn key_of(&self, ino: u64) -> Res<crate::name::Key> {
        self.shared
            .inodes
            .lock()
            .key_of(ino)
            .cloned()
            .ok_or(Errno::ENOENT)
    }

    /// Runs `f` with the tree write lock and the resolved directory `parent`.
    fn mutate<T>(&self, parent: u64, f: impl FnOnce(&mut Tree, NodeId) -> Res<T>) -> Res<T> {
        // Tree first, then inodes.
        let mut tree = self.shared.tree.write();
        self.shared.generation.fetch_add(1, Ordering::AcqRel);
        let pkey = self.key_of(parent)?;
        let pid = tree.lookup_folded(&pkey).ok_or(Errno::ENOENT)?;
        f(&mut tree, pid)
    }

    fn do_write(&self, fh: u64, offset: u64, data: &[u8]) -> Res<u32> {
        let fd = self.handle(fh)?;
        let mut done = 0;
        while done < data.len() {
            match rustix::io::pwrite(&*fd, &data[done..], offset + done as u64) {
                Ok(0) => return Err(Errno::EIO),
                Ok(n) => done += n,
                Err(rustix::io::Errno::INTR) => continue,
                // Report what was written if some bytes already went out.
                Err(_) if done > 0 => break,
                Err(e) => return Err(errno(e)),
            }
        }
        Ok(done as u32)
    }

    fn do_sync(&self, fh: u64, datasync: bool) -> Res<()> {
        let fd = self.handle(fh)?;
        let r = if datasync {
            fdatasync(&*fd)
        } else {
            fsync(&*fd)
        };
        r.map_err(errno)
    }

    /// `flush` runs on every close(): like native filesystems it does not sync (that is
    /// `fsync`'s job). It only checks that the handle exists.
    fn do_flush(&self, fh: u64) -> Res<()> {
        self.handle(fh).map(|_| ())
    }

    fn do_create(&self, parent: u64, name: &OsStr, mode: u32, flags: i32) -> Res<(FileAttr, u64)> {
        let (ino, origin, layers, fd) = self.mutate(parent, |tree, pid| {
            let layers = self.shared.layers();
            let (id, fd) = ops::create_file(
                tree,
                &layers,
                pid,
                name,
                Mode::from_raw_mode(mode & 0o7777),
                open_oflags(flags),
            )
            .map_err(errno)?;
            let key = fold_path(&tree.path_of(id));
            let ino = self.shared.inodes.lock().ino_for(&key);
            Ok((ino, tree.node(id).origin.clone(), layers, fd))
        })?;
        let attr = self.attr_of(&layers, ino, Kind::File, &origin)?;
        let fh = self.shared.next_fh.fetch_add(1, Ordering::Relaxed);
        self.shared.handles.lock().insert(fh, Arc::new(fd));
        Ok((attr, fh))
    }

    fn do_mkdir(&self, parent: u64, name: &OsStr, mode: u32) -> Res<FileAttr> {
        let (ino, origin, layers) = self.mutate(parent, |tree, pid| {
            let layers = self.shared.layers();
            let id = ops::mkdir(tree, &layers, pid, name, Mode::from_raw_mode(mode & 0o7777))
                .map_err(errno)?;
            let key = fold_path(&tree.path_of(id));
            let ino = self.shared.inodes.lock().ino_for(&key);
            Ok((ino, tree.node(id).origin.clone(), layers))
        })?;
        self.attr_of(&layers, ino, Kind::Dir, &origin)
    }

    /// unlink (`dir == false`) or rmdir. Returns the entries the kernel may still cache.
    fn do_remove(&self, parent: u64, name: &OsStr, dir: bool) -> Res<Vec<(u64, OsString)>> {
        self.mutate(parent, |tree, pid| {
            let shown = tree.child(pid, name).map(|c| tree.node(c).name.clone());
            let layers = self.shared.layers();
            if dir {
                ops::rmdir(tree, &layers, pid, name)
            } else {
                ops::unlink(tree, &layers, pid, name)
            }
            .map_err(errno)?;
            Ok(shown
                .filter(|s| s != name)
                .map(|s| vec![(parent, s)])
                .unwrap_or_default())
        })
    }

    fn do_rename(
        &self,
        parent: u64,
        name: &OsStr,
        newparent: u64,
        newname: &OsStr,
        flags: RenameFlags,
    ) -> Res<Vec<(u64, OsString)>> {
        self.mutate(parent, |tree, pid| {
            let npkey = self.key_of(newparent)?;
            let npid = tree.lookup_folded(&npkey).ok_or(Errno::ENOENT)?;
            let src = tree.child(pid, name);
            let old_key = src.map(|s| fold_path(&tree.path_of(s)));
            let old_shown = src.map(|s| tree.node(s).name.clone());
            let dest_shown = tree
                .child(npid, newname)
                .filter(|&d| Some(d) != src)
                .map(|d| tree.node(d).name.clone());
            ops::rename(
                tree,
                &self.shared.layers(),
                pid,
                name,
                npid,
                newname,
                rustix::fs::RenameFlags::from_bits_retain(flags.bits()),
            )
            .map_err(errno)?;
            if let Some(old_key) = old_key {
                let new_key = fold_path(&tree.path_of(npid).join(newname));
                // Inodes under the old path follow the move; those of a replaced
                // destination stay valid as aliases of the new path.
                self.shared.inodes.lock().rekey_prefix(&old_key, &new_key);
            }
            let mut stale = Vec::new();
            if let Some(s) = old_shown.filter(|s| s != name) {
                stale.push((parent, s));
            }
            if let Some(s) = dest_shown {
                stale.push((newparent, s));
            }
            Ok(stale)
        })
    }

    /// Tells the kernel to forget names whose spelling differs from the one it was asked to
    /// remove. Sent from a short-lived thread: the notification needs the parent's inode
    /// lock, which the kernel still holds while the request being answered is in flight.
    fn invalidate(&self, entries: Vec<(u64, OsString)>) {
        if entries.is_empty() {
            return;
        }
        let Some(notifier) = self.shared.notifier.get().cloned() else {
            return;
        };
        let spawned = std::thread::Builder::new()
            .name("mo2-vfs-inval".into())
            .spawn(move || {
                for (parent, name) in entries {
                    let _ = notifier.inval_entry(INodeNo(parent), &name);
                }
            });
        if let Err(e) = spawned {
            tracing::warn!(error = %e, "cannot start the cache invalidation thread");
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn do_setattr(
        &self,
        ino: u64,
        mode: Option<u32>,
        uid: Option<u32>,
        gid: Option<u32>,
        size: Option<u64>,
        atime: Option<TimeOrNow>,
        mtime: Option<TimeOrNow>,
        fh: Option<u64>,
    ) -> Res<FileAttr> {
        let key = self.key_of(ino)?;
        let times = (atime.is_some() || mtime.is_some()).then(|| Timestamps {
            last_access: timespec(atime),
            last_modification: timespec(mtime),
        });
        let perm = mode.map(|m| Mode::from_raw_mode(m & 0o7777));

        // Open handle: act on it. The real file may even have been unlinked meanwhile.
        if let Some(fh) = fh {
            let fd = self.handle(fh)?;
            let st = fstat(&*fd).map_err(errno)?;
            if uid.is_some_and(|u| u != st.st_uid) || gid.is_some_and(|g| g != st.st_gid) {
                return Err(Errno::EPERM);
            }
            if let Some(size) = size {
                ftruncate(&*fd, size).map_err(errno)?;
            }
            if let Some(p) = perm {
                fchmod(&*fd, p).map_err(errno)?;
            }
            if let Some(t) = &times {
                futimens(&*fd, t).map_err(errno)?;
            }
            let st = fstat(&*fd).map_err(errno)?;
            return Ok(attr_from_stat(ino, Kind::File, &st));
        }

        // By path. The size change opens the real file under the tree read lock and
        // truncates after releasing it; mode and times are short `*at` calls under the lock.
        let trunc_fd = match size {
            Some(size) => {
                let tree = self.shared.tree.read();
                let id = tree.lookup_folded(&key).ok_or(Errno::ENOENT)?;
                let node = tree.node(id);
                if node.kind == Kind::Dir {
                    return Err(Errno::EISDIR);
                }
                self.check_owner(node, uid, gid)?;
                let fd = ops::open(&self.shared.layers(), node, OFlags::WRONLY).map_err(errno)?;
                Some((fd, size))
            }
            None => None,
        };
        if let Some((fd, size)) = trunc_fd {
            ftruncate(&fd, size).map_err(errno)?;
        }
        let tree = self.shared.tree.read();
        let id = tree.lookup_folded(&key).ok_or(Errno::ENOENT)?;
        let node = tree.node(id);
        self.check_owner(node, uid, gid)?;
        let layers = self.shared.layers();
        let layer = layers.list.get(node.origin.layer).ok_or(Errno::EIO)?;
        let rel: &Path = &node.origin.rel;
        if let Some(p) = perm {
            chmodat(&layer.dir, rel, p, AtFlags::empty()).map_err(errno)?;
        }
        if let Some(t) = &times {
            utimensat(&layer.dir, rel, t, AtFlags::empty()).map_err(errno)?;
        }
        self.attr_of(&layers, ino, node.kind, &node.origin)
    }

    /// uid/gid changes are refused unless they change nothing.
    fn check_owner(&self, node: &crate::tree::Node, uid: Option<u32>, gid: Option<u32>) -> Res<()> {
        if uid.is_none() && gid.is_none() {
            return Ok(());
        }
        let layers = self.shared.layers();
        let layer = layers.list.get(node.origin.layer).ok_or(Errno::EIO)?;
        let st = statat(&layer.dir, &node.origin.rel, AtFlags::empty()).map_err(errno)?;
        if uid.is_some_and(|u| u != st.st_uid) || gid.is_some_and(|g| g != st.st_gid) {
            return Err(Errno::EPERM);
        }
        Ok(())
    }

    fn do_statfs(&self) -> Res<rustix::fs::StatVfs> {
        let layers = self.shared.layers();
        let layers = &layers.list;
        let layer = layers.iter().find(|l| l.create).unwrap_or(&layers[0]);
        rustix::fs::fstatvfs(&layer.dir).map_err(errno)
    }
}

impl Filesystem for Vfs {
    fn init(&mut self, _req: &Request, config: &mut KernelConfig) -> std::io::Result<()> {
        if self.shared.options.try_passthrough {
            let ok = config.add_capabilities(InitFlags::FUSE_PASSTHROUGH).is_ok()
                && config.set_max_stack_depth(2).is_ok();
            if !ok {
                tracing::info!("kernel has no FUSE passthrough, using pread");
                self.shared.passthrough.store(false, Ordering::Relaxed);
                self.shared
                    .passthrough_refused
                    .store(true, Ordering::Relaxed);
            }
        }
        Ok(())
    }

    fn lookup(&self, _req: &Request, parent: INodeNo, name: &OsStr, reply: ReplyEntry) {
        match guarded("lookup", || self.do_lookup(parent.into(), name)) {
            Ok((attr, exact)) => {
                // The kernel is told about a rename or removal only under the spelling it
                // used, so other spellings of the same name must not stay cached.
                let ttl = if exact {
                    self.shared.options.ttl
                } else {
                    Duration::ZERO
                };
                reply.entry(&ttl, &attr, Generation(0));
            }
            Err(e) => reply.error(e),
        }
    }

    fn getattr(&self, _req: &Request, ino: INodeNo, fh: Option<FileHandle>, reply: ReplyAttr) {
        match guarded("getattr", || {
            // With a handle the answer is about that open file, which may be unlinked by now.
            if let Some(fd) = fh.and_then(|fh| self.handle(fh.into()).ok()) {
                let st = fstat(&*fd).map_err(errno)?;
                return Ok(attr_from_stat(ino.into(), Kind::File, &st));
            }
            self.do_getattr(ino.into())
        }) {
            Ok(attr) => reply.attr(&self.shared.options.ttl, &attr),
            Err(e) => reply.error(e),
        }
    }

    fn readdir(
        &self,
        _req: &Request,
        ino: INodeNo,
        _fh: FileHandle,
        offset: u64,
        mut reply: ReplyDirectory,
    ) {
        match guarded("readdir", || {
            self.do_readdir(ino.into(), offset, &mut reply)
        }) {
            Ok(()) => reply.ok(),
            Err(e) => reply.error(e),
        }
    }

    fn open(&self, _req: &Request, ino: INodeNo, flags: OpenFlags, reply: ReplyOpen) {
        match guarded("open", || self.do_open(ino.into(), flags, &reply)) {
            Ok((fh, Some(backing))) => {
                let mut backings = self.shared.backings.lock();
                backings.insert(fh, backing);
                reply.opened_passthrough(FileHandle(fh), FopenFlags::empty(), &backings[&fh]);
            }
            Ok((fh, None)) => reply.opened(FileHandle(fh), FopenFlags::empty()),
            Err(e) => reply.error(e),
        }
    }

    fn read(
        &self,
        _req: &Request,
        _ino: INodeNo,
        fh: FileHandle,
        offset: u64,
        size: u32,
        _flags: OpenFlags,
        _lock_owner: Option<LockOwner>,
        reply: ReplyData,
    ) {
        match guarded("read", || self.do_read(fh.into(), offset, size)) {
            Ok(data) => reply.data(&data),
            Err(e) => reply.error(e),
        }
    }

    fn release(
        &self,
        _req: &Request,
        _ino: INodeNo,
        fh: FileHandle,
        _flags: OpenFlags,
        _lock_owner: Option<LockOwner>,
        _flush: bool,
        reply: ReplyEmpty,
    ) {
        let fh: u64 = fh.into();
        self.shared.handles.lock().remove(&fh);
        self.shared.backings.lock().remove(&fh);
        reply.ok();
    }

    fn write(
        &self,
        _req: &Request,
        _ino: INodeNo,
        fh: FileHandle,
        offset: u64,
        data: &[u8],
        _write_flags: WriteFlags,
        _flags: OpenFlags,
        _lock_owner: Option<LockOwner>,
        reply: ReplyWrite,
    ) {
        match guarded("write", || self.do_write(fh.into(), offset, data)) {
            Ok(n) => reply.written(n),
            Err(e) => reply.error(e),
        }
    }

    fn flush(
        &self,
        _req: &Request,
        _ino: INodeNo,
        fh: FileHandle,
        _lock_owner: LockOwner,
        reply: ReplyEmpty,
    ) {
        match guarded("flush", || self.do_flush(fh.into())) {
            Ok(()) => reply.ok(),
            Err(e) => reply.error(e),
        }
    }

    fn fsync(
        &self,
        _req: &Request,
        _ino: INodeNo,
        fh: FileHandle,
        datasync: bool,
        reply: ReplyEmpty,
    ) {
        match guarded("fsync", || self.do_sync(fh.into(), datasync)) {
            Ok(()) => reply.ok(),
            Err(e) => reply.error(e),
        }
    }

    fn setattr(
        &self,
        _req: &Request,
        ino: INodeNo,
        mode: Option<u32>,
        uid: Option<u32>,
        gid: Option<u32>,
        size: Option<u64>,
        atime: Option<TimeOrNow>,
        mtime: Option<TimeOrNow>,
        _ctime: Option<SystemTime>,
        fh: Option<FileHandle>,
        _crtime: Option<SystemTime>,
        _chgtime: Option<SystemTime>,
        _bkuptime: Option<SystemTime>,
        _flags: Option<fuser::BsdFileFlags>,
        reply: ReplyAttr,
    ) {
        let r = guarded("setattr", || {
            self.do_setattr(
                ino.into(),
                mode,
                uid,
                gid,
                size,
                atime,
                mtime,
                fh.map(Into::into),
            )
        });
        match r {
            Ok(attr) => reply.attr(&self.shared.options.ttl, &attr),
            Err(e) => reply.error(e),
        }
    }

    fn create(
        &self,
        _req: &Request,
        parent: INodeNo,
        name: &OsStr,
        mode: u32,
        _umask: u32,
        flags: i32,
        reply: ReplyCreate,
    ) {
        match guarded("create", || {
            self.do_create(parent.into(), name, mode, flags)
        }) {
            Ok((attr, fh)) => reply.created(
                &self.shared.options.ttl,
                &attr,
                Generation(0),
                FileHandle(fh),
                FopenFlags::empty(),
            ),
            Err(e) => reply.error(e),
        }
    }

    fn mkdir(
        &self,
        _req: &Request,
        parent: INodeNo,
        name: &OsStr,
        mode: u32,
        _umask: u32,
        reply: ReplyEntry,
    ) {
        match guarded("mkdir", || self.do_mkdir(parent.into(), name, mode)) {
            Ok(attr) => reply.entry(&self.shared.options.ttl, &attr, Generation(0)),
            Err(e) => reply.error(e),
        }
    }

    fn unlink(&self, _req: &Request, parent: INodeNo, name: &OsStr, reply: ReplyEmpty) {
        match guarded("unlink", || self.do_remove(parent.into(), name, false)) {
            Ok(stale) => {
                reply.ok();
                self.invalidate(stale);
            }
            Err(e) => reply.error(e),
        }
    }

    fn rmdir(&self, _req: &Request, parent: INodeNo, name: &OsStr, reply: ReplyEmpty) {
        match guarded("rmdir", || self.do_remove(parent.into(), name, true)) {
            Ok(stale) => {
                reply.ok();
                self.invalidate(stale);
            }
            Err(e) => reply.error(e),
        }
    }

    fn rename(
        &self,
        _req: &Request,
        parent: INodeNo,
        name: &OsStr,
        newparent: INodeNo,
        newname: &OsStr,
        flags: RenameFlags,
        reply: ReplyEmpty,
    ) {
        let r = guarded("rename", || {
            self.do_rename(parent.into(), name, newparent.into(), newname, flags)
        });
        match r {
            Ok(stale) => {
                reply.ok();
                self.invalidate(stale);
            }
            Err(e) => reply.error(e),
        }
    }

    fn mknod(
        &self,
        _req: &Request,
        _parent: INodeNo,
        _name: &OsStr,
        _mode: u32,
        _umask: u32,
        _rdev: u32,
        reply: ReplyEntry,
    ) {
        reply.error(Errno::EPERM);
    }

    fn symlink(
        &self,
        _req: &Request,
        _parent: INodeNo,
        _link_name: &OsStr,
        _target: &Path,
        reply: ReplyEntry,
    ) {
        reply.error(Errno::EPERM);
    }

    fn link(
        &self,
        _req: &Request,
        _ino: INodeNo,
        _newparent: INodeNo,
        _newname: &OsStr,
        reply: ReplyEntry,
    ) {
        reply.error(Errno::EPERM);
    }

    fn setxattr(
        &self,
        _req: &Request,
        _ino: INodeNo,
        _name: &OsStr,
        _value: &[u8],
        _flags: i32,
        _position: u32,
        reply: ReplyEmpty,
    ) {
        reply.error(Errno::ENOTSUP);
    }

    fn getxattr(
        &self,
        _req: &Request,
        _ino: INodeNo,
        _name: &OsStr,
        _size: u32,
        reply: ReplyXattr,
    ) {
        reply.error(Errno::ENOTSUP);
    }

    fn listxattr(&self, _req: &Request, _ino: INodeNo, _size: u32, reply: ReplyXattr) {
        reply.error(Errno::ENOTSUP);
    }

    fn removexattr(&self, _req: &Request, _ino: INodeNo, _name: &OsStr, reply: ReplyEmpty) {
        reply.error(Errno::ENOTSUP);
    }

    fn statfs(&self, _req: &Request, _ino: INodeNo, reply: ReplyStatfs) {
        match guarded("statfs", || self.do_statfs()) {
            Ok(s) => reply.statfs(
                s.f_blocks,
                s.f_bfree,
                s.f_bavail,
                s.f_files,
                s.f_ffree,
                s.f_bsize as u32,
                s.f_namemax as u32,
                s.f_frsize as u32,
            ),
            Err(e) => reply.error(e),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn panic_in_a_handler_becomes_eio() {
        let r: Res<()> = guarded("test", || panic!("boom"));
        assert_eq!(r.unwrap_err(), Errno::EIO);
        assert_eq!(guarded("test", || Ok(3)).unwrap(), 3);
    }
}
