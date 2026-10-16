//! Case-insensitive name keys.

use std::borrow::Borrow;
use std::ffi::OsStr;
use std::os::unix::ffi::OsStrExt;
use std::path::{Component, Path};

/// Folded (case-insensitive) form of a file name or relative path.
#[derive(Clone, PartialEq, Eq, Hash, Debug, PartialOrd, Ord)]
pub struct Key(pub(crate) Vec<u8>);

/// Lets maps keyed by `Key` be searched with a borrowed byte slice (no allocation). The
/// ordering and hash of `Key` are those of its bytes, so this is consistent.
impl Borrow<[u8]> for Key {
    fn borrow(&self) -> &[u8] {
        &self.0
    }
}

/// Folds one name. Valid UTF-8 is lowercased char by char (simple folding); other names
/// only have ASCII bytes lowercased.
pub fn fold(name: &OsStr) -> Key {
    let mut out = Vec::new();
    fold_into(name.as_bytes(), &mut out);
    Key(out)
}

/// Appends the folded form of the name `bytes` (see `fold`) to `out`.
pub(crate) fn fold_into(bytes: &[u8], out: &mut Vec<u8>) {
    // ASCII is the common case; simple folding of ASCII is ASCII lowercasing.
    if bytes.is_ascii() {
        out.extend(bytes.iter().map(u8::to_ascii_lowercase));
        return;
    }
    match std::str::from_utf8(bytes) {
        Ok(s) => {
            let mut buf = [0u8; 4];
            for c in s.chars().flat_map(char::to_lowercase) {
                out.extend_from_slice(c.encode_utf8(&mut buf).as_bytes());
            }
        }
        Err(_) => out.extend(bytes.iter().map(u8::to_ascii_lowercase)),
    }
}

/// Folds each normal component of `rel`, joined by `/`. The empty path is the root.
pub fn fold_path(rel: &Path) -> Key {
    let mut out = Vec::new();
    for c in rel.components() {
        if let Component::Normal(n) = c {
            if !out.is_empty() {
                out.push(b'/');
            }
            out.extend_from_slice(&fold(n).0);
        }
    }
    Key(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fold_is_case_insensitive() {
        assert_eq!(fold(OsStr::new("Textures")), fold(OsStr::new("TEXTURES")));
        assert_eq!(fold(OsStr::new("Ünïcödé")), fold(OsStr::new("üNÏCÖDÉ")));
        assert_ne!(fold(OsStr::new("a")), fold(OsStr::new("b")));
    }

    #[test]
    fn fold_is_not_context_sensitive() {
        assert_eq!(fold(OsStr::new("ΟΔΥΣΣΕΥΣ")), fold(OsStr::new("οδυσσευσ")));
    }

    #[test]
    fn fold_non_utf8_lowercases_ascii_bytes_only() {
        let a = OsStr::from_bytes(b"AB\xff");
        let b = OsStr::from_bytes(b"ab\xff");
        assert_eq!(fold(a), fold(b));
    }

    #[test]
    fn fold_path_joins_components() {
        assert_eq!(
            fold_path(Path::new("Textures/Rock.DDS")),
            fold_path(Path::new("textures/rock.dds"))
        );
        assert_eq!(fold_path(Path::new("")).0, b"".to_vec());
    }
}
