# Tests of the basic_games plugin on Linux (Phase 4d.2): the installed package is
# imported with the installed mobase, in a HOME with a fake Steam (never the user's).
import sys
from pathlib import Path

import pytest
from PyQt6.QtWidgets import QApplication

_app = None


def pytest_configure():
    global _app
    _app = QApplication(sys.argv)


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


GAMES = {
    # app ID: (install dir, binary relative to it, with a Proton prefix)
    "524220": ("NieRAutomata", "NieRAutomata.exe", True),
    "1091500": ("Cyberpunk 2077", "bin/x64/Cyberpunk2077.exe", True),
    "1086940": ("Baldurs Gate 3", "bin/bg3.exe", True),
    "3489700": ("StellarBlade", "SB/Binaries/Win64/SB-Win64-Shipping.exe", True),
}


@pytest.fixture
def steam(tmp_path, monkeypatch):
    home = tmp_path / "home"
    root = home / ".local/share/Steam"
    _write(
        root / "steamapps/libraryfolders.vdf",
        '"libraryfolders"\n{\n' f'\t"0"\n\t{{\n\t\t"path"\t\t"{root}"\n\t}}\n' "}\n",
    )
    for appid, (installdir, binary, prefix) in GAMES.items():
        _write(
            root / f"steamapps/appmanifest_{appid}.acf",
            f'"AppState"\n{{\n\t"appid"\t\t"{appid}"\n'
            f'\t"installdir"\t\t"{installdir}"\n}}\n',
        )
        _write(root / "steamapps/common" / installdir / binary, "")
        if appid == "3489700":
            _write(root / "steamapps/common" / installdir / "SB.exe", "")
        compat = root / "steamapps/compatdata" / appid
        compat.mkdir(parents=True)
        if prefix:
            (compat / "pfx/drive_c/users/steamuser").mkdir(parents=True)
    monkeypatch.setenv("HOME", str(home))
    monkeypatch.setenv("XDG_DATA_HOME", str(home / ".local/share"))
    monkeypatch.setenv("XDG_DOCUMENTS_DIR", str(home / "Documents"))
    return root
