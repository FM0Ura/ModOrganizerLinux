# Tests of the UE4SS mods.txt / mods.json generated in the profile (Phase 6a, spec 3.2).
import json
import sys
from pathlib import Path
from unittest.mock import MagicMock

from test_detection import _load_basic_games

_MODS = "SB/Binaries/Win64/ue4ss/Mods"
_DEFAULT_LINES = [
    "CheatManagerEnablerMod : 1",
    "ActorDumperMod : 0",
    "ConsoleCommandsMod : 1",
    "ConsoleEnablerMod : 1",
    "SplitScreenMod : 0",
    "LineTraceMod : 1",
    "BPModLoaderMod : 1",
    "BPML_GenericFunctions : 1",
    "jsbLuaProfilerMod : 0",
]


def _write():
    module = sys.modules[type(_load_basic_games()["StellarBladeGame"]).__module__]
    return module.write_ue4ss_mod_lists


def test_mod_lists_have_defaults_extras_and_keybinds_last(tmp_path):
    a = tmp_path / "mods/CNS"
    (a / _MODS / "DekCNS/Scripts").mkdir(parents=True)
    b = tmp_path / "mods/Jiggle"
    (b / _MODS / "JiggleUpdate/Scripts").mkdir(parents=True)
    (b / _MODS / "shared").mkdir()
    _write()(tmp_path / "profile", [a, b])
    lines = (tmp_path / "profile/mods.txt").read_text().splitlines()
    assert lines[:9] == _DEFAULT_LINES
    assert lines[9:] == ["DekCNS : 1", "JiggleUpdate : 1", "Keybinds : 1"]
    data = json.loads((tmp_path / "profile/mods.json").read_text())
    assert data[-1] == {"mod_name": "Keybinds", "mod_enabled": True}
    assert {"mod_name": "ActorDumperMod", "mod_enabled": False} in data


def test_duplicate_ue4ss_mod_dirs_listed_once(tmp_path):
    a = tmp_path / "mods/A"
    (a / _MODS / "JiggleUpdate").mkdir(parents=True)
    b = tmp_path / "mods/B"
    (b / _MODS / "JiggleUpdate").mkdir(parents=True)
    c = tmp_path / "mods/C"
    (c / _MODS / "jiggleupdate").mkdir(parents=True)
    _write()(tmp_path / "profile", [a, b, c])
    lines = (tmp_path / "profile/mods.txt").read_text().splitlines()
    assert lines[9:] == ["JiggleUpdate : 1", "Keybinds : 1"]


def test_default_mod_dirs_are_not_repeated(tmp_path):
    ue4ss = tmp_path / "mods/UE4SS"
    (ue4ss / _MODS / "BPModLoaderMod").mkdir(parents=True)
    (ue4ss / _MODS / "Keybinds").mkdir()
    _write()(tmp_path / "profile", [ue4ss])
    lines = (tmp_path / "profile/mods.txt").read_text().splitlines()
    assert lines == _DEFAULT_LINES + ["Keybinds : 1"]
    assert lines.count("BPModLoaderMod : 1") == 1


def test_no_ue4ss_mods_writes_defaults_only(tmp_path):
    plain = tmp_path / "mods/Plain"
    plain.mkdir(parents=True)
    _write()(tmp_path / "profile", [plain, tmp_path / "mods/Missing"])
    lines = (tmp_path / "profile/mods.txt").read_text().splitlines()
    assert lines == _DEFAULT_LINES + ["Keybinds : 1"]
    assert sorted(p.name for p in tmp_path.iterdir()) == ["mods", "profile"]
    assert not (tmp_path / "mods/Missing").exists()


def test_mappings_point_profile_files_to_ue4ss_mods(steam, tmp_path):
    game = _load_basic_games()["StellarBladeGame"]
    game.detectGame()
    organizer = MagicMock()
    organizer.profilePath.return_value = str(tmp_path / "profile")
    game.init(organizer)
    mappings = game.mappings()
    assert len(mappings) == 2
    game_dir = steam / "steamapps/common/StellarBlade"
    for mapping, name in zip(mappings, ["mods.txt", "mods.json"]):
        assert Path(mapping.source) == tmp_path / "profile" / name
        assert Path(mapping.destination) == game_dir / _MODS / name
        assert mapping.isDirectory is False
