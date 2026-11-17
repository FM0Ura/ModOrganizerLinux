from __future__ import annotations

import time
import uuid
from pathlib import Path
from PyQt6.QtCore import QTimer
from PyQt6.QtGui import QTextCursor, QTextDocument
from PyQt6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QComboBox,
    QLabel,
    QPushButton,
    QTextBrowser,
    QPlainTextEdit,
)
from ..config import AssistantConfig
from ..tools.dispatcher import MainThreadDispatcher
from ..agent.cli_runner import prepare_bridge
from ..agent.graph import build_system_prompt
from ..agent.mcp_server import LocalMcpServer
from ..agent.worker import AgentTurnWorker
from .approval_card import ApprovalCard
from .settings_dialog import AssistantSettingsDialog

_MARKDOWN_FEATURES = (
    QTextDocument.MarkdownFeature.MarkdownDialectGitHub | QTextDocument.MarkdownFeature.MarkdownNoHTML
)

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
        self._data_dir = Path(data_dir)
        self._config = AssistantConfig.load(self._data_dir)
        self._thread_id = str(uuid.uuid4())
        self._worker: AgentTurnWorker | None = None
        self._turn_cancelled = False
        self._turn_started = 0.0
        self._bridge_path, self._endpoint_path = prepare_bridge(
            self._data_dir, mcp_server.endpoint_url(), mcp_server.token
        )

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

        # Busy row: shown while a turn runs
        busy_row = QHBoxLayout()
        self.status_label = QLabel()
        self.cancel_btn = QPushButton("Cancel")
        self.cancel_btn.clicked.connect(self._on_cancel_clicked)
        busy_row.addWidget(self.status_label, 1)
        busy_row.addWidget(self.cancel_btn)
        root.addLayout(busy_row)
        self.status_label.hide()
        self.cancel_btn.hide()
        self._busy_timer = QTimer(self)
        self._busy_timer.setInterval(1000)
        self._busy_timer.timeout.connect(self._update_busy_text)

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

    # --- settings ---------------------------------------------------------------------

    def _save_config(self) -> None:
        try:
            self._config.save(self._data_dir)
        except OSError as exc:
            self._append_error(f"Could not save the settings: {exc}")

    def _on_provider_changed(self) -> None:
        self._config.provider = self.provider_combo.currentData()
        self._save_config()
        if self._config.provider == "agy_cli":
            self._append_note(
                "Antigravity has no per-session MCP option: register the MO2 tools once "
                "with Settings → \"Register in Antigravity\"."
            )

    def _on_mode_changed(self) -> None:
        mode = self.mode_combo.currentData()
        self._config.permission_mode = mode
        self._dispatcher.set_permission_mode(mode)
        self._save_config()

    def _on_new_chat(self) -> None:
        self._thread_id = str(uuid.uuid4())
        self._dispatcher.reset_session_approvals()
        self.transcript.clear()

    def _on_open_settings(self) -> None:
        dlg = AssistantSettingsDialog(
            self._config, self._data_dir, self, bridge_path=self._bridge_path, endpoint_path=self._endpoint_path
        )
        dlg.exec()

    # --- transcript ---------------------------------------------------------------------

    def _append(self, header_html: str, body: str, markdown: bool) -> None:
        """Appends a block: header_html is trusted markup, body is shown as text or Markdown."""
        cursor = self.transcript.textCursor()
        cursor.movePosition(QTextCursor.MoveOperation.End)
        if not self.transcript.document().isEmpty():
            cursor.insertBlock()
        cursor.insertHtml(header_html)
        cursor.insertBlock()
        if markdown:
            # model output is Markdown; raw HTML in it is shown as text, never rendered
            cursor.insertMarkdown(body, _MARKDOWN_FEATURES)
        else:
            cursor.insertText(body)
        self.transcript.setTextCursor(cursor)
        self.transcript.ensureCursorVisible()

    def _append_note(self, text: str) -> None:
        self._append("<i style='color:gray;'>Note</i>", text, markdown=False)

    def _append_error(self, text: str) -> None:
        self._append("<b style='color:#d9534f;'>Error:</b>", text, markdown=False)

    # --- turns ----------------------------------------------------------------------------

    def _on_send_clicked(self) -> None:
        text = self.input_box.toPlainText().strip()
        if not text:
            return
        self.input_box.clear()
        self.send_prompt(text)

    def send_prompt(self, text: str) -> None:
        if self._worker is not None and self._worker.isRunning():
            return
        self._append("<b>You:</b>", text, markdown=False)

        # everything that touches the organizer happens here, on the main thread
        organizer = self._dispatcher._organizer
        try:
            system_prompt = build_system_prompt(organizer)
        except Exception as exc:
            system_prompt = f"You are the Mod Organizer 2 Linux AI Assistant. (Context unavailable: {exc})"
        try:
            cwd = organizer.basePath()
        except Exception:
            cwd = None

        self._turn_cancelled = False
        self._worker = AgentTurnWorker(
            dispatcher=self._dispatcher,
            config=self._config,
            user_text=text,
            session_id=self._thread_id,
            system_prompt=system_prompt,
            cwd=cwd,
            bridge_path=self._bridge_path,
            endpoint_path=self._endpoint_path,
            sqlite_path=self._data_dir / "checkpoints.sqlite",
        )
        self._worker.response_ready.connect(self._on_response_ready)
        self._worker.error_occurred.connect(self._on_error)
        self._worker.finished.connect(self._on_turn_finished)
        self._set_busy(True)
        self._worker.start()

    def _set_busy(self, busy: bool) -> None:
        self.send_btn.setEnabled(not busy)
        self.status_label.setVisible(busy)
        self.cancel_btn.setVisible(busy)
        self.cancel_btn.setEnabled(busy)
        if busy:
            self._turn_started = time.monotonic()
            self._update_busy_text()
            self._busy_timer.start()
        else:
            self._busy_timer.stop()

    def _update_busy_text(self) -> None:
        secs = int(time.monotonic() - self._turn_started)
        verb = "Cancelling" if self._turn_cancelled else "Thinking"
        self.status_label.setText(f"{verb}… {secs} s — {self.provider_combo.currentText()}")

    def _on_cancel_clicked(self) -> None:
        if self._worker is None:
            return
        self._turn_cancelled = True
        self.cancel_btn.setEnabled(False)
        self._worker.cancel()
        self._update_busy_text()

    def _on_response_ready(self, reply: str) -> None:
        if not self._turn_cancelled:
            self._append("<b>AI Assistant:</b>", reply, markdown=True)

    def _on_error(self, err: str) -> None:
        if not self._turn_cancelled:
            self._append_error(err)

    def _on_turn_finished(self) -> None:
        if self._turn_cancelled:
            self._append_note("Cancelled.")
        self._set_busy(False)

    def _on_approval_requested(self, req_id: str, tool_name: str, args: dict, destructive: bool) -> None:
        card = ApprovalCard(req_id, tool_name, args, destructive=destructive, parent=self)

        def _handle_decision(r_id: str, decision: str) -> None:
            self._dispatcher.resolve_approval(r_id, decision)
            card.setParent(None)
            card.deleteLater()

        card.decision_made.connect(_handle_decision)
        self.cards_layout.addWidget(card)
