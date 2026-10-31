from __future__ import annotations

import uuid
from pathlib import Path
from PyQt6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QComboBox,
    QPushButton,
    QTextBrowser,
    QPlainTextEdit,
)
from ..config import AssistantConfig
from ..tools.dispatcher import MainThreadDispatcher
from ..agent.mcp_server import LocalMcpServer
from ..agent.worker import AgentTurnWorker
from .approval_card import ApprovalCard
from .settings_dialog import AssistantSettingsDialog

PROVIDER_LABELS = [
    ("claude_cli", "Claude Code (CLI)"),
    ("codex_cli", "Codex (CLI)"),
    ("agy_cli", "Antigravity (CLI)"),
    ("ollama", "Ollama (Local)"),
    ("gemini", "Google Gemini API"),
    ("openai", "OpenAI / Compatible API"),
    ("anthropic", "Anthropic API"),
]


class ChatPanel(QWidget):
    def __init__(
        self,
        dispatcher: MainThreadDispatcher,
        mcp_server: LocalMcpServer,
        data_dir: Path,
        parent=None,
    ) -> None:
        super().__init__(parent)
        self._dispatcher = dispatcher
        self._mcp_server = mcp_server
        self._data_dir = data_dir
        self._config = AssistantConfig.load(data_dir)
        self._thread_id = str(uuid.uuid4())
        self._worker: AgentTurnWorker | None = None

        self._dispatcher.set_permission_mode(self._config.permission_mode)
        self._dispatcher.approval_requested.connect(self._on_approval_requested)

        root = QVBoxLayout(self)

        # Top bar
        top_bar = QHBoxLayout()
        self.provider_combo = QComboBox()
        for key, label in PROVIDER_LABELS:
            self.provider_combo.addItem(label, key)
        idx = next((i for i, (k, _) in enumerate(PROVIDER_LABELS) if k == self._config.provider), 0)
        self.provider_combo.setCurrentIndex(idx)
        self.provider_combo.currentIndexChanged.connect(self._on_provider_changed)

        self.mode_combo = QComboBox()
        self.mode_combo.addItem("Safe Mode (Confirm Writes)", "safe")
        self.mode_combo.addItem("Autonomous Mode", "autonomous")
        self.mode_combo.setCurrentIndex(0 if self._config.permission_mode == "safe" else 1)
        self.mode_combo.currentIndexChanged.connect(self._on_mode_changed)

        self.clear_btn = QPushButton("New Chat")
        self.clear_btn.clicked.connect(self._on_new_chat)
        self.settings_btn = QPushButton("Settings")
        self.settings_btn.clicked.connect(self._on_open_settings)

        top_bar.addWidget(self.provider_combo, 2)
        top_bar.addWidget(self.mode_combo, 2)
        top_bar.addWidget(self.clear_btn)
        top_bar.addWidget(self.settings_btn)
        root.addLayout(top_bar)

        # Transcript area + approval container
        self.transcript = QTextBrowser()
        self.transcript.setOpenExternalLinks(True)
        root.addWidget(self.transcript, 1)

        self.cards_layout = QVBoxLayout()
        root.addLayout(self.cards_layout)

        # Quick action chips
        chips = QHBoxLayout()
        for label, prompt in [
            ("Diagnose Crash", "Read the latest crash log and identify which mod or file caused the crash."),
            ("Check Mod Conflicts", "Inspect my active mods and report any notable issues or inactive dependencies."),
            ("Review Load Order", "List my active plugin load order and check for issues."),
        ]:
            btn = QPushButton(label)
            btn.clicked.connect(lambda _=False, p=prompt: self.send_prompt(p))
            chips.addWidget(btn)
        root.addLayout(chips)

        # Input row
        input_row = QHBoxLayout()
        self.input_box = QPlainTextEdit()
        self.input_box.setPlaceholderText("Ask the AI Assistant to analyze logs, toggle mods, install archives...")
        self.input_box.setMaximumHeight(75)
        self.send_btn = QPushButton("Send")
        self.send_btn.clicked.connect(self._on_send_clicked)
        input_row.addWidget(self.input_box, 1)
        input_row.addWidget(self.send_btn)
        root.addLayout(input_row)

    def _on_provider_changed(self) -> None:
        self._config.provider = self.provider_combo.currentData()

    def _on_mode_changed(self) -> None:
        mode = self.mode_combo.currentData()
        self._config.permission_mode = mode
        self._dispatcher.set_permission_mode(mode)

    def _on_new_chat(self) -> None:
        self._thread_id = str(uuid.uuid4())
        self._dispatcher.reset_session_approvals()
        self.transcript.clear()

    def _on_open_settings(self) -> None:
        dlg = AssistantSettingsDialog(self._config, self._data_dir, self)
        dlg.exec()

    def _on_send_clicked(self) -> None:
        text = self.input_box.toPlainText().strip()
        if not text:
            return
        self.input_box.clear()
        self.send_prompt(text)

    def send_prompt(self, text: str) -> None:
        if self._worker is not None and self._worker.isRunning():
            return
        self.transcript.append(f"<p><b>You:</b> {text}</p>")
        self.send_btn.setEnabled(False)
        sqlite_path = self._data_dir / "checkpoints.sqlite"
        self._worker = AgentTurnWorker(
            dispatcher=self._dispatcher,
            config=self._config,
            user_text=text,
            thread_id=self._thread_id,
            mcp_endpoint=self._mcp_server.endpoint_url(),
            mcp_token=self._mcp_server.token,
            sqlite_path=sqlite_path,
        )
        self._worker.response_ready.connect(self._on_response_ready)
        self._worker.error_occurred.connect(self._on_error)
        self._worker.start()

    def _on_response_ready(self, reply: str) -> None:
        self.transcript.append(f"<p><b>AI Assistant:</b><br>{reply}</p>")
        self.send_btn.setEnabled(True)

    def _on_error(self, err: str) -> None:
        self.transcript.append(f"<p style='color:#d9534f;'><b>Error:</b> {err}</p>")
        self.send_btn.setEnabled(True)

    def _on_approval_requested(self, req_id: str, tool_name: str, args: dict, destructive: bool) -> None:
        card = ApprovalCard(req_id, tool_name, args, destructive=destructive, parent=self)

        def _handle_decision(r_id: str, decision: str) -> None:
            self._dispatcher.resolve_approval(r_id, decision)
            card.setParent(None)
            card.deleteLater()

        card.decision_made.connect(_handle_decision)
        self.cards_layout.addWidget(card)
