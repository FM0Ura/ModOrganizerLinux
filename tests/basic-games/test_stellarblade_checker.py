# Tests of the Stellar Blade mod data checker (Phase 6a), with the layouts of the real
# mods (spec 3.2).
from typing import TypeAlias, cast
from unittest.mock import MagicMock

import mobase
from test_detection import _load_basic_games

V = mobase.ModDataChecker.VALID
F = mobase.ModDataChecker.FIXABLE
I = mobase.ModDataChecker.INVALID  # noqa: E741

_tree_values: TypeAlias = list["str | tuple[str, _tree_values]"]


def make_tree(
    values: _tree_values, root: mobase.IFileTree | None = None
) -> mobase.IFileTree:
    if root is None:
        root = cast(mobase.IFileTree, mobase.private.makeTree())  # type: ignore

    for value in values:
        if isinstance(value, str):
            root.addFile(value)
        else:
            sub_tree = root.addDirectory(value[0])
            make_tree(value[1], sub_tree)

    return root


def _checker() -> mobase.ModDataChecker:
    game = _load_basic_games()["StellarBladeGame"]
    organizer = MagicMock()
    registered: list[object] = []
    organizer.gameFeatures().registerFeature.side_effect = (
        lambda _game, feature, *_: registered.append(feature) or True
    )
    game.init(organizer)
    checkers = [f for f in registered if isinstance(f, mobase.ModDataChecker)]
    assert len(checkers) == 1
    return checkers[0]


def _files(tree: mobase.IFileTree) -> set[str]:
    return {e.path("/") for e in tree.walk() if e.isFile()}


def _fixed(values: _tree_values) -> set[str]:
    checker = _checker()
    tree = make_tree(values)
    assert checker.dataLooksValid(tree) == F
    return _files(checker.fix(tree))


def test_cns_full_layout_is_valid():  # mod 1496
    t = make_tree(
        [
            (
                "SB",
                [
                    (
                        "Binaries",
                        [
                            (
                                "Win64",
                                [
                                    (
                                        "ue4ss",
                                        [
                                            (
                                                "Mods",
                                                [
                                                    (
                                                        "DekCNS",
                                                        [
                                                            "enabled.txt",
                                                            ("Scripts", ["main.lua"]),
                                                        ],
                                                    )
                                                ],
                                            ),
                                            "UE4SS-settings.ini",
                                        ],
                                    )
                                ],
                            )
                        ],
                    ),
                    (
                        "Content",
                        [
                            (
                                "Paks",
                                [
                                    (
                                        "LogicMods",
                                        ["DekCNS_P.pak", "DekCNS_P.ucas", "DekCNS_P.utoc"],
                                    ),
                                    (
                                        "~mods",
                                        [
                                            (
                                                "CustomNanosuitSystem",
                                                [("Cosmetics", ["a.dekcns.json"])],
                                            )
                                        ],
                                    ),
                                ],
                            )
                        ],
                    ),
                ],
            )
        ]
    )
    assert _checker().dataLooksValid(t) == V


def test_ue4ss_itself_layout_is_valid():  # mod 2952
    t = make_tree(
        [
            (
                "SB",
                [
                    (
                        "Binaries",
                        [
                            (
                                "Win64",
                                [
                                    "dwmapi.dll",
                                    (
                                        "ue4ss",
                                        [
                                            (
                                                "Mods",
                                                [
                                                    (
                                                        "BPModLoaderMod",
                                                        [("Scripts", ["main.lua"])],
                                                    ),
                                                    "mods.txt",
                                                ],
                                            )
                                        ],
                                    ),
                                ],
                            )
                        ],
                    )
                ],
            )
        ]
    )
    assert _checker().dataLooksValid(t) == V


def test_jiggle_ue4ss_root_is_prefixed():  # mod 1570
    assert _fixed(
        [
            (
                "ue4ss",
                [
                    (
                        "Mods",
                        [("JiggleUpdate", [("Scripts", ["main.lua"])]), "mods.txt"],
                    ),
                    "UE4SS-settings.ini",
                ],
            )
        ]
    ) == {
        "SB/Binaries/Win64/ue4ss/Mods/JiggleUpdate/Scripts/main.lua",
        "SB/Binaries/Win64/ue4ss/Mods/mods.txt",
        "SB/Binaries/Win64/ue4ss/UE4SS-settings.ini",
    }


def test_catwalk_tilde_mods_root_is_prefixed():  # mod 2717
    assert _fixed(
        [
            (
                "~Mods",
                [
                    "x_P.pak",
                    "x_P.ucas",
                    "x_P.utoc",
                    ("CustomNanosuitSystem", [("Animations", ["a.dekani.json"])]),
                ],
            )
        ]
    ) == {
        "SB/Content/Paks/~mods/x_P.pak",
        "SB/Content/Paks/~mods/x_P.ucas",
        "SB/Content/Paks/~mods/x_P.utoc",
        "SB/Content/Paks/~mods/CustomNanosuitSystem/Animations/a.dekani.json",
    }


def test_logicmods_root_stays_logicmods():
    assert _fixed([("LogicMods", ["a.pak"])]) == {"SB/Content/Paks/LogicMods/a.pak"}


def test_loose_paks_go_to_tilde_mods():
    assert _fixed(["a.pak", "a.ucas", "a.utoc", "readme.txt", "cover.png"]) == {
        "SB/Content/Paks/~mods/a.pak",
        "SB/Content/Paks/~mods/a.ucas",
        "SB/Content/Paks/~mods/a.utoc",
    }


def test_bare_lua_mod_folder_goes_to_ue4ss_mods():
    assert _fixed(
        [("MyMod", [("Scripts", ["main.lua"]), "enabled.txt"])]
    ) == {
        "SB/Binaries/Win64/ue4ss/Mods/MyMod/Scripts/main.lua",
        "SB/Binaries/Win64/ue4ss/Mods/MyMod/enabled.txt",
    }


def test_mods_folder_goes_to_ue4ss_mods():
    assert _fixed([("Mods", [("MyMod", [("Scripts", ["main.lua"])])])]) == {
        "SB/Binaries/Win64/ue4ss/Mods/MyMod/Scripts/main.lua"
    }


def test_loose_proxy_dll_goes_to_win64():
    assert _fixed(["dwmapi.dll", ("ue4ss", ["UE4SS.dll"])]) == {
        "SB/Binaries/Win64/dwmapi.dll",
        "SB/Binaries/Win64/ue4ss/UE4SS.dll",
    }
    assert _fixed(["UE4SS.dll"]) == {"SB/Binaries/Win64/ue4ss/UE4SS.dll"}


def test_wrapper_folder_is_unwrapped():
    assert _fixed(
        [("MeuMod-v1.2", [("SB", [("Content", [("Paks", [("~mods", ["a.pak"])])])])])]
    ) == {"SB/Content/Paks/~mods/a.pak"}
    assert _fixed([("MeuMod", [("~mods", ["a.pak"])])]) == {
        "SB/Content/Paks/~mods/a.pak"
    }


def test_wrapper_with_same_named_child_keeps_files():
    assert _fixed([("MyMod", [("MyMod", [("Scripts", ["main.lua"]), "enabled.txt"])])]) == {
        "SB/Binaries/Win64/ue4ss/Mods/MyMod/Scripts/main.lua",
        "SB/Binaries/Win64/ue4ss/Mods/MyMod/enabled.txt",
    }
    assert _fixed([("Foo", [("Foo", ["a.pak", "b.pak"])])]) == {
        "SB/Content/Paks/~mods/a.pak",
        "SB/Content/Paks/~mods/b.pak",
    }


def test_mixed_loose_pak_and_ue4ss():
    assert _fixed(
        ["a.pak", ("ue4ss", [("Mods", [("M", [("Scripts", ["main.lua"])])])])]
    ) == {
        "SB/Content/Paks/~mods/a.pak",
        "SB/Binaries/Win64/ue4ss/Mods/M/Scripts/main.lua",
    }


def test_casing_variants_are_recognized():
    checker = _checker()
    tree = make_tree(
        [("sb", [("content", [("paks", [("logicmods", ["a.pak"])])])])]
    )
    status = checker.dataLooksValid(tree)
    assert status in (V, F)
    if status == F:
        tree = checker.fix(tree)
    assert _files(tree) == {"sb/content/paks/logicmods/a.pak"} or _files(tree) == {
        "SB/Content/Paks/LogicMods/a.pak"
    }
    assert len(list(tree)) == 1


def test_exe_is_invalid():  # mod 861
    t = make_tree(["StellarBlade_Chunk_Id_exe.exe"])
    assert _checker().dataLooksValid(t) == I


def test_nothing_recognizable_is_invalid():
    t = make_tree(["readme.txt", ("Textures", ["a.dds"])])
    assert _checker().dataLooksValid(t) == I
