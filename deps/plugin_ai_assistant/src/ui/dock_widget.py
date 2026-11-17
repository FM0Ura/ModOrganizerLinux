from __future__ import annotations

from pathlib import Path
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QCloseEvent
from PyQt6.QtWidgets import QDockWidget, QMainWindow

from ..agent.mcp_server import LocalMcpServer
from ..tools.dispatcher import MainThreadDispatcher
from .chat_panel import ChatPanel


class AiAssistantDock(QDockWidget):
    """QDockWidget hosting the AI Assistant chat panel inside MO2's main window."""

    def __init__(
        self,
        dispatcher: MainThreadDispatcher,
        mcp_server: LocalMcpServer,
        data_dir: Path,
        parent: QMainWindow | None = None,
        instance_path: str | None = None,
    ) -> None:
        super().__init__("AI Assistant", parent)
        self.setObjectName("AiAssistantDockWidget")
        self.setAllowedAreas(
            Qt.DockWidgetArea.RightDockWidgetArea
            | Qt.DockWidgetArea.LeftDockWidgetArea
            | Qt.DockWidgetArea.BottomDockWidgetArea
        )
        self.chat_panel = ChatPanel(dispatcher, mcp_server, data_dir, self, instance_path=instance_path)
        self.setWidget(self.chat_panel)

    def focus_input(self) -> None:
        """Shows the dock and puts the cursor in the input box (Ctrl+Shift+A, Tools menu)."""
        self.show()
        self.raise_()
        # a no-op on KDE Wayland: the input box gets the focus of its window anyway
        self.activateWindow()
        self.chat_panel.focus_input()

    def closeEvent(self, event: QCloseEvent) -> None:
        # closing the dock only hides it: a running turn goes on, but what is typed is
        # saved now; stopping the turn is for when MO2 itself closes (plugin aboutToQuit)
        self.chat_panel.save_draft()
        super().closeEvent(event)
