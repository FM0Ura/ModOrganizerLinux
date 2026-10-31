from __future__ import annotations

import mobase
from PyQt6.QtCore import QCoreApplication
from PyQt6.QtGui import QIcon


class AiAssistantPlugin(mobase.IPluginTool):
    def __init__(self) -> None:
        super().__init__()
        self._organizer: mobase.IOrganizer | None = None
        self._dock = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        return True

    def name(self) -> str:
        return "AI Assistant"

    def localizedName(self) -> str:
        return self._tr("AI Assistant")

    def author(self) -> str:
        return "ModOrganizerLinux"

    def description(self) -> str:
        return self._tr("Integrated AI agent for mod management, log analysis, and debugging.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.FINAL)

    def settings(self) -> list[mobase.PluginSetting]:
        return [
            mobase.PluginSetting("enabled", self._tr("Enable the AI Assistant dock panel"), True),
        ]

    def displayName(self) -> str:
        return self._tr("AI Assistant")

    def tooltip(self) -> str:
        return self._tr("Open or focus the AI Assistant panel")

    def icon(self) -> QIcon:
        return QIcon()

    def display(self) -> None:
        if self._dock is not None:
            self._dock.show()
            self._dock.raise_()

    def _tr(self, text: str) -> str:
        return QCoreApplication.translate("AiAssistantPlugin", text)
