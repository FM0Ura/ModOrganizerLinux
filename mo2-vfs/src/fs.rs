//! The `fuser::Filesystem` implementation: inode -> node -> real file.

use crate::inodes::Inodes;
use crate::layer::Layers;
use crate::name::fold_path;
use crate::ops;
use crate::tree::{Kind, Origin, Tree};
use fuser::{
    BackingId, Errno, FileAttr, FileHandle, FileType, Filesystem, FopenFlags, Generation, INodeNo,
    InitFlags, KernelConfig, LockOwner, OpenAccMode, OpenFlags, ReplyAttr, ReplyData,
    ReplyDirectory, ReplyEmpty, ReplyEntry, ReplyOpen, ReplyStatfs, Request,
};
use parking_lot::{Mutex, RwLock};
use rustix::fs::{AtFlags, OFlags, statat};
use std::collections::HashMap;
use std::ffi::OsStr;
use std::os::fd::OwnedFd;
use std::panic::{AssertUnwindSafe, catch_unwind};
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
pub struct VfsShared {
    pub tree: RwLock<Tree>,
    pub inodes: Mutex<Inodes>,
    pub layers: Layers,
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

pub struct Vfs {
    shared: Arc<VfsShared>,
}

/// What a handler needs of a node after the tree lock is released.
struct Snap {
    kind: Kind,
    origin: Origin,
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
                layers,
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
        })
    }

    fn attr_of(&self, ino: u64, kind: Kind, origin: &Origin) -> Res<FileAttr> {
        let layer = self
            .shared
            .layers
            .list
            .get(origin.layer)
            .ok_or(Errno::EIO)?;
        // Symlinks are followed: the node reports the kind of what they point to.
        let st = statat(&layer.dir, &origin.rel, AtFlags::empty()).map_err(errno)?;
        let (ftype, nlink) = match kind {
            Kind::File => (FileType::RegularFile, st.st_nlink as u32),
            Kind::Dir => (FileType::Directory, 2),
        };
        Ok(FileAttr {
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
        })
    }

    fn do_lookup(&self, parent: u64, name: &OsStr) -> Res<FileAttr> {
        let pkey = self
            .shared
            .inodes
            .lock()
            .key_of(parent)
            .cloned()
            .ok_or(Errno::ENOENT)?;
        let (kind, origin, key) = {
            let tree = self.shared.tree.read();
            let pid = tree.lookup_folded(&pkey).ok_or(Errno::ENOENT)?;
            if tree.node(pid).kind != Kind::Dir {
                return Err(Errno::ENOTDIR);
            }
            let id = tree.child(pid, name).ok_or(Errno::ENOENT)?;
            let node = tree.node(id);
            (node.kind, node.origin.clone(), fold_path(&tree.path_of(id)))
        };
        let ino = self.shared.inodes.lock().ino_for(&key);
        self.attr_of(ino, kind, &origin)
    }

    fn do_getattr(&self, ino: u64) -> Res<FileAttr> {
        let s = self.snap(ino)?;
        self.attr_of(ino, s.kind, &s.origin)
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
        if flags.acc_mode() != OpenAccMode::O_RDONLY {
            // Writes arrive with the write path (copy-up on open).
            return Err(Errno::EROFS);
        }
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
            ops::open(&self.shared.layers, node, OFlags::RDONLY).map_err(errno)?
        };
        let fd = Arc::new(fd);
        let mut backing = None;
        if self.shared.options.try_passthrough && self.shared.passthrough.load(Ordering::Relaxed) {
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

    fn do_statfs(&self) -> Res<rustix::fs::StatVfs> {
        let layers = &self.shared.layers.list;
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
            Ok(attr) => reply.entry(&self.shared.options.ttl, &attr, Generation(0)),
            Err(e) => reply.error(e),
        }
    }

    fn getattr(&self, _req: &Request, ino: INodeNo, _fh: Option<FileHandle>, reply: ReplyAttr) {
        match guarded("getattr", || self.do_getattr(ino.into())) {
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
