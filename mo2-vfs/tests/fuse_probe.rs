//! Proves that FUSE mounts work (unprivileged, through fusermount3) where the tests run.
mod common;

use std::ffi::OsStr;
use std::time::{Duration, UNIX_EPOCH};

use fuser::{
    Config, Errno, FileAttr, FileHandle, FileType, Filesystem, FopenFlags, Generation, INodeNo,
    LockOwner, MountOption, OpenFlags, ReplyAttr, ReplyData, ReplyDirectory, ReplyEntry, ReplyOpen,
    Request,
};

const TTL: Duration = Duration::from_secs(1);
const CONTENT: &[u8] = b"hello\n";

fn attr(ino: u64, kind: FileType, size: u64) -> FileAttr {
    FileAttr {
        ino: INodeNo(ino),
        size,
        blocks: size.div_ceil(512),
        atime: UNIX_EPOCH,
        mtime: UNIX_EPOCH,
        ctime: UNIX_EPOCH,
        crtime: UNIX_EPOCH,
        kind,
        perm: if kind == FileType::Directory {
            0o755
        } else {
            0o644
        },
        nlink: if kind == FileType::Directory { 2 } else { 1 },
        uid: rustix::process::getuid().as_raw(),
        gid: rustix::process::getgid().as_raw(),
        rdev: 0,
        flags: 0,
        blksize: 512,
    }
}

struct Probe;

impl Filesystem for Probe {
    fn lookup(&self, _req: &Request, parent: INodeNo, name: &OsStr, reply: ReplyEntry) {
        if parent == INodeNo::ROOT && name == "hello.txt" {
            reply.entry(
                &TTL,
                &attr(2, FileType::RegularFile, CONTENT.len() as u64),
                Generation(0),
            );
        } else {
            reply.error(Errno::ENOENT);
        }
    }

    fn getattr(&self, _req: &Request, ino: INodeNo, _fh: Option<FileHandle>, reply: ReplyAttr) {
        match u64::from(ino) {
            1 => reply.attr(&TTL, &attr(1, FileType::Directory, 0)),
            2 => reply.attr(&TTL, &attr(2, FileType::RegularFile, CONTENT.len() as u64)),
            _ => reply.error(Errno::ENOENT),
        }
    }

    fn open(&self, _req: &Request, ino: INodeNo, _flags: OpenFlags, reply: ReplyOpen) {
        if u64::from(ino) == 2 {
            reply.opened(FileHandle(0), FopenFlags::empty());
        } else {
            reply.error(Errno::ENOENT);
        }
    }

    fn read(
        &self,
        _req: &Request,
        ino: INodeNo,
        _fh: FileHandle,
        offset: u64,
        size: u32,
        _flags: OpenFlags,
        _lock_owner: Option<LockOwner>,
        reply: ReplyData,
    ) {
        if u64::from(ino) != 2 {
            reply.error(Errno::ENOENT);
            return;
        }
        let start = (offset as usize).min(CONTENT.len());
        let end = (start + size as usize).min(CONTENT.len());
        reply.data(&CONTENT[start..end]);
    }

    fn readdir(
        &self,
        _req: &Request,
        ino: INodeNo,
        _fh: FileHandle,
        offset: u64,
        mut reply: ReplyDirectory,
    ) {
        if ino != INodeNo::ROOT {
            reply.error(Errno::ENOENT);
            return;
        }
        let entries = [
            (1, FileType::Directory, "."),
            (1, FileType::Directory, ".."),
            (2, FileType::RegularFile, "hello.txt"),
        ];
        for (i, (n, kind, name)) in entries.into_iter().enumerate().skip(offset as usize) {
            if reply.add(INodeNo(n), (i + 1) as u64, kind, name) {
                break;
            }
        }
        reply.ok();
    }
}

#[test]
fn mounts_and_reads_a_file() {
    if !common::require_fuse() {
        return;
    }
    let dir = common::tempdir();
    let mnt = dir.path().join("mnt");
    std::fs::create_dir(&mnt).unwrap();

    let mut config = Config::default();
    config.mount_options = vec![
        MountOption::FSName("mo2-vfs".to_string()),
        MountOption::Subtype("mo2-vfs".to_string()),
        MountOption::DefaultPermissions,
    ];
    let session = fuser::spawn_mount(Probe, &mnt, &config).expect("mount");

    let read = std::fs::read(mnt.join("hello.txt"));
    let names: Vec<_> = std::fs::read_dir(&mnt)
        .map(|d| d.map(|e| e.unwrap().file_name()).collect())
        .unwrap_or_default();
    session.umount_and_join().expect("unmount");

    assert_eq!(read.unwrap(), CONTENT);
    assert_eq!(names, vec![std::ffi::OsString::from("hello.txt")]);
}
