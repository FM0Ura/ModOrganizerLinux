//! mo2-vfs: FUSE virtual file system of the Mod Organizer 2 Linux port.
//!
//! Planned modules:
//! - `mapping`: parses the mapping list and groups destinations by mount root.
//! - `tree`: virtual tree built from the layers; case-insensitive lookup, exclusion masks.
//! - `fs`: the `fuser::Filesystem` implementation (inode, node, real file).
//! - `session`: one mount per root; opens the original layer fd before mounting; state file.
//! - `ipc`: line-delimited JSON protocol over stdin/stdout.
//! - `bin/mo2-vfs.rs`: arguments, logging, signals and the `ipc` loop.

pub mod layer;
pub mod mapping;
pub mod name;
pub mod ops;
pub mod tree;
