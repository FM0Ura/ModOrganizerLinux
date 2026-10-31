from __future__ import annotations

from pathlib import Path
from PyQt6.QtCore import Qt
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
    ) -> None:
        super().__init__("AI Assistant", parent)
        self.setObjectName("AiAssistantDockWidget")
        self.setAllowedAreas(
            Qt.DockWidgetArea.RightDockWidgetArea
            | Qt.DockWidgetArea.LeftDockWidgetArea
            | Qt.DockWidgetArea.BottomDockWidgetArea
        )
        self.chat_panel = ChatPanel(dispatcher, mcp_server, data_dir, self)
        self.setWidget(self.chat_panel)
