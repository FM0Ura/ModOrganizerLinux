from pathlib import Path

from test_detection import _load_basic_games


def test_stellar_blade_is_detected(steam):
    game = _load_basic_games()["StellarBladeGame"]
    game.detectGame()
    assert game.isInstalled()
    assert (
        Path(game.gameDirectory().absolutePath())
        == steam / "steamapps/common/StellarBlade"
    )
    assert game.steamAPPId() == "3489700"
    assert game.gameNexusName() == "stellarblade"
    assert game.binaryName() == "SB/Binaries/Win64/SB-Win64-Shipping.exe"
    assert (
        Path(game.dataDirectory().absolutePath())
        == steam / "steamapps/common/StellarBlade"
    )


def test_stellar_blade_folders_are_in_the_prefix(steam):
    game = _load_basic_games()["StellarBladeGame"]
    game.detectGame()
    user = steam / "steamapps/compatdata/3489700/pfx/drive_c/users/steamuser"
    assert (
        Path(game.savesDirectory().absolutePath())
        == user / "AppData/Local/SB/Saved/SaveGames"
    )
    assert (
        Path(game.documentsDirectory().absolutePath())
        == user / "AppData/Local/SB/Saved/Config/WindowsNoEditor"
    )
    assert game.iniFiles() == [
        "Engine.ini",
        "Game.ini",
        "Input.ini",
        "GameUserSettings.ini",
    ]
    assert game._mappings.savegameExtension.get() == "sav"


def test_stellar_blade_ue4ss_dirs(steam):
    game = _load_basic_games()["StellarBladeGame"]
    game.detectGame()
    root = steam / "steamapps/common/StellarBlade"
    assert Path(game.paksDirectory().absolutePath()) == root / "SB/Content/Paks"
    assert (
        Path(game.ue4ssModsDirectory().absolutePath())
        == root / "SB/Binaries/Win64/ue4ss/Mods"
    )
