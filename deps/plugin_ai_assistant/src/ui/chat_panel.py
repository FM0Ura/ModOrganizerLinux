from __future__ import annotations

import json
import time
from pathlib import Path
from PyQt6 import sip
from PyQt6.QtCore import QEvent, QObject, Qt, QTimer
from PyQt6.QtGui import QTextCursor, QTextDocument
from PyQt6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QComboBox,
    QLabel,
    QFileDialog,
    QPushButton,
    QStackedWidget,
    QTextBrowser,
    QPlainTextEdit,
)
from ..config import AssistantConfig
from ..tools.dispatcher import MainThreadDispatcher
from ..agent.cli_runner import prepare_bridge
from ..agent.graph import build_system_prompt
from ..agent.mcp_server import LocalMcpServer
from ..agent.worker import AgentTurnWorker
from ..history.export import approval_line, to_markdown
from ..history.store import Conversation, ConversationStore, Message, session_key
from .approval_card import ApprovalCard
from .history_view import HistoryView
from .queue_view import QueueView
from .settings_dialog import AssistantSettingsDialog
from .slash_commands import HELP_TEXT, PROVIDER_ALIASES, SlashCommand, parse_slash

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
_INTERRUPTED_NOTE = "Turn interrupted: MO2 was closed."
_SHUTDOWN_WAIT_MS = 3000


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
        self._queue: list[str] = []
        self._composing = False
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

        self.clear_btn = QPushButton("+ New")
        self.clear_btn.clicked.connect(self._on_new_chat)
        self.history_btn = QPushButton("History")
        self.history_btn.clicked.connect(self.show_history)
        self.settings_btn = QPushButton("Settings")
        self.settings_btn.clicked.connect(self._on_open_settings)

        top_bar.addWidget(self.provider_combo, 2)
        top_bar.addWidget(self.mode_combo, 2)
        top_bar.addWidget(self.clear_btn)
        top_bar.addWidget(self.history_btn)
        top_bar.addWidget(self.settings_btn)
        root.addLayout(top_bar)

        # the chat and the history screen share the panel below the top bar
        self.stack = QStackedWidget()
        self.chat_page = QWidget()
        chat_layout = QVBoxLayout(self.chat_page)
        chat_layout.setContentsMargins(0, 0, 0, 0)
        self.stack.addWidget(self.chat_page)
        self.history_view = HistoryView(lambda: self.store)
        self.history_view.open_requested.connect(self._on_history_open)
        self.history_view.back_requested.connect(self._show_chat)
        self.history_view.export_requested.connect(self.export_conversation)
        self.history_view.delete_requested.connect(self._on_history_delete)
        self.history_view.rename_requested.connect(self._on_history_rename)
        self.stack.addWidget(self.history_view)
        root.addWidget(self.stack, 1)

        # Transcript area + approval container
        self.transcript = QTextBrowser()
        self.transcript.setOpenExternalLinks(True)
        chat_layout.addWidget(self.transcript, 1)

        self.cards_layout = QVBoxLayout()
        chat_layout.addLayout(self.cards_layout)

        # messages typed while a turn runs
        self.queue_view = QueueView(self)
        self.queue_view.remove_requested.connect(self.remove_queued)
        chat_layout.addWidget(self.queue_view)

        # Busy row: shown while a turn runs
        busy_row = QHBoxLayout()
        self.status_label = QLabel()
        self.cancel_btn = QPushButton("Cancel")
        self.cancel_btn.clicked.connect(self._on_cancel_clicked)
        busy_row.addWidget(self.status_label, 1)
        busy_row.addWidget(self.cancel_btn)
        chat_layout.addLayout(busy_row)
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
            btn.clicked.connect(lambda _=False, p=prompt: self.submit(p))
            chips.addWidget(btn)
        chat_layout.addLayout(chips)

        # Input row
        input_row = QHBoxLayout()
        self.input_box = QPlainTextEdit()
        self.input_box.setPlaceholderText("Ask the AI Assistant to analyze logs, toggle mods, install archives...")
        self.input_box.setMaximumHeight(75)
        self.input_box.installEventFilter(self)
        self.send_btn = QPushButton("Send")
        self.send_btn.clicked.connect(self._on_send_clicked)
        input_row.addWidget(self.input_box, 1)
        input_row.addWidget(self.send_btn)
        chat_layout.addLayout(input_row)

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

    def save_draft(self) -> bool:
        """Saves the input box and the queue as the draft of the current conversation."""
        return self._stash_draft()

    def _stash_draft(self) -> bool:
        """Saves the current conversation with the input box and the queue as its draft."""
        self.conversation.draft = self.draft_text()
        return self._save_conversation()

    # --- input and queue ----------------------------------------------------------------

    @property
    def queue(self) -> list[str]:
        return list(self._queue)

    def draft_text(self) -> str:
        """The input box and the queued messages, in order, joined by blank lines."""
        parts = [self.input_box.toPlainText(), *self._queue]
        return "\n\n".join(part for part in parts if part.strip())

    def eventFilter(self, obj: QObject, event: QEvent) -> bool:
        if obj is self.input_box:
            kind = event.type()
            if kind == QEvent.Type.InputMethod:
                self._composing = bool(event.preeditString())
            elif kind == QEvent.Type.FocusOut:
                self._composing = False  # a preedit abandoned on focus loss must not block Enter
            elif kind == QEvent.Type.KeyPress and event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
                if not event.modifiers() & Qt.KeyboardModifier.ShiftModifier and not self._composing:
                    self._on_send_clicked()
                    return True
        return super().eventFilter(obj, event)

    def _queue_changed(self) -> None:
        self.queue_view.set_items(self._queue)
        self.conversation.draft = self.draft_text()
        self._save_conversation()

    def enqueue(self, text: str) -> None:
        text = text.strip()
        if text:
            self._queue.append(text)
            self._queue_changed()

    def remove_queued(self, index: int) -> None:
        if 0 <= index < len(self._queue):
            del self._queue[index]
            self._queue_changed()

    def _clear_queue(self) -> None:
        """Empties the queue without touching the draft: it belongs to the conversation being left."""
        self._queue.clear()
        self.queue_view.set_items([])

    def submit(self, text: str) -> None:
        """Single entry point for user text: runs a slash command, sends it, or queues it while a turn runs."""
        text = text.strip()
        if not text:
            return
        parsed = parse_slash(text)
        if isinstance(parsed, SlashCommand) and parsed.name == "help":
            self._append_note(HELP_TEXT)  # always immediate, even during a turn
        elif self.is_busy() and self._turn_cancelled:
            # a cancelled turn drops its queue: new text goes back to the box instead of being stranded
            current = self.input_box.toPlainText()
            self.input_box.setPlainText("\n\n".join(part for part in (current, text) if part.strip()))
        elif self.is_busy():
            self.enqueue(text)
        else:
            self._run_text(text)

    def _run_text(self, text: str) -> bool:
        """Runs a command or sends the text; True when it started a turn."""
        parsed = parse_slash(text)
        if parsed is None:
            self.send_prompt(text)
            return self.is_busy()
        if isinstance(parsed, str):
            self.send_prompt(parsed)
            return self.is_busy()
        self._run_command(parsed)
        return False

    def _run_command(self, cmd: SlashCommand) -> None:
        name, arg = cmd.name, cmd.arg
        if name == "help":
            self._append_note(HELP_TEXT)
        elif name == "new":
            self.new_conversation()
        elif name == "history":
            self.show_history()
        elif name == "export":
            self.export_current()
        elif name == "mode":
            self._command_mode(arg.lower())
        elif name == "provider":
            self._command_provider(arg.lower())
        else:
            self._append_note("Unknown command; /help lists the commands.")

    def _command_mode(self, arg: str) -> None:
        if not arg:
            self._append_note(f"Permission mode: {self._config.permission_mode} (safe or autonomous).")
        elif arg in ("safe", "autonomous"):
            self.mode_combo.setCurrentIndex(self.mode_combo.findData(arg))  # runs _on_mode_changed
            self._append_note(f"Permission mode set to {arg}.")
        else:
            self._append_note(f"Unknown mode '{arg}'; use safe or autonomous.")

    def _command_provider(self, arg: str) -> None:
        names = ", ".join(PROVIDER_ALIASES)
        if not arg:
            current = next((k for k, v in PROVIDER_ALIASES.items() if v == self.conversation.provider), "")
            self._append_note(f"Current provider: {current}. Available: {names}.")
        elif arg in PROVIDER_ALIASES and self.provider_combo.findData(PROVIDER_ALIASES[arg]) >= 0:
            idx = self.provider_combo.findData(PROVIDER_ALIASES[arg])
            if idx == self.provider_combo.currentIndex():
                self.new_conversation(PROVIDER_ALIASES[arg])
            else:
                self.provider_combo.setCurrentIndex(idx)  # runs _on_provider_changed: new conversation
        else:
            self._append_note(f"Unknown provider '{arg}'. Available: {names}.")

    # --- history screen and export ----------------------------------------------------------

    def show_history(self) -> None:
        if self.is_busy():
            return
        self.history_view.refresh()
        self.stack.setCurrentWidget(self.history_view)

    def _show_chat(self) -> None:
        self.stack.setCurrentWidget(self.chat_page)

    def _tell(self, text: str, error: bool = False) -> None:
        """Feedback for an action: in the history screen when it is showing, else in the transcript."""
        if self.stack.currentWidget() is self.history_view:
            self.history_view.show_message(text)
        elif error:
            self._append_error(text)
        else:
            # drawn only: an export confirmation is not part of the conversation
            self._append(_HEADERS["note"], text, markdown=False)

    def _on_history_open(self, conv_id: str) -> None:
        if self.open_conversation(conv_id):
            self._show_chat()
        else:
            self.history_view.show_message("Could not open that conversation.")

    def _on_history_rename(self, conv_id: str, title: str) -> None:
        title = " ".join(title.split())
        if not title:
            return
        if conv_id == self.conversation.id:
            # the next save of the open conversation must not overwrite the new title
            self.conversation.title = title
            self.conversation.title_custom = True
            self._save_conversation()  # a conversation without a user message is not stored
        self.store.rename(conv_id, title)
        self.history_view.refresh()

    def _on_history_delete(self, conv_id: str) -> None:
        is_open = conv_id == self.conversation.id
        if is_open and self.is_busy():
            self.history_view.show_message("Wait for the running turn to finish first.")
            return
        self.store.delete(conv_id)
        if is_open:
            # nothing of the deleted conversation may be saved again: no draft stash here, but
            # what is typed or queued right now is the user's and moves to the new conversation
            typed = self.draft_text()
            self._cancel_pending_cards_silently()
            self._clear_queue()
            self.conversation = Conversation.new(self._config.provider)
            self._select_provider(self.conversation.provider)
            self._dispatcher.reset_session_approvals()
            self.transcript.clear()
            self.input_box.setPlainText(typed)
        self.history_view.refresh()

    def _cancel_pending_cards_silently(self) -> None:
        for req_id in list(self._pending_cards):
            self._dispatcher.resolve_approval(req_id, "reject")
            self._take_card(req_id)

    def export_current(self) -> None:
        if not self.conversation.has_user_message():
            self._append_note("Nothing to export yet.")
            return
        self.export_conversation(self.conversation.id)

    def export_conversation(self, conv_id: str, path: str | None = None) -> None:
        conv = self.conversation if conv_id == self.conversation.id else self.store.load(conv_id)
        if conv is None:
            self._tell("Could not export that conversation.", error=True)
            return
        if path is None:
            name = " ".join((conv.title or "Conversation").split())
            name = "".join("_" if ch in '/\\:*?"<>|' else ch for ch in name)
            path, _ = QFileDialog.getSaveFileName(
                self, "Export conversation", f"{name}.md", "Markdown (*.md);;All files (*)"
            )
            if not path:
                return
        try:
            Path(path).write_text(to_markdown(conv), encoding="utf-8")
        except OSError as exc:
            self._tell(f"Could not export the conversation: {exc}", error=True)
            return
        self._tell(f"Exported to {path}")

    def new_conversation(self, provider: str | None = None) -> None:
        if self.is_busy():
            return
        # cards belong to the conversation being left: an approval must not carry over
        self._cancel_pending_cards()
        draft = self.draft_text()
        saved = self._stash_draft()
        self._clear_queue()
        provider = provider or self._config.provider
        self.conversation = Conversation.new(provider)
        self._select_provider(provider)
        self._dispatcher.reset_session_approvals()
        self.transcript.clear()
        if saved:
            # the draft went with the saved conversation; an unsaved one keeps it in the box
            self.input_box.clear()
        else:
            self.input_box.setPlainText(draft)

    def open_conversation(self, conv_id: str) -> bool:
        if self.is_busy() or self.store.load(conv_id) is None:
            return False  # panel unchanged
        # cards belong to the conversation being left: an approval must not carry over
        self._cancel_pending_cards()
        typed = self.draft_text()
        saved = self._stash_draft()
        self._clear_queue()
        conv = self.store.load(conv_id)  # loaded again: it may be the conversation just saved
        if conv is None:
            return False
        self.conversation = conv
        self._select_provider(conv.provider)
        self.render_transcript()
        draft = conv.draft
        if not saved:
            # the text typed in an unsaved conversation is stored nowhere else: keep it
            draft = "\n\n".join(part for part in (conv.draft, typed) if part)
        self.input_box.setPlainText(draft)
        self._dispatcher.reset_session_approvals()
        return True

    def open_most_recent(self) -> None:
        if self.is_busy():
            return
        conv = self.store.most_recent()
        if conv is not None:
            self.open_conversation(conv.id)

    # --- lifecycle --------------------------------------------------------------------------

    def focus_input(self) -> None:
        self._show_chat()
        self.input_box.setFocus(Qt.FocusReason.ShortcutFocusReason)

    def shutdown(self) -> None:
        """MO2 is closing or changing instance: stops the turn and saves what was typed.

        Only Python state, the worker and files: it runs from aboutToQuit, where Qt GL and
        QtWebEngine objects must not be touched. Safe to call more than once.
        """
        interrupted = self.is_busy()
        worker = self._worker
        if interrupted and worker is not None:
            # whatever the stopped turn still emits belongs to no conversation
            for signal in (
                worker.session_started,
                worker.response_ready,
                worker.note_ready,
                worker.error_occurred,
                worker.late_response,
                worker.finished,
            ):
                try:
                    signal.disconnect()
                except (TypeError, RuntimeError):
                    pass
            self._turn_cancelled = True
            worker.cancel()  # a CLI turn is killed; an API turn is abandoned
        # a turn that outlives this run must not reach its organizer: every later tool call
        # fails, and the calls and approvals it waits for are released
        self._dispatcher.close()
        self._cancel_pending_cards()
        if interrupted:
            self._set_busy(False)
            if worker is not None:
                # a killed CLI ends at once; an API turn may not
                worker.wait(_SHUTDOWN_WAIT_MS)
                if worker.isRunning():
                    # a QThread freed by Python while running aborts MO2, and this panel's
                    # Python object goes away when the plugin is unloaded: Qt owns it now
                    # and deletes it when its run() ends
                    sip.transferto(worker, None)
                    worker.finished.connect(worker.deleteLater)
                self._worker = None
            self._append_note(_INTERRUPTED_NOTE)
        self._turn_cancelled = False
        if self._stash_draft():
            # the draft is in the saved conversation: nothing is left for the next one
            self._clear_queue()
            self.input_box.clear()

    def switch_instance(self, instance_path: str) -> None:
        """Leaves the conversation of the current instance and opens the history of another."""
        self.shutdown()
        self._dispatcher.reopen()  # same dispatcher and organizer object for the new run
        self._instance_path = instance_path
        self._store = None
        # what is left was saved nowhere (no user message yet, or the save failed): keep it
        typed = self.draft_text()
        self._clear_queue()
        self.input_box.setPlainText(typed)
        self.conversation = Conversation.new(self._config.provider)
        self._select_provider(self.conversation.provider)
        self.transcript.clear()
        self._show_chat()
        self._dispatcher.reset_session_approvals()
        self.open_most_recent()

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
        self.submit(text)

    def send_prompt(self, text: str) -> None:
        if self.is_busy():
            self.enqueue(text)
            return
        provider = self.conversation.provider
        history = None
        if provider == "agy_cli":
            history = [(m.role, m.text) for m in self.conversation.messages if m.role in ("user", "assistant")]
        # what is still typed stays as the draft; nothing left means it is cleared
        self.conversation.draft = self.draft_text()
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
        if self._worker is not None:
            # the previous QThread must really have ended before its last reference is dropped
            self._worker.wait()
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
        self.history_btn.setEnabled(not busy)
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
        # the queued messages go back to the box rather than being sent after the turn
        text = self.draft_text()
        self._clear_queue()
        self.input_box.setPlainText(text)
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
        if self._turn_cancelled:
            if self._queue:
                self.input_box.setPlainText(self.draft_text())
                self._clear_queue()
        elif self._queue:
            # not inside the finished signal of the thread that is ending
            QTimer.singleShot(0, self._drain_queue)

    def _drain_queue(self) -> None:
        if self._queue and not self.is_busy():
            # queued commands run as commands; a command does not start a turn, so keep going
            while self._queue and not self.is_busy():
                text = self._queue.pop(0)
                self.queue_view.set_items(self._queue)
                if isinstance(parse_slash(text), SlashCommand):
                    # a command (/new, /provider) must not swallow what was queued after it
                    rest, self._queue = self._queue, []
                    try:
                        self._run_text(text)
                    finally:
                        self._queue = rest + self._queue
                    if rest:
                        self._queue_changed()
                else:
                    self._run_text(text)

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
