//! Stable inode numbers: the folded virtual path is the identity, the number lives as long as
//! the mount.

use crate::name::Key;
use std::collections::HashMap;

pub const ROOT_INO: u64 = 1;

pub struct Inodes {
    by_key: HashMap<Key, u64>,
    by_ino: HashMap<u64, Key>,
    next: u64,
}

impl Default for Inodes {
    fn default() -> Self {
        Self::new()
    }
}

impl Inodes {
    pub fn new() -> Inodes {
        let root = Key(Vec::new());
        Inodes {
            by_key: HashMap::from([(root.clone(), ROOT_INO)]),
            by_ino: HashMap::from([(ROOT_INO, root)]),
            next: ROOT_INO + 1,
        }
    }

    /// Inode of `key`, allocating one the first time it is seen.
    pub fn ino_for(&mut self, key: &Key) -> u64 {
        if let Some(&ino) = self.by_key.get(key) {
            return ino;
        }
        let ino = self.next;
        self.next += 1;
        self.by_key.insert(key.clone(), ino);
        self.by_ino.insert(ino, key.clone());
        ino
    }

    pub fn key_of(&self, ino: u64) -> Option<&Key> {
        self.by_ino.get(&ino)
    }

    /// A directory moved from `old` to `new`: every known key at or under `old` keeps its
    /// inode under the new prefix. Inodes already known under `new` (that name was replaced)
    /// stay valid as aliases of the new path: the kernel may still hold a dentry for another
    /// spelling of it, and the path is what an inode number stands for.
    pub fn rekey_prefix(&mut self, old: &Key, new: &Key) {
        if old == new {
            return;
        }
        let mut aliases: Vec<(u64, Key)> = Vec::new();
        let under = |k: &Key, p: &Key| {
            k == p || (k.0.len() > p.0.len() && k.0.starts_with(&p.0) && k.0[p.0.len()] == b'/')
        };
        let stale: Vec<Key> = self
            .by_key
            .keys()
            .filter(|k| !k.0.is_empty() && under(k, new))
            .cloned()
            .collect();
        for k in stale {
            if let Some(ino) = self.by_key.remove(&k) {
                aliases.push((ino, k));
            }
        }
        let moved: Vec<Key> = self
            .by_key
            .keys()
            .filter(|k| !k.0.is_empty() && under(k, old))
            .cloned()
            .collect();
        for k in moved {
            let ino = self.by_key.remove(&k).expect("listed above");
            let mut bytes = new.0.clone();
            bytes.extend_from_slice(&k.0[old.0.len()..]);
            let nk = Key(bytes);
            self.by_ino.insert(ino, nk.clone());
            self.by_key.insert(nk, ino);
        }
        // Their path is now served by the moved entry.
        for (ino, k) in aliases {
            self.by_ino.insert(ino, k);
        }
    }

    /// Every known inode with its key. Several inodes may share a key (aliases left by a
    /// rename that replaced a name); aliases are never removed before unmount.
    pub fn known(&self) -> impl Iterator<Item = (u64, &Key)> {
        self.by_ino.iter().map(|(&i, k)| (i, k))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::name::fold_path;
    use std::path::Path;

    fn k(p: &str) -> Key {
        fold_path(Path::new(p))
    }

    #[test]
    fn root_is_one_and_numbers_are_stable() {
        let mut i = Inodes::new();
        assert_eq!(i.ino_for(&k("")), 1);
        let a = i.ino_for(&k("Textures/Rock.dds"));
        assert!(a > 1);
        assert_eq!(i.ino_for(&k("textures/rock.DDS")), a);
        assert_eq!(i.key_of(a), Some(&k("textures/rock.dds")));
        assert_ne!(i.ino_for(&k("textures")), a);
        assert_eq!(i.key_of(999), None);
    }

    #[test]
    fn rekey_moves_the_subtree_keeping_numbers() {
        let mut i = Inodes::new();
        let d = i.ino_for(&k("a"));
        let f = i.ino_for(&k("a/x/f.txt"));
        let other = i.ino_for(&k("ab/f.txt"));
        i.rekey_prefix(&k("a"), &k("b"));
        assert_eq!(i.key_of(d), Some(&k("b")));
        assert_eq!(i.key_of(f), Some(&k("b/x/f.txt")));
        assert_eq!(i.key_of(other), Some(&k("ab/f.txt")));
        assert_eq!(i.ino_for(&k("b/x/f.txt")), f);
        assert_ne!(i.ino_for(&k("a")), d);
    }

    #[test]
    fn rekey_keeps_replaced_target_as_alias() {
        let mut i = Inodes::new();
        let a = i.ino_for(&k("a"));
        let b = i.ino_for(&k("b"));
        let bf = i.ino_for(&k("b/f"));
        i.rekey_prefix(&k("a"), &k("b"));
        assert_eq!(i.key_of(a), Some(&k("b")));
        // Still resolves to the path, but the path's number is the moved entry's.
        assert_eq!(i.key_of(b), Some(&k("b")));
        assert_eq!(i.key_of(bf), Some(&k("b/f")));
        assert_eq!(i.ino_for(&k("b")), a);
    }

    #[test]
    fn known_lists_everything() {
        let mut i = Inodes::new();
        i.ino_for(&k("x"));
        assert_eq!(i.known().count(), 2);
    }
}
