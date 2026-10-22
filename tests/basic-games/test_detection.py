import importlib
from pathlib import Path


def _load_basic_games():
    import basic_games
    from basic_games.basic_game import BasicGame

    importlib.reload(basic_games.steam_utils)
    BasicGame.steam_games = basic_games.steam_utils.find_games()
    return {type(g).__name__: g for g in basic_games.createPlugins()}


def _load_basic_games_steam():
    from basic_games import steam_utils

    importlib.reload(steam_utils)
    return steam_utils.find_games()


def test_steam_games_are_found_by_app_id(steam):
    from basic_games.steam_utils import find_games

    games = find_games()
    assert "524220" in games
    assert games["1091500"] == steam / "steamapps/common/Cyberpunk 2077"
    assert games.get("999999") is None
    assert "999999" not in games


def test_the_three_games_are_detected(steam):
    plugins = _load_basic_games()
    expected = {
        "NierAutomataGame": "NieRAutomata",
        "Cyberpunk2077Game": "Cyberpunk 2077",
        "BG3Game": "Baldurs Gate 3",
    }
    for cls, installdir in expected.items():
        game = plugins[cls]
        game.detectGame()
        assert game.isInstalled(), cls
        assert (
            Path(game.gameDirectory().absolutePath())
            == steam / "steamapps/common" / installdir
        )


def test_a_game_with_several_steam_ids_is_detected_by_the_second(steam):
    from basic_games.basic_game import BasicGame

    class TwoIdsGame(BasicGame):
        Name = "Two IDs"
        Author = "test"
        Version = "1.0.0"
        GameName = "Two IDs"
        GameShortName = "twoids"
        GameSteamId = ["111", "524220"]
        GameBinary = "NieRAutomata.exe"
        GameDataPath = ""

    BasicGame.steam_games = _load_basic_games_steam()
    game = TwoIdsGame()
    assert "111" not in BasicGame.steam_games
    game.detectGame()
    assert game.isInstalled()
    assert (
        Path(game.gameDirectory().absolutePath())
        == steam / "steamapps/common/NieRAutomata"
    )
    # the ID of the installed game, not the first of the list
    assert game.steamAPPId() == "524220"
    assert game.is_steam()


def test_the_steam_app_id_follows_a_path_set_by_hand(steam):
    from basic_games.basic_game import BasicGame

    BasicGame.steam_games = _load_basic_games_steam()
    game = _load_basic_games()["Cyberpunk2077Game"].__class__()
    assert not game.is_steam()
    game.setGamePath(steam / "steamapps/common/Cyberpunk 2077")
    assert game.steamAPPId() == "1091500"
    assert game.is_steam()

    # a folder that is not a Steam game of this one (and a link to the real one)
    other = _load_basic_games()["Cyberpunk2077Game"].__class__()
    other.setGamePath(steam / "steamapps/common/NieRAutomata")
    assert not other.is_steam()
    link = steam.parent / "link"
    link.symlink_to(steam / "steamapps/common/Cyberpunk 2077")
    linked = game.__class__()
    linked.setGamePath(link)
    assert linked.steamAPPId() == "1091500" and linked.is_steam()


def test_every_game_module_imports_on_linux(steam, capsys):
    import basic_games

    assert basic_games.createPlugins()
    assert "Failed to import" not in capsys.readouterr().err


def test_a_game_that_is_not_installed_is_not_detected(steam):
    plugins = _load_basic_games()
    game = plugins["NierAutomataGame"]
    # the Steam of a HOME without the game
    (steam / "steamapps/appmanifest_524220.acf").unlink()
    game.detectGame()
    assert not game.isInstalled()


def test_other_stores_are_empty_on_linux():
    from basic_games import eadesktop_utils, epic_utils, gog_utils, origin_utils

    assert gog_utils.find_games() == {}
    assert origin_utils.find_games() == {}
    errors = []
    assert epic_utils.find_games(errors) == {} and errors == []
    assert eadesktop_utils.find_games(errors) == {} and errors == []
