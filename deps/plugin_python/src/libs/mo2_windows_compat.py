"""Compatibility of Python plugins written for Windows (Linux only).

Plugins written for the Windows version of Mod Organizer assume Windows: a file system
that ignores case, the open flags of Windows (os.O_BINARY) and os.chmod() that only sets
or clears the read-only flag. Mod Organizer installs this module once (install(), from
the runner) and registers each plugin that is not shipped with it (add_root(), from the
proxy): a plugin of the user's folder.

- os.O_BINARY and the other open flags of Windows exist, with the value 0 (for the
  whole process: an attribute cannot be scoped, and 0 changes no open);
- for the calls made by a registered plugin (the innermost frame outside of the
  standard library is in one of the roots), the shipped plugins and Mod Organizer keep
  the behavior of Linux, even when a plugin of Windows calls them back:
  - a path is looked up ignoring case, as Wine does: a component that does not exist
    is replaced by the only item of its folder with the same name ignoring case (none
    or several: the path is left as it was); reading or removing tries again with the
    resolved path after FileNotFoundError, creating resolves first, so that a file is
    written in the existing folder, whatever the case of its name;
  - backslashes are separators;
  - os.chmod() with a mode that would make the file unreadable by its owner (e.g.,
    stat.S_IWRITE alone) or made only of stat.S_IREAD and stat.S_IWRITE has the meaning
    of Windows: with S_IWRITE, the file is made writable, without it, read-only; the
    other bits are kept.
"""

import builtins
import glob
import io
import os
import stat
import sys
import sysconfig

_FLAGS = (
    "O_BINARY",
    "O_TEXT",
    "O_NOINHERIT",
    "O_SHORT_LIVED",
    "O_TEMPORARY",
    "O_RANDOM",
    "O_SEQUENTIAL",
)

# functions taking a path that is read or removed: the positions of their paths
_READ = {
    "stat": (0,),
    "lstat": (0,),
    "remove": (0,),
    "unlink": (0,),
    "rmdir": (0,),
    "listdir": (0,),
    "scandir": (0,),
    "utime": (0,),
    "truncate": (0,),
    "readlink": (0,),
    "chown": (0,),
    "lchown": (0,),
    # shutil.copy2() copies the extended attributes (SELinux labels) to its target
    "listxattr": (0,),
    "getxattr": (0,),
    "setxattr": (0,),
    "removexattr": (0,),
}

# functions creating something: the positions of all their paths, all resolved first
_CREATE = {
    "mkdir": (0,),
    "rename": (0, 1),
    "replace": (0, 1),
    "link": (0, 1),
    "symlink": (1,),
}

_SUPPORTS = ("fd", "dir_fd", "follow_symlinks")

# the functions replaced by install(), by name, and, for each os.supports_* set, the
# originals that were in it
_ORIGINALS = {}
_ORIGINAL_SUPPORTS = {name: set() for name in _SUPPORTS}

_roots = []
_callers = {}
_installed = False
_THIS_FILE = os.path.abspath(__file__)

# the frames of these folders (the standard library) are skipped: the plugin that
# called shutil or pathlib is the caller
_STDLIB = tuple(
    {
        os.path.normpath(sysconfig.get_paths()[name]) + os.sep
        for name in ("stdlib", "platstdlib")
    }
)


def add_root(path):
    """Registers a plugin of Windows: its folder, or its file for a single-file plugin."""
    root = os.path.normpath(os.path.abspath(path))
    if root not in _roots:
        _roots.append(root)
        _callers.clear()


def _in_roots(filename):
    known = _callers.get(filename)
    if known is None:
        known = any(
            filename == root or filename.startswith(root + os.sep) for root in _roots
        )
        _callers[filename] = known
    return known


def from_windows_plugin():
    """True if the caller is a plugin of Windows: the innermost frame that is neither in
    this module nor in the standard library is in one of the roots (code of Mod Organizer
    or of a shipped plugin called back from a plugin of Windows is not)."""
    if not _roots:
        return False
    frame = sys._getframe(1)
    while frame is not None:
        filename = frame.f_code.co_filename
        if not (
            filename == _THIS_FILE
            or filename.startswith(_STDLIB)
            or filename.startswith("<frozen ")
        ):
            return _in_roots(filename)
        frame = frame.f_back
    return False


def _lexists(path):
    try:
        _ORIGINALS.get("lstat", os.lstat)(path)
        return True
    except (OSError, ValueError):
        return False


def resolve(path):
    """The path with backslashes as separators and each component that does not exist
    replaced by the only item of its folder with the same name ignoring case."""
    path = path.replace("\\", "/")
    listdir = _ORIGINALS.get("listdir", os.listdir)
    parts = path.split("/")
    current = "/" if path.startswith("/") else ""
    for index, part in enumerate(parts):
        if part in ("", "."):
            continue
        candidate = current + part if current in ("", "/") else current + "/" + part
        if part == ".." or _lexists(candidate):
            current = candidate
            continue
        try:
            names = listdir(current or ".")
        except OSError:
            names = []
        wanted = part.casefold()
        matches = [name for name in names if name.casefold() == wanted]
        if len(matches) != 1:
            rest = "/".join(p for p in parts[index:] if p not in ("", "."))
            return current + rest if current in ("", "/") else current + "/" + rest
        current = current + matches[0] if current in ("", "/") else current + "/" + matches[0]
    if path.endswith("/") and not current.endswith("/"):
        current += "/"
    return current or path


def _text(value):
    """The path as a str, or None for what is not a path of text (fd, bytes)."""
    if isinstance(value, str):
        return value
    if isinstance(value, os.PathLike):
        value = os.fspath(value)
        if isinstance(value, str):
            return value
    return None


def _backslashes(args, positions):
    """The arguments with backslashes as separators, when a plugin of Windows calls."""
    if not any(
        (text := _text(args[i])) is not None and "\\" in text
        for i in positions
        if i < len(args)
    ):
        return args
    if not from_windows_plugin():
        return args
    args = list(args)
    for i in positions:
        if i < len(args) and (text := _text(args[i])) is not None:
            args[i] = text.replace("\\", "/")
    return tuple(args)


def _resolved(args, positions):
    """The arguments with their paths resolved, None if no path changed."""
    changed = False
    args = list(args)
    for i in positions:
        if i < len(args) and (text := _text(args[i])) is not None:
            new = resolve(text)
            if new != text:
                args[i] = new
                changed = True
    return tuple(args) if changed else None


def _read_wrapper(original, positions):
    def wrapper(*args, **kwargs):
        if kwargs.get("dir_fd") is not None:
            return original(*args, **kwargs)
        args = _backslashes(args, positions)
        try:
            return original(*args, **kwargs)
        except (FileNotFoundError, NotADirectoryError):
            if not from_windows_plugin():
                raise
            new = _resolved(args, positions)
            if new is None:
                raise
            return original(*new, **kwargs)

    return wrapper


def _create_wrapper(original, positions):
    def wrapper(*args, **kwargs):
        if (
            kwargs.get("dir_fd") is not None
            or kwargs.get("src_dir_fd") is not None
            or kwargs.get("dst_dir_fd") is not None
        ):
            return original(*args, **kwargs)
        if from_windows_plugin():
            args = _backslashes(args, positions)
            args = _resolved(args, positions) or args
        return original(*args, **kwargs)

    return wrapper


def _chmod_wrapper(original):
    read = _read_wrapper(original, (0,))

    def chmod(path, mode, *args, **kwargs):
        if (
            isinstance(mode, int)
            and (not mode & stat.S_IRUSR or not mode & ~(stat.S_IREAD | stat.S_IWRITE))
            and kwargs.get("dir_fd") is None
            and from_windows_plugin()
        ):
            target = path
            text = _text(path)
            if text is not None:
                text = text.replace("\\", "/")
                target = text if _lexists(text) else resolve(text)
            current = stat.S_IMODE(_ORIGINALS["stat"](target).st_mode)
            if mode & stat.S_IWRITE:
                mode = current | stat.S_IWUSR
            else:
                mode = current & ~(stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH)
            return original(target, mode, *args, **kwargs)
        return read(path, mode, *args, **kwargs)

    return chmod


def _access_wrapper(original):
    # os.access() returns False for a missing path instead of raising
    def access(path, mode, *args, **kwargs):
        if kwargs.get("dir_fd") is None:
            (path,) = _backslashes((path,), (0,))
        if original(path, mode, *args, **kwargs):
            return True
        if kwargs.get("dir_fd") is not None or not from_windows_plugin():
            return False
        new = _resolved((path,), (0,))
        return new is not None and original(new[0], mode, *args, **kwargs)

    return access


def _creates(mode):
    return any(c in mode for c in "wax+")


def _open_wrapper(original):
    read = _read_wrapper(original, (0,))
    create = _create_wrapper(original, (0,))

    def open(file, *args, **kwargs):
        mode = args[0] if args else kwargs.get("mode", "r")
        if isinstance(mode, str) and _creates(mode):
            return create(file, *args, **kwargs)
        return read(file, *args, **kwargs)

    return open


def _os_open_wrapper(original):
    read = _read_wrapper(original, (0,))
    create = _create_wrapper(original, (0,))

    def open(path, flags, *args, **kwargs):
        if isinstance(flags, int) and flags & os.O_CREAT:
            return create(path, flags, *args, **kwargs)
        return read(path, flags, *args, **kwargs)

    return open


def _glob_wrapper(original):
    def wrapper(pathname, *args, **kwargs):
        if isinstance(pathname, str) and "\\" in pathname and from_windows_plugin():
            pathname = pathname.replace("\\", "/")
        return original(pathname, *args, **kwargs)

    return wrapper


def _replace_os(name, wrapper):
    original = getattr(os, name)
    _ORIGINALS[name] = original
    for support in _SUPPORTS:
        functions = getattr(os, "supports_" + support)
        if original in functions:
            _ORIGINAL_SUPPORTS[support].add(original)
            functions.add(wrapper)
    setattr(os, name, wrapper)


def install():
    """Installs the compatibility (once)."""
    global _installed
    if _installed:
        return
    _installed = True

    for flag in _FLAGS:
        if not hasattr(os, flag):
            setattr(os, flag, 0)

    for name, positions in _READ.items():
        if hasattr(os, name):
            _replace_os(name, _read_wrapper(getattr(os, name), positions))
    for name, positions in _CREATE.items():
        _replace_os(name, _create_wrapper(getattr(os, name), positions))
    _replace_os("chmod", _chmod_wrapper(os.chmod))
    _replace_os("access", _access_wrapper(os.access))
    _replace_os("open", _os_open_wrapper(os.open))

    _ORIGINALS["builtins.open"] = builtins.open
    wrapped_open = _open_wrapper(builtins.open)
    builtins.open = wrapped_open
    io.open = wrapped_open

    _ORIGINALS["glob.glob"] = glob.glob
    _ORIGINALS["glob.iglob"] = glob.iglob
    glob.glob = _glob_wrapper(glob.glob)
    glob.iglob = _glob_wrapper(glob.iglob)
