import mobase
from PyQt6.QtCore import QDir

from ..basic_game import BasicGame

_PAKS = "SB/Content/Paks"
_WIN64 = "SB/Binaries/Win64"
_UE4SS = _WIN64 + "/ue4ss"

# Folders (compared with casefold()) that are completed up to the canonical SB/... path
_FOLDERS = {
    "content": "SB/Content",
    "paks": _PAKS,
    "~mods": _PAKS + "/~mods",
    "logicmods": _PAKS + "/LogicMods",
    "binaries": "SB/Binaries",
    "win64": _WIN64,
    "ue4ss": _UE4SS,
}
_PAK_EXTENSIONS = (".pak", ".ucas", ".utoc")
_JUNK_EXTENSIONS = (".md", ".png", ".jpg", ".jpeg", ".webp")


class StellarBladeModDataChecker(mobase.ModDataChecker):
    """
    Checks and fixes the layout of a Stellar Blade mod archive: everything has to end
    up under SB/ (paks in SB/Content/Paks/~mods or LogicMods, UE4SS in
    SB/Binaries/Win64/ue4ss).
    """

    @staticmethod
    def _is_junk(entry: mobase.FileTreeEntry) -> bool:
        if not entry.isFile():
            return False
        name = entry.name().casefold()
        if name == "enabled.txt":
            return False
        return (
            name.endswith(".txt")
            or name.endswith(_JUNK_EXTENSIONS)
            or name == "manifest.json"
        )

    @staticmethod
    def _is_lua_mod(entry: mobase.FileTreeEntry) -> bool:
        if not isinstance(entry, mobase.IFileTree):
            return False
        return (
            entry.find("Scripts/main.lua") is not None
            or entry.find("dlls/main.dll") is not None
            or entry.find("enabled.txt") is not None
        )

    def _is_mods_folder(self, entry: mobase.FileTreeEntry) -> bool:
        return (
            isinstance(entry, mobase.IFileTree)
            and entry.name().casefold() == "mods"
            and any(self._is_lua_mod(child) for child in entry)
        )

    def _is_recognized(self, entry: mobase.FileTreeEntry) -> bool:
        return self._destination(entry) is not None or (
            entry.isDir() and entry.name().casefold() == "sb"
        )

    def _destination(self, entry: mobase.FileTreeEntry) -> str | None:
        """The canonical path the entry has to be moved to; None if it is not
        recognized or already in place (SB/)."""
        name = entry.name()
        key = name.casefold()
        if entry.isFile():
            if key.endswith(_PAK_EXTENSIONS):
                return f"{_PAKS}/~mods/{name}"
            if key == "dwmapi.dll":
                return f"{_WIN64}/dwmapi.dll"
            if key == "ue4ss.dll":
                return f"{_UE4SS}/UE4SS.dll"
            return None
        if key in _FOLDERS:
            return _FOLDERS[key]
        if self._is_mods_folder(entry):
            return f"{_UE4SS}/Mods"
        if self._is_lua_mod(entry):
            return f"{_UE4SS}/Mods/{name}"
        return None

    def _wrapper(self, level: mobase.IFileTree) -> mobase.IFileTree | None:
        """The only folder of the level (ignoring junk) if it is not recognized."""
        entries = [e for e in level if not self._is_junk(e)]
        if len(entries) == 1 and isinstance(entries[0], mobase.IFileTree):
            if not self._is_recognized(entries[0]):
                return entries[0]
        return None

    def _levels(self, tree: mobase.IFileTree) -> list[mobase.IFileTree]:
        levels = [tree]
        while (wrapper := self._wrapper(levels[-1])) is not None:
            levels.append(wrapper)
        return levels

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        levels = self._levels(filetree)
        recognized = False
        for level in levels:
            for entry in level:
                if entry.isFile() and entry.name().casefold().endswith(".exe"):
                    return mobase.ModDataChecker.INVALID
        for entry in levels[-1]:
            recognized = recognized or self._is_recognized(entry)
        if not recognized:
            return mobase.ModDataChecker.INVALID
        if len(levels) == 1 and all(
            not self._is_junk(e) and self._destination(e) is None for e in filetree
        ):
            return mobase.ModDataChecker.VALID
        return mobase.ModDataChecker.FIXABLE

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        levels = self._levels(filetree)
        for level in levels:
            for entry in [e for e in level if self._is_junk(e)]:
                level.remove(entry)
        if len(levels) > 1:
            for entry in list(levels[-1]):
                filetree.move(entry, "/", mobase.IFileTree.InsertPolicy.MERGE)
            filetree.remove(levels[1])
        for entry in list(filetree):
            destination = self._destination(entry)
            if destination is not None:
                filetree.move(entry, destination, mobase.IFileTree.InsertPolicy.MERGE)
        return filetree


class StellarBladeGame(BasicGame):
    Name = "Stellar Blade Support Plugin"
    Author = "FM0Ura"
    Version = "1.0.0"

    GameName = "Stellar Blade"
    GameShortName = "stellarblade"
    GameNexusName = "stellarblade"
    GameSteamId = "3489700"

    GameBinary = "SB/Binaries/Win64/SB-Win64-Shipping.exe"
    GameLauncher = "SB.exe"
    GameDataPath = "%GAME_PATH%"

    GameDocumentsDirectory = (
        "%USERPROFILE%/AppData/Local/SB/Saved/Config/WindowsNoEditor"
    )
    GameSavesDirectory = "%USERPROFILE%/AppData/Local/SB/Saved/SaveGames"
    GameSaveExtension = "sav"
    GameIniFiles = ["Engine.ini", "Game.ini", "Input.ini", "GameUserSettings.ini"]

    def paksDirectory(self) -> QDir:
        return QDir(self.gameDirectory().absoluteFilePath("SB/Content/Paks"))

    def ue4ssModsDirectory(self) -> QDir:
        return QDir(
            self.gameDirectory().absoluteFilePath("SB/Binaries/Win64/ue4ss/Mods")
        )

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(StellarBladeModDataChecker())
        return True
