"""Linux: the Windows user folders of a Steam game are in its Proton prefix
(Phase 4d.2, spec section 3.2); without a prefix, None (the caller keeps the XDG
folders of today)."""

from __future__ import annotations

import sys
from typing import TYPE_CHECKING

from PyQt6.QtCore import qWarning

if TYPE_CHECKING:
    from .basic_game import BasicGame

_warned: set[str] = set()


def steam_ids(game: BasicGame) -> list[str]:
    """The Steam app IDs to look for the prefix of the game: the one recorded when
    the game path was set (the right one for a game with several IDs), else all."""
    mappings = getattr(game, "_mappings", None)  # absent while the game is built
    if mappings is None:
        return []
    ids = mappings.steamAPPId
    if ids.has_value():
        return [ids.current()]
    return [i for i in ids.get() if i]


def _compat_data(steam_ids: list[str], game_name: str) -> str | None:
    if sys.platform == "win32" or not steam_ids:
        # not a Steam game: it has no prefix, nothing to warn about
        return None
    import mobase

    for appid in steam_ids:
        app = mobase.utils.steamApp(str(appid))
        if app is not None and app["compatData"]:
            return app["compatData"]
    if game_name not in _warned:
        _warned.add(game_name)
        qWarning(
            f"{game_name}: no Proton prefix (run the game once from Steam); "
            "using the folders of Linux for its documents and saves"
        )
    return None


def user_profile(steam_ids: list[str], game_name: str) -> str | None:
    compat = _compat_data(steam_ids, game_name)
    return f"{compat}/pfx/drive_c/users/steamuser" if compat else None


def documents(steam_ids: list[str], game_name: str) -> str | None:
    profile = user_profile(steam_ids, game_name)
    return f"{profile}/Documents" if profile else None
