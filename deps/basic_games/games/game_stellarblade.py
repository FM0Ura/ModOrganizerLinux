from PyQt6.QtCore import QDir

from ..basic_game import BasicGame


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
