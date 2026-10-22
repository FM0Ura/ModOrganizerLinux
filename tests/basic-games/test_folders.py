# The Windows user folders of a Steam game are in its Proton prefix (Phase 4d.2,
# spec section 3.2); without a prefix the XDG folders and a warning.
import shutil
from pathlib import Path

import pytest
from PyQt6.QtCore import qInstallMessageHandler

from test_detection import _load_basic_games


def _user(steam: Path, appid: str) -> Path:
    return steam / "steamapps/compatdata" / appid / "pfx/drive_c/users/steamuser"


@pytest.fixture
def qt_messages():
    # Qt may log to the journal instead of stderr: capture with a handler
    messages = []
    previous = qInstallMessageHandler(lambda mode, ctx, msg: messages.append(msg))
    yield messages
    qInstallMessageHandler(previous)


def _reset_warnings():
    from basic_games import proton_utils

    proton_utils._warned.clear()


def test_cyberpunk_folders_in_its_prefix(steam):
    game = _load_basic_games()["Cyberpunk2077Game"]
    game.detectGame()
    user = _user(steam, "1091500")
    assert Path(game.documentsDirectory().absolutePath()) == (
        user / "AppData/Local/CD Projekt Red/Cyberpunk 2077"
    )
    assert Path(game.savesDirectory().absolutePath()) == (
        user / "Saved Games/CD Projekt Red/Cyberpunk 2077"
    )


def test_bg3_folders_in_its_prefix(steam):
    game = _load_basic_games()["BG3Game"]
    game.detectGame()
    user = _user(steam, "1086940")
    docs = user / "AppData/Local/Larian Studios/Baldur's Gate 3"
    assert Path(game.documentsDirectory().absolutePath()) == docs
    assert Path(game.savesDirectory().absolutePath()) == (
        docs / "PlayerProfiles/Public/Savegames/Story"
    )


def test_documents_variable_and_default_folder_in_its_prefix(steam):
    from basic_games.basic_game import BasicGame, replace_variables

    game = _load_basic_games()["NierAutomataGame"]
    game.detectGame()
    user = _user(steam, "524220")
    assert replace_variables("%DOCUMENTS%/x", game) == f"{user}/Documents/x"
    assert replace_variables("%USERPROFILE%/x", game) == f"{user}/x"

    # the default documents folder is looked up inside the prefix
    docs = user / "Documents/My Games" / game.gameName()
    docs.mkdir(parents=True)
    assert Path(game.documentsDirectory().absolutePath()) == docs


def test_the_prefix_of_the_matched_steam_id(steam):
    from basic_games.basic_game import BasicGame, replace_variables

    class TwoIdsGame(BasicGame):
        Name = "Two IDs"
        Author = "test"
        Version = "1.0.0"
        GameName = "Two IDs"
        GameShortName = "twoids"
        GameSteamId = ["111", "524220"]
        GameBinary = "NieRAutomata.exe"
        GameDataPath = ""

    _load_basic_games()
    game = TwoIdsGame()
    game.detectGame()
    assert game.steamAPPId() == "524220"
    assert replace_variables("%USERPROFILE%", game) == str(_user(steam, "524220"))


def test_without_prefix_the_xdg_folders_and_a_warning(steam, qt_messages):
    _reset_warnings()
    shutil.rmtree(steam / "steamapps/compatdata/1091500/pfx")
    game = _load_basic_games()["Cyberpunk2077Game"]
    game.detectGame()
    docs = Path(game.documentsDirectory().absolutePath())
    assert "compatdata" not in str(docs)
    assert str(docs).startswith(str(Path.home()))
    game.savesDirectory()
    # once per game, even though the folders were asked for several times
    warnings = [m for m in qt_messages if "no Proton prefix" in m]
    assert len(warnings) == 1
    assert "Cyberpunk 2077" in warnings[0]


def test_a_game_without_steam_ids_has_no_warning(steam, qt_messages):
    from basic_games.basic_game import BasicGame

    class NoSteamGame(BasicGame):
        Name = "No Steam"
        Author = "test"
        Version = "1.0.0"
        GameName = "No Steam"
        GameShortName = "nosteam"
        GameBinary = "x.exe"
        GameDataPath = ""

    _reset_warnings()
    game = NoSteamGame()
    docs = Path(game.documentsDirectory().absolutePath())
    assert "compatdata" not in str(docs)
    assert not [m for m in qt_messages if "no Proton prefix" in m]
