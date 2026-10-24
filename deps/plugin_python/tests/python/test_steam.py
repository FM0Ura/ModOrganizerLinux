import os
import sys
from pathlib import Path

import pytest

import mobase

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux only")


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


@pytest.fixture
def steam(tmp_path, monkeypatch):
    """A HOME with the native Steam and a second library, with three games:
    524220 (with a Proton prefix), 1091500 (in the second library, with a prefix)
    and 1086940 (no prefix)."""
    home = tmp_path / "home"
    root = home / ".local/share/Steam"
    second = tmp_path / "Games/SteamLibrary"
    _write(
        root / "steamapps/libraryfolders.vdf",
        '"libraryfolders"\n{\n'
        f'\t"0"\n\t{{\n\t\t"path"\t\t"{root}"\n\t}}\n'
        f'\t"1"\n\t{{\n\t\t"path"\t\t"{second}"\n\t}}\n'
        "}\n",
    )
    for lib, appid, installdir, prefix in [
        (root, "524220", "NieRAutomata", True),
        (second, "1091500", "Cyberpunk 2077", True),
        (root, "1086940", "Baldurs Gate 3", False),
    ]:
        _write(
            lib / f"steamapps/appmanifest_{appid}.acf",
            f'"AppState"\n{{\n\t"appid"\t\t"{appid}"\n'
            f'\t"installdir"\t\t"{installdir}"\n}}\n',
        )
        (lib / "steamapps/common" / installdir).mkdir(parents=True)
        compat = lib / "steamapps/compatdata" / appid
        compat.mkdir(parents=True)
        if prefix:
            (compat / "pfx/drive_c/users/steamuser").mkdir(parents=True)
    monkeypatch.setenv("HOME", str(home))
    monkeypatch.setenv("XDG_DATA_HOME", str(home / ".local/share"))
    return root, second


def test_installed_game_with_prefix(steam):
    root, _ = steam
    app = mobase.utils.steamApp("524220")
    assert app is not None
    assert app["appId"] == "524220"
    assert Path(app["gamePath"]) == root / "steamapps/common/NieRAutomata"
    assert Path(app["compatData"]) == root / "steamapps/compatdata/524220"


def test_game_in_another_library(steam):
    _, second = steam
    app = mobase.utils.steamApp("1091500")
    assert app is not None
    assert Path(app["library"]) == second
    assert Path(app["compatData"]) == second / "steamapps/compatdata/1091500"


def test_game_without_prefix(steam):
    app = mobase.utils.steamApp("1086940")
    assert app is not None
    assert app["compatData"] == ""


def test_missing_game(steam):
    assert mobase.utils.steamApp("999999") is None
