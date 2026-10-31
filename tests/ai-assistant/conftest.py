from __future__ import annotations

import sys
from pathlib import Path
import pytest
from PyQt6.QtCore import QDir
from PyQt6.QtWidgets import QApplication

_app = None


def pytest_configure():
    global _app
    if QApplication.instance() is None:
        _app = QApplication(sys.argv)


class FakeMod:
    def __init__(self, name: str, path: Path, version: str = "1.0.0", nexus_id: int = 100, is_sep: bool = False, is_foreign: bool = False):
        self._name = name
        self._path = path
        self._version = version
        self._nexus_id = nexus_id
        self._is_sep = is_sep
        self._is_foreign = is_foreign

    def name(self) -> str:
        return self._name

    def absolutePath(self) -> str:
        return str(self._path)

    def newestVersion(self):
        return self._version

    def nexusId(self) -> int:
        return self._nexus_id

    def isSeparator(self) -> bool:
        return self._is_sep

    def isForeign(self) -> bool:
        return self._is_foreign


class FakeModList:
    def __init__(self, mods_dir: Path):
        self._mods_dir = mods_dir
        self._mods: dict[str, FakeMod] = {}
        self._active: dict[str, bool] = {}
        self._priority: dict[str, int] = {}

    def add_fake_mod(self, name: str, active: bool = True, priority: int | None = None, is_sep: bool = False) -> FakeMod:
        mod_path = self._mods_dir / name
        mod_path.mkdir(parents=True, exist_ok=True)
        mod = FakeMod(name, mod_path, is_sep=is_sep)
        self._mods[name] = mod
        self._active[name] = active
        self._priority[name] = len(self._priority) if priority is None else priority
        return mod

    def allModsByProfilePriority(self, profile=None) -> list[str]:
        return [k for k, _ in sorted(self._priority.items(), key=lambda x: x[1])]

    def state(self, name: str) -> int:
        if name not in self._mods:
            return 0
        # 0x02 == STATE_ACTIVE in mobase
        return 0x02 if self._active.get(name, False) else 0x01

    def priority(self, name: str) -> int:
        return self._priority.get(name, -1)

    def getMod(self, name: str):
        return self._mods.get(name)

    def setActive(self, names, active: bool) -> int:
        if isinstance(names, str):
            names = [names]
        count = 0
        for n in names:
            if n in self._mods and not self._mods[n].isSeparator():
                self._active[n] = bool(active)
                count += 1
        return count

    def setPriority(self, name: str, new_priority: int) -> bool:
        if name not in self._mods:
            return False
        self._priority[name] = int(new_priority)
        return True


class FakePluginList:
    def __init__(self):
        self._plugins: dict[str, dict] = {
            "Skyrim.esm": {"active": True, "order": 0, "master": True, "light": False, "origin": "data"},
            "SkyUI_SE.esp": {"active": True, "order": 1, "master": False, "light": False, "origin": "SkyUI"},
        }

    def pluginNames(self) -> list[str]:
        return list(self._plugins.keys())

    def state(self, name: str) -> int:
        if name not in self._plugins:
            return 0
        return 2 if self._plugins[name]["active"] else 1

    def loadOrder(self, name: str) -> int:
        return self._plugins.get(name, {}).get("order", -1)

    def isMasterFlagged(self, name: str) -> bool:
        return self._plugins.get(name, {}).get("master", False)

    def isLightFlagged(self, name: str) -> bool:
        return self._plugins.get(name, {}).get("light", False)

    def origin(self, name: str) -> str:
        return self._plugins.get(name, {}).get("origin", "")

    def setState(self, name: str, state: int) -> None:
        if name in self._plugins:
            self._plugins[name]["active"] = (state == 2)


class FakeGame:
    def __init__(self, game_dir: Path):
        self._game_dir = game_dir

    def gameName(self) -> str:
        return "Skyrim Special Edition"

    def gameShortName(self) -> str:
        return "SkyrimSE"

    def gameDirectory(self) -> QDir:
        return QDir(str(self._game_dir))


class FakeOrganizer:
    def __init__(self, root: Path):
        self.root = root
        self.base_dir = root / "instance"
        self.mods_dir = self.base_dir / "mods"
        self.downloads_dir = self.base_dir / "downloads"
        self.overwrite_dir = self.base_dir / "overwrite"
        self.profile_dir = self.base_dir / "profiles/Default"
        self.logs_dir = self.base_dir / "logs"
        self.plugin_data_dir = root / "plugin_data"
        self.game_dir = root / "game"
        for d in (self.mods_dir, self.downloads_dir, self.overwrite_dir, self.profile_dir, self.logs_dir, self.plugin_data_dir, self.game_dir):
            d.mkdir(parents=True, exist_ok=True)
        self._mod_list = FakeModList(self.mods_dir)
        self._plugin_list = FakePluginList()
        self._game = FakeGame(self.game_dir)
        self.installed_archives: list[tuple[str, str]] = []
        self.refresh_count = 0

    def basePath(self) -> str:
        return str(self.base_dir)

    def modsPath(self) -> str:
        return str(self.mods_dir)

    def downloadsPath(self) -> str:
        return str(self.downloads_dir)

    def overwritePath(self) -> str:
        return str(self.overwrite_dir)

    def profilePath(self) -> str:
        return str(self.profile_dir)

    def profileName(self) -> str:
        return "Default"

    def pluginDataPath(self) -> str:
        return str(self.plugin_data_dir)

    def modList(self) -> FakeModList:
        return self._mod_list

    def pluginList(self) -> FakePluginList:
        return self._plugin_list

    def managedGame(self) -> FakeGame:
        return self._game

    def resolvePath(self, subpath: str) -> str:
        for mod_name in reversed(self._mod_list.allModsByProfilePriority()):
            if self._mod_list.state(mod_name) == 0x02:
                candidate = self.mods_dir / mod_name / subpath
                if candidate.exists():
                    return str(candidate)
        return ""

    def installMod(self, archive_path: str, name_suggestion: str = ""):
        self.installed_archives.append((archive_path, name_suggestion))
        mod_name = name_suggestion or Path(archive_path).stem
        return self._mod_list.add_fake_mod(mod_name, active=True)

    def createMod(self, name: str):
        return self._mod_list.add_fake_mod(name, active=False, is_sep=name.endswith("_separator"))

    def refresh(self, save_changes: bool = True) -> None:
        self.refresh_count += 1

    def onUserInterfaceInitialized(self, callback) -> None:
        self._ui_callback = callback


@pytest.fixture
def fake_organizer(tmp_path):
    org = FakeOrganizer(tmp_path)
    org.modList().add_fake_mod("USSEP", active=True, priority=0)
    org.modList().add_fake_mod("SkyUI", active=True, priority=1)
    org.modList().add_fake_mod("Lux", active=False, priority=2)
    return org
