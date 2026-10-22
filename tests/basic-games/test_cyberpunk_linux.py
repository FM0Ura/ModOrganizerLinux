from pathlib import Path
from unittest.mock import MagicMock

import pytest
from PyQt6.QtCore import QDir
from test_detection import _load_basic_games


@pytest.fixture
def cyberpunk(steam, tmp_path, monkeypatch):
    game = _load_basic_games()["Cyberpunk2077Game"]
    game.detectGame()
    modlist = tmp_path / "overwrite/r6/cache/modded/MO_REDmod_load_order.txt"
    game._organizer = MagicMock()
    game._organizer.waitForApplication.return_value = (True, 0)
    game._modlist_files = MagicMock()
    game._modlist_files.modfile_names.return_value = iter(["mod_a", "mod_b"])
    game._modlist_files.update_modlist.return_value = (
        modlist,
        ["mod_a", "mod_b"],
        ["mod_a", "mod_b"],
    )
    monkeypatch.setattr(game, "_get_setting", lambda key: True)
    monkeypatch.setattr(game, "isActive", lambda: True)
    return game, modlist


def _started(game):
    (app, args, wd), _ = game._organizer.startApplication.call_args
    return app, args, wd


def test_the_deploy_modlist_is_a_windows_path_for_redmod(cyberpunk):
    game, modlist = cyberpunk
    assert game._deploy_redmod() == (True, 0)
    _, args, _ = _started(game)
    expected = "Z:" + str(modlist).replace("/", "\\")
    assert f'-modlist="{expected}"' in args[0]
    assert "/" not in args[0].split("-modlist=")[1]


def test_the_manual_deploy_modlist_is_a_windows_path_for_redmod(cyberpunk):
    game, modlist = cyberpunk
    redmod = game._get_redmod_binary()
    game._onAboutToRun(
        str(redmod), QDir(str(redmod.parent)), "deploy -reportProgress %modlist%"
    )
    _, args, _ = _started(game)
    assert args[0] == (
        'deploy -reportProgress -modlist="Z:' + str(modlist).replace("/", "\\") + '"'
    )


def test_the_redmod_binary_and_the_directory_stay_linux_paths(cyberpunk):
    # MO2 converts the program and the working directory itself
    game, _ = cyberpunk
    game._deploy_redmod()
    app, _, wd = _started(game)
    assert Path(app).is_absolute() and "\\" not in str(app)
    assert "\\" not in str(wd)
