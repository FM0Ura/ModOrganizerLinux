from __future__ import annotations

import json
import time
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
from ..history.export import approval_line
from ..history.store import Conversation, ConversationStore, Message, session_key
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

# card button -> decision recorded on the approval line
_CARD_DECISIONS = {"approve": "approved", "approve_session": "approved_session", "reject": "rejected"}
_ARGS_SUMMARY_LIMIT = 120
_LATE_REPLY_NOTE = "Reply received after cancelling (the assistant remembers it):\n\n"


def summarise_call(tool_name: str, args: dict) -> str:
    """'tool(args)' with the arguments as compact JSON, cut to 120 characters."""
    text = json.dumps(args, ensure_ascii=False, separators=(",", ":"), default=str)
    if len(text) > _ARGS_SUMMARY_LIMIT:
        text = text[: _ARGS_SUMMARY_LIMIT - 1] + "…"
    return f"{tool_name}({text})"


_HEADERS = {
    "user": "<b>You:</b>",
    "assistant": "<b>AI Assistant:</b>",
    "note": "<i style='color:gray;'>Note</i>",
    "error": "<b style='color:#d9534f;'>Error:</b>",
}


class ChatPanel(QWidget):
    def __init__(
        self,
        dispatcher: MainThreadDispatcher,
        mcp_server: LocalMcpServer,
        data_dir: Path,
        parent=None,
        instance_path: str | None = None,
    ) -> None:
        super().__init__(parent)
        self._dispatcher = dispatcher
        self._mcp_server = mcp_server
        self._data_dir = Path(data_dir)
        self._config = AssistantConfig.load(self._data_dir)
        self._instance_path = instance_path
        self._store: ConversationStore | None = None
        self.conversation = Conversation.new(self._config.provider)
        self._worker: AgentTurnWorker | None = None
        self._busy = False
        self._turn_cancelled = False
        # req_id -> (card, tool name, args) for the approval cards still on screen
        self._pending_cards: dict[str, tuple[ApprovalCard, str, dict]] = {}
        self._turn_started = 0.0
        self._bridge_path, self._endpoint_path = prepare_bridge(
            self._data_dir, mcp_server.endpoint_url(), mcp_server.token
        )

        self._dispatcher.set_permission_mode(self._config.permission_mode)
        self._dispatcher.approval_requested.connect(self._on_approval_requested)
        self._dispatcher.write_executed.connect(self._on_write_executed)

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
        # a conversation is bound to its provider: another provider starts a new one
        provider = self.provider_combo.currentData()
        self.new_conversation(provider)
        if provider == "agy_cli":
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
        self.new_conversation()

    def _on_open_settings(self) -> None:
        dlg = AssistantSettingsDialog(
            self._config, self._data_dir, self, bridge_path=self._bridge_path, endpoint_path=self._endpoint_path
        )
        dlg.exec()

    def _select_provider(self, provider: str) -> None:
        """Shows provider in the combo without starting a new conversation, and saves it."""
        idx = self.provider_combo.findData(provider)
        if idx >= 0 and idx != self.provider_combo.currentIndex():
            self.provider_combo.blockSignals(True)
            try:
                self.provider_combo.setCurrentIndex(idx)
            finally:
                self.provider_combo.blockSignals(False)
        if self._config.provider != provider:
            self._config.provider = provider
            self._save_config()

    # --- conversations ------------------------------------------------------------------

    @property
    def store(self) -> ConversationStore:
        # created on first use; it writes nothing until a conversation with a user message is saved
        if self._store is None:
            path = self._instance_path
            if path is None:
                path = self._dispatcher._organizer.basePath()
            self._store = ConversationStore(self._data_dir, path)
        return self._store

    def is_busy(self) -> bool:
        return self._busy

    def _save_conversation(self) -> bool:
        try:
            return self.store.save(self.conversation)
        except OSError as exc:
            # drawn but not recorded: recording it would try to save again
            self._append(_HEADERS["error"], f"Could not save the conversation: {exc}", markdown=False)
            return False

    def _stash_draft(self) -> bool:
        """Saves the current conversation with the input box as its draft."""
        self.conversation.draft = self.input_box.toPlainText()
        return self._save_conversation()

    def new_conversation(self, provider: str | None = None) -> None:
        if self.is_busy():
            return
        saved = self._stash_draft()
        provider = provider or self._config.provider
        self.conversation = Conversation.new(provider)
        self._select_provider(provider)
        self._dispatcher.reset_session_approvals()
        self.transcript.clear()
        if saved:
            # the draft went with the saved conversation; an unsaved one keeps it in the box
            self.input_box.clear()

    def open_conversation(self, conv_id: str) -> bool:
        if self.is_busy():
            return False
        self._stash_draft()
        conv = self.store.load(conv_id)
        if conv is None:
            return False
        self.conversation = conv
        self._select_provider(conv.provider)
        self.render_transcript()
        self.input_box.setPlainText(conv.draft)
        self._dispatcher.reset_session_approvals()
        return True

    def open_most_recent(self) -> None:
        if self.is_busy():
            return
        conv = self.store.most_recent()
        if conv is not None:
            self.open_conversation(conv.id)

    def _turn_number(self) -> int:
        return sum(1 for m in self.conversation.messages if m.role == "user")

    # --- transcript ---------------------------------------------------------------------

    def _record(self, role: str, text: str, meta: dict | None = None) -> None:
        """Adds a message to the conversation, draws it and saves the conversation."""
        msg = self.conversation.add(role, text, meta)
        self._draw(msg)
        self._save_conversation()

    def render_transcript(self) -> None:
        self.transcript.clear()
        for msg in self.conversation.messages:
            self._draw(msg)

    def _draw(self, msg: Message) -> None:
        if msg.role == "approval":
            self._append("", approval_line(msg.text, (msg.meta or {}).get("decision", "")), markdown=False)
        else:
            self._append(_HEADERS.get(msg.role, ""), msg.text, markdown=msg.role == "assistant")

    def _append(self, header_html: str, body: str, markdown: bool) -> None:
        """Appends a block: header_html is trusted markup, body is shown as text or Markdown."""
        cursor = self.transcript.textCursor()
        cursor.movePosition(QTextCursor.MoveOperation.End)
        if not self.transcript.document().isEmpty():
            cursor.insertBlock()
        if header_html:
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
        self._record("note", text)

    def _append_error(self, text: str) -> None:
        self._record("error", text)

    # --- turns ----------------------------------------------------------------------------

    def _on_send_clicked(self) -> None:
        text = self.input_box.toPlainText().strip()
        if not text:
            return
        self.input_box.clear()
        self.send_prompt(text)

    def send_prompt(self, text: str) -> None:
        if self.is_busy():
            return
        provider = self.conversation.provider
        history = None
        if provider == "agy_cli":
            history = [(m.role, m.text) for m in self.conversation.messages if m.role in ("user", "assistant")]
        self.conversation.draft = ""
        self._record("user", text)

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
            # None on the first turn: the worker creates (or learns) the id and emits session_started
            session_id=self.conversation.session.get(session_key(provider)),
            system_prompt=system_prompt,
            cwd=cwd,
            bridge_path=self._bridge_path,
            endpoint_path=self._endpoint_path,
            sqlite_path=self._data_dir / "checkpoints.sqlite",
            history=history,
        )
        self._worker.session_started.connect(self._on_session_started)
        self._worker.response_ready.connect(self._on_response_ready)
        self._worker.note_ready.connect(self._on_worker_note)
        self._worker.error_occurred.connect(self._on_error)
        self._worker.late_response.connect(self._on_late_response)
        self._worker.finished.connect(self._on_turn_finished)
        self._set_busy(True)
        self._worker.start()

    def _set_busy(self, busy: bool) -> None:
        self._busy = busy
        self.send_btn.setEnabled(not busy)
        # the conversation can't change under a running turn
        self.provider_combo.setEnabled(not busy)
        self.clear_btn.setEnabled(not busy)
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
        self._cancel_pending_cards()
        self._update_busy_text()

    def _on_session_started(self, session_id: str) -> None:
        self.conversation.session[session_key(self.conversation.provider)] = session_id
        self._save_conversation()

    def _on_response_ready(self, reply: str) -> None:
        if not self._turn_cancelled:
            self._record("assistant", reply)

    def _on_worker_note(self, text: str) -> None:
        self._record("note", text)

    def _on_error(self, err: str) -> None:
        if not self._turn_cancelled:
            self._append_error(err)

    def _on_late_response(self, reply: str) -> None:
        self._record("note", _LATE_REPLY_NOTE + reply)

    def _on_turn_finished(self) -> None:
        self._cancel_pending_cards()
        if self._turn_cancelled:
            self._append_note("Cancelled.")
        self._set_busy(False)

    # --- approvals --------------------------------------------------------------------------

    def _record_approval(self, tool_name: str, args: dict, decision: str) -> None:
        self._record(
            "approval",
            summarise_call(tool_name, args),
            meta={"tool": tool_name, "args": args, "decision": decision},
        )

    def _on_approval_requested(self, req_id: str, tool_name: str, args: dict, destructive: bool) -> None:
        card = ApprovalCard(
            req_id, tool_name, args, destructive=destructive, parent=self, turn=self._turn_number()
        )
        self._pending_cards[req_id] = (card, tool_name, dict(args))
        card.decision_made.connect(self._on_card_decision)
        self.cards_layout.addWidget(card)

    def _take_card(self, req_id: str) -> tuple[str, dict] | None:
        entry = self._pending_cards.pop(req_id, None)
        if entry is None:
            return None
        card, tool_name, args = entry
        card.setParent(None)
        card.deleteLater()
        return tool_name, args

    def _on_card_decision(self, req_id: str, decision: str) -> None:
        self._dispatcher.resolve_approval(req_id, decision)
        taken = self._take_card(req_id)
        if taken is not None:
            self._record_approval(*taken, _CARD_DECISIONS.get(decision, "rejected"))

    def _cancel_pending_cards(self) -> None:
        """Rejects the cards left when a turn ends or is cancelled."""
        for req_id in list(self._pending_cards):
            self._dispatcher.resolve_approval(req_id, "reject")
            taken = self._take_card(req_id)
            if taken is not None:
                self._record_approval(*taken, "cancelled")

    def _on_write_executed(self, tool_name: str, args: dict, decision: str) -> None:
        # approved / approved_session writes already have the line of the card that allowed them
        if decision == "auto":
            self._record_approval(tool_name, dict(args), "auto")
