import importlib.util
import os
import shutil
import stat
import sys
from pathlib import Path

import pytest

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux only")

import mo2_windows_compat  # noqa: E402

mo2_windows_compat.install()

# the operations of a plugin, run from a file of the plugin so that the plugin is the
# caller, as with a plugin of Windows that calls os, open, shutil or pathlib
OPS = '''
import glob as _glob
import os
from pathlib import Path


def read(path):
    with open(path) as f:
        return f.read()


def write(path, text):
    with open(path, "w") as f:
        f.write(text)


def remove(path):
    os.remove(path)


def exists(path):
    return os.path.exists(path)


def path_exists(path):
    return Path(path).exists()


def makedirs(path):
    os.makedirs(path, exist_ok=True)


def chmod(path, mode):
    os.chmod(path, mode)


def hardlink(source, target):
    Path(target).hardlink_to(source)


def glob(pattern, recursive=False):
    return _glob.glob(pattern, recursive=recursive)
'''


def _load(folder: Path, name: str):
    folder.mkdir()
    (folder / "ops.py").write_text(OPS)
    spec = importlib.util.spec_from_file_location(name, folder / "ops.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def tree(tmp_path):
    """A game folder with Bin/Vanilla.DLL and Bin/x64/, a plugin of Windows (win,
    registered as a root) and the same plugin outside of any root (linux)."""
    game = tmp_path / "game"
    (game / "Bin/x64").mkdir(parents=True)
    (game / "Bin/Vanilla.DLL").write_text("vanilla")
    win = _load(tmp_path / "winplugin", f"winops_{tmp_path.name}")
    linux = _load(tmp_path / "linuxplugin", f"linuxops_{tmp_path.name}")
    mo2_windows_compat.add_root(str(tmp_path / "winplugin"))
    return game, win, linux


def test_flags_exist():
    assert os.O_BINARY == 0
    assert os.O_TEXT == 0


def test_read_ignores_case(tree):
    game, win, linux = tree
    assert win.read(str(game / "bin/vanilla.dll")) == "vanilla"
    with pytest.raises(FileNotFoundError):
        linux.read(str(game / "bin/vanilla.dll"))


def test_remove_ignores_case(tree):
    game, win, linux = tree
    win.remove(str(game / "bin/vanilla.dll"))
    assert not (game / "Bin/Vanilla.DLL").exists()


def test_exists_and_pathlib(tree):
    game, win, linux = tree
    assert win.exists(str(game / "BIN/VANILLA.dll")) is True
    assert win.path_exists(str(game / "BIN/VANILLA.dll")) is True
    assert linux.exists(str(game / "BIN/VANILLA.dll")) is False


def test_create_uses_existing_folders(tree):
    game, win, linux = tree
    win.makedirs(str(game / "bin/x64/new"))
    assert (game / "Bin/x64/new").is_dir()
    assert sorted(os.listdir(game)) == ["Bin"]


def test_overwrite_with_other_case(tree):
    game, win, linux = tree
    win.write(str(game / "bin/vanilla.dll"), "mod")
    assert (game / "Bin/Vanilla.DLL").read_text() == "mod"
    assert sorted(os.listdir(game / "Bin")) == ["Vanilla.DLL", "x64"]


def test_link_destination_is_resolved(tree, tmp_path):
    game, win, linux = tree
    source = tmp_path / "mod.dll"
    source.write_text("mod")
    win.remove(str(game / "bin/vanilla.dll"))
    win.hardlink(str(source), str(game / "bin/vanilla.dll"))
    # the old file was removed: the link has the name it was given, in the existing
    # folder
    assert (game / "Bin/vanilla.dll").stat().st_ino == source.stat().st_ino
    assert sorted(os.listdir(game)) == ["Bin"]


def test_chmod_windows_meaning(tree):
    game, win, linux = tree
    f = game / "Bin/Vanilla.DLL"
    os.chmod(f, 0o644)
    win.chmod(str(f), stat.S_IWRITE)
    assert stat.S_IMODE(f.stat().st_mode) == 0o644
    win.chmod(str(f), stat.S_IREAD)
    assert stat.S_IMODE(f.stat().st_mode) == 0o400
    os.chmod(f, 0o644)
    linux.chmod(str(f), stat.S_IWRITE)
    assert stat.S_IMODE(f.stat().st_mode) == 0o200
    os.chmod(f, 0o644)


def test_chmod_without_write_makes_read_only(tree):
    game, win, linux = tree
    f = game / "Bin/Vanilla.DLL"
    os.chmod(f, 0o644)
    win.chmod(str(f), 0)
    assert stat.S_IMODE(f.stat().st_mode) == 0o444
    os.chmod(f, 0o644)


def test_backslashes(tree):
    game, win, linux = tree
    assert win.read(str(game) + "\\Bin\\Vanilla.DLL") == "vanilla"


def test_glob_backslashes(tree):
    game, win, linux = tree
    found = win.glob(str(game) + "/**\\*.DLL", recursive=True)
    assert str(game / "Bin/Vanilla.DLL") in found


def test_ambiguous_names_are_not_resolved(tree):
    game, win, linux = tree
    (game / "a.txt").write_text("lower")
    (game / "A.txt").write_text("upper")
    with pytest.raises(FileNotFoundError):
        win.read(str(game / "a.TXT"))


def test_new_file_keeps_its_name(tree):
    game, win, linux = tree
    win.write(str(game / "New.TXT"), "x")
    assert (game / "New.TXT").read_text() == "x"


def test_supports_sets_keep_wrapped_functions(tmp_path):
    a = tmp_path / "a"
    b = tmp_path / "b"
    a.write_text("a")
    b.write_text("b")
    original = mo2_windows_compat._ORIGINALS["chmod"]
    if original in mo2_windows_compat._ORIGINAL_SUPPORTS["follow_symlinks"]:
        assert os.chmod in os.supports_follow_symlinks
    shutil.copystat(a, b, follow_symlinks=False)


def test_install_is_idempotent(tmp_path):
    stat_before = os.stat
    mo2_windows_compat.install()
    assert os.stat is stat_before
    assert os.stat(tmp_path).st_mode
