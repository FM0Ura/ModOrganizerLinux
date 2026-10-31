from __future__ import annotations

from pathlib import Path
import mobase
from PyQt6.QtCore import QCoreApplication, Qt
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import QMainWindow

from .agent.mcp_server import LocalMcpServer
from .tools import MainThreadDispatcher, build_default_registry
from .ui.dock_widget import AiAssistantDock


class AiAssistantPlugin(mobase.IPluginTool):
    def __init__(self) -> None:
        super().__init__()
        self._organizer: mobase.IOrganizer | None = None
        self._dock: AiAssistantDock | None = None
        self._dispatcher: MainThreadDispatcher | None = None
        self._mcp_server: LocalMcpServer | None = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        organizer.onUserInterfaceInitialized(self._on_ui_initialized)
        return True

    def _on_ui_initialized(self, main_window: QMainWindow) -> None:
        if self._dock is None:
            data_dir = Path(self._organizer.pluginDataPath()) / "ai_assistant"
            data_dir.mkdir(parents=True, exist_ok=True)
            reg = build_default_registry()
            self._dispatcher = MainThreadDispatcher(self._organizer, reg, permission_mode="safe")
            self._mcp_server = LocalMcpServer(self._dispatcher)
            self._mcp_server.start()
            self._dock = AiAssistantDock(self._dispatcher, self._mcp_server, data_dir, main_window)
            main_window.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self._dock)
        else:
            self._dock.setParent(main_window)
            main_window.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self._dock)

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
