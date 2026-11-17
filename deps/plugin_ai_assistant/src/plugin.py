from __future__ import annotations

import logging
from pathlib import Path
import mobase
from PyQt6 import sip
from PyQt6.QtCore import QCoreApplication, Qt
from PyQt6.QtGui import QIcon, QKeySequence, QShortcut
from PyQt6.QtWidgets import QMainWindow

from .agent.mcp_server import LocalMcpServer
from .tools import MainThreadDispatcher, build_default_registry
from .ui.dock_widget import AiAssistantDock

ICON_PATH = Path(__file__).parent / "ui" / "icons" / "ai_assistant.svg"
SHORTCUT = "Ctrl+Shift+A"

_log = logging.getLogger(__name__)


class AiAssistantPlugin(mobase.IPluginTool):
    def __init__(self) -> None:
        super().__init__()
        self._organizer: mobase.IOrganizer | None = None
        self._dock: AiAssistantDock | None = None
        self._dispatcher: MainThreadDispatcher | None = None
        self._mcp_server: LocalMcpServer | None = None
        self._shortcut: QShortcut | None = None
        self._quit_connected = False

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        organizer.onUserInterfaceInitialized(self._on_ui_initialized)
        return True

    def _on_ui_initialized(self, main_window: QMainWindow) -> None:
        # MO2 restarts in the same process (Change Game, instance switch); in this port a
        # new plugin object is created for each run, but a second call on the same object
        # must work too: it then moves the dock and its conversation to the new window
        instance_path = self._organizer.basePath()
        if self._dock is not None and sip.isdeleted(self._dock):
            # deleted with the previous main window; its aboutToQuit saved the conversation
            self._dock = None
        if self._dispatcher is None:
            reg = build_default_registry()
            self._dispatcher = MainThreadDispatcher(self._organizer, reg, permission_mode="safe")
            self._mcp_server = LocalMcpServer(self._dispatcher)
        else:
            # after an aboutToQuit on this same object: both were closed
            self._dispatcher.reopen()
        self._mcp_server.start()  # no-op while it runs
        # before the history is read: a failure there must not leave MO2 without the
        # shortcut or, worse, without the hook that saves the conversation on exit
        self._install_shortcut(main_window)
        if not self._quit_connected:
            QCoreApplication.instance().aboutToQuit.connect(self._on_about_to_quit)
            self._quit_connected = True
        if self._dock is None:
            data_dir = Path(self._organizer.pluginDataPath()) / "ai_assistant"
            data_dir.mkdir(parents=True, exist_ok=True)
            self._dock = AiAssistantDock(
                self._dispatcher, self._mcp_server, data_dir, main_window, instance_path=instance_path
            )
            # MO2 restores a position the user saved (by objectName) over this default
            main_window.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self._dock)
            try:
                self._dock.chat_panel.open_most_recent()
            except Exception:
                _log.exception("AI Assistant: could not open the most recent conversation")
        else:
            # the conversation of the previous run is stopped and saved before the dock
            # moves; then the panel shows the history of this run's instance
            self._dock.chat_panel.switch_instance(instance_path)
            self._dock.setParent(main_window)
            main_window.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, self._dock)

    def _install_shortcut(self, main_window: QMainWindow) -> None:
        if self._shortcut is not None:
            try:
                self._shortcut.activated.disconnect()
                self._shortcut.setEnabled(False)
                self._shortcut.setParent(None)
                self._shortcut.deleteLater()
            except RuntimeError:
                pass  # already deleted with the previous main window
        self._shortcut = QShortcut(QKeySequence(SHORTCUT), main_window)
        self._shortcut.setContext(Qt.ShortcutContext.ApplicationShortcut)
        self._shortcut.activated.connect(self.display)

    def _on_about_to_quit(self) -> None:
        """Qt emits aboutToQuit on every exit of MO2's restart loop, before the main window
        (and the dock) are deleted. Only Python state, the worker and files are touched here
        (QtWebEngine crashed in a second aboutToQuit before); the handler disconnects itself,
        since this plugin object is unloaded after a restart."""
        if self._quit_connected:
            try:
                QCoreApplication.instance().aboutToQuit.disconnect(self._on_about_to_quit)
            except (TypeError, RuntimeError):
                pass
            self._quit_connected = False
        # an exception escaping a PyQt6 slot is fatal: log it instead
        try:
            if self._dock is not None and not sip.isdeleted(self._dock):
                self._dock.chat_panel.shutdown()
        except Exception:
            _log.exception("AI Assistant: could not save the conversation on exit")
        try:
            if self._dispatcher is not None:
                self._dispatcher.close()
            if self._mcp_server is not None:
                # this plugin object is unloaded after a restart: its server must not leak
                self._mcp_server.stop()
        except Exception:
            _log.exception("AI Assistant: could not stop the MCP server on exit")

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
        return QIcon(str(ICON_PATH))

    def display(self) -> None:
        if self._dock is not None:
            self._dock.focus_input()

    def _tr(self, text: str) -> str:
        return QCoreApplication.translate("AiAssistantPlugin", text)
