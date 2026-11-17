"""The chat panel is driven by a persisted Conversation (Task 5 of the F1 plan)."""

from __future__ import annotations

import json
import uuid

import pytest
from PyQt6.QtCore import QObject, pyqtSignal

from ai_assistant.agent.mcp_server import LocalMcpServer
from ai_assistant.history.export import to_markdown
from ai_assistant.history.store import ConversationStore
from ai_assistant.tools import MainThreadDispatcher, build_default_registry
from ai_assistant.ui import chat_panel as chat_panel_mod
from ai_assistant.ui.approval_card import ApprovalCard
from ai_assistant.ui.chat_panel import ChatPanel


class FakeWorker(QObject):
    """Stands in for AgentTurnWorker: no CLI or API is ever called."""

    session_started = pyqtSignal(str)
    response_ready = pyqtSignal(str)
    note_ready = pyqtSignal(str)
    error_occurred = pyqtSignal(str)
    late_response = pyqtSignal(str)
    finished = pyqtSignal()
    instances: list["FakeWorker"] = []

    def __init__(self, **kwargs):
        super().__init__()
        self.kwargs = kwargs
        self.cancelled = False
        self.running = False
        FakeWorker.instances.append(self)

    def start(self):
        self.running = True
        # like the real worker: Claude/APIs get a fresh id before the turn runs
        if self.kwargs["session_id"] is None and self.kwargs["config"].provider not in ("codex_cli", "agy_cli"):
            self.session_started.emit(str(uuid.uuid4()))

    def isRunning(self):
        return self.running

    def cancel(self):
        self.cancelled = True

    def finish(self, reply: str | None = None):
        if reply is not None:
            self.response_ready.emit(reply)
        self.running = False
        self.finished.emit()


@pytest.fixture
def env(fake_organizer, tmp_path, monkeypatch):
    FakeWorker.instances.clear()
    monkeypatch.setattr(chat_panel_mod, "AgentTurnWorker", FakeWorker)
    disp = MainThreadDispatcher(
        fake_organizer, build_default_registry(), permission_mode="safe", vfs_mounted_check=lambda: False
    )
    srv = LocalMcpServer(disp)
    srv.start()
    data_dir = tmp_path / "data"
    panels = []

    def make_panel():
        p = ChatPanel(disp, srv, data_dir)
        panels.append(p)
        return p

    yield make_panel, disp, data_dir
    srv.stop()


def _store(data_dir, fake_organizer) -> ConversationStore:
    return ConversationStore(data_dir, fake_organizer.basePath())


def _turn(panel, text, reply):
    panel.send_prompt(text)
    worker = FakeWorker.instances[-1]
    worker.finish(reply)
    return worker


def test_turn_persists_messages_and_session(env, fake_organizer):
    make_panel, _, data_dir = env
    panel = make_panel()
    _turn(panel, "olá", "hi there")

    saved = _store(data_dir, fake_organizer).load(panel.conversation.id)
    assert saved is not None
    assert [(m.role, m.text) for m in saved.messages] == [("user", "olá"), ("assistant", "hi there")]
    assert saved.session["claude_session_id"]
    assert saved.provider == "claude_cli"


def test_first_turn_has_no_session_and_second_turn_passes_it(env):
    make_panel, _, _ = env
    panel = make_panel()
    first = _turn(panel, "one", "1")
    assert first.kwargs["session_id"] is None
    sid = panel.conversation.session["claude_session_id"]

    second = _turn(panel, "two", "2")
    assert second.kwargs["session_id"] == sid
    assert second.kwargs["history"] is None


def test_codex_session_id_arrives_later(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.new_conversation("codex_cli")
    panel.send_prompt("one")
    worker = FakeWorker.instances[-1]
    assert worker.kwargs["session_id"] is None
    worker.session_started.emit("codex-thread-1")
    worker.finish("1")
    assert panel.conversation.session == {"codex_session_id": "codex-thread-1"}
    assert _turn(panel, "two", "2").kwargs["session_id"] == "codex-thread-1"


def test_agy_gets_prior_history(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.new_conversation("agy_cli")
    _turn(panel, "first question", "first answer")
    worker = _turn(panel, "second question", "second answer")
    assert worker.kwargs["history"] == [("user", "first question"), ("assistant", "first answer")]


def test_reopen_after_restart(env, fake_organizer):
    make_panel, _, _ = env
    panel = make_panel()
    _turn(panel, "remember the number 42", "noted")
    sid = panel.conversation.session["claude_session_id"]
    conv_id = panel.conversation.id

    again = make_panel()
    again.open_most_recent()
    assert again.conversation.id == conv_id
    text = again.transcript.toPlainText()
    assert "remember the number 42" in text and "noted" in text
    assert _turn(again, "which number?", "42").kwargs["session_id"] == sid


def test_open_most_recent_with_nothing_saved_keeps_empty_panel(env):
    make_panel, _, _ = env
    panel = make_panel()
    before = panel.conversation.id
    panel.open_most_recent()
    assert panel.conversation.id == before
    assert panel.transcript.toPlainText() == ""


def test_provider_change_starts_new_conversation(env, fake_organizer):
    make_panel, _, data_dir = env
    panel = make_panel()
    _turn(panel, "hello", "hi")
    old_id = panel.conversation.id

    panel.provider_combo.setCurrentIndex(panel.provider_combo.findData("gemini"))
    assert panel.conversation.id != old_id
    assert panel.conversation.provider == "gemini"
    assert panel.transcript.toPlainText() == ""
    assert _store(data_dir, fake_organizer).load(old_id) is not None

    panel.provider_combo.setCurrentIndex(panel.provider_combo.findData("agy_cli"))
    notes = [m for m in panel.conversation.messages if m.role == "note"]
    assert notes and "Antigravity" in notes[0].text
    assert "Antigravity" in panel.transcript.toPlainText()


def test_open_conversation_selects_provider_without_new(env, fake_organizer):
    make_panel, _, _ = env
    panel = make_panel()
    panel.new_conversation("codex_cli")
    _turn(panel, "codex chat", "ok")
    codex_id = panel.conversation.id
    panel.new_conversation("claude_cli")
    _turn(panel, "claude chat", "ok")

    calls = []
    original = panel.new_conversation
    panel.new_conversation = lambda *a, **k: (calls.append(a), original(*a, **k))
    assert panel.open_conversation(codex_id) is True
    assert calls == []
    assert panel.conversation.id == codex_id
    assert panel.provider_combo.currentData() == "codex_cli"
    assert panel._config.provider == "codex_cli"
    assert "codex chat" in panel.transcript.toPlainText()
    assert "claude chat" not in panel.transcript.toPlainText()


def test_open_conversation_draft_round_trip(env, fake_organizer):
    make_panel, _, data_dir = env
    panel = make_panel()
    _turn(panel, "first", "ok")
    first_id = panel.conversation.id
    panel.input_box.setPlainText("half-written idea")
    panel.new_conversation()
    assert panel.input_box.toPlainText() == ""
    assert _store(data_dir, fake_organizer).load(first_id).draft == "half-written idea"

    assert panel.open_conversation(first_id) is True
    assert panel.input_box.toPlainText() == "half-written idea"


def test_open_unknown_or_unreadable_conversation_leaves_panel_unchanged(env, fake_organizer):
    make_panel, _, data_dir = env
    panel = make_panel()
    _turn(panel, "keep me", "ok")
    current = panel.conversation
    # a stale index entry: listed, but its file is gone
    stale = _store(data_dir, fake_organizer)
    stale._path(current.id).with_name("ghost.json").write_text("not json")
    assert panel.open_conversation("ghost") is False
    assert panel.open_conversation("missing") is False
    assert panel.conversation is current
    assert "keep me" in panel.transcript.toPlainText()


def test_reopen_resets_session_approvals(env):
    make_panel, disp, _ = env
    panel = make_panel()
    _turn(panel, "hello", "hi")
    conv_id = panel.conversation.id

    disp._session_approved_all = True
    assert panel.open_conversation(conv_id) is True
    assert disp._session_approved_all is False

    disp._session_approved_all = True
    panel.new_conversation()
    assert disp._session_approved_all is False


def test_new_and_open_are_refused_during_a_turn(env):
    make_panel, _, _ = env
    panel = make_panel()
    _turn(panel, "hello", "hi")
    conv_id = panel.conversation.id
    panel.new_conversation()
    panel.send_prompt("busy now")
    busy_id = panel.conversation.id
    assert panel.is_busy()
    panel.new_conversation()
    assert panel.open_conversation(conv_id) is False
    assert panel.conversation.id == busy_id
    assert not panel.provider_combo.isEnabled() and not panel.clear_btn.isEnabled()
    FakeWorker.instances[-1].finish("done")
    assert not panel.is_busy()
    assert panel.provider_combo.isEnabled() and panel.clear_btn.isEnabled()


def _cards(panel):
    return panel.findChildren(ApprovalCard)


def test_approval_lines_recorded(env, fake_organizer):
    make_panel, disp, data_dir = env
    panel = make_panel()
    resolved = []
    disp.resolve_approval = lambda r, d: resolved.append((r, d))
    panel.send_prompt("do things")

    disp.approval_requested.emit("r1", "set_mod_active", {"mod_name": "SkyUI", "active": False}, False)
    disp.approval_requested.emit("r2", "remove_mod", {"mod_name": "Lux"}, True)
    disp.approval_requested.emit("r3", "set_mod_active", {"mod_name": "USSEP", "active": True}, False)
    cards = {c.request_id: c for c in _cards(panel)}
    assert "turn 1" in cards["r1"].title_label.text()
    cards["r1"].approve_btn.click()
    cards["r2"].reject_btn.click()
    cards["r3"].approve_all_btn.click()
    assert resolved == [("r1", "approve"), ("r2", "reject"), ("r3", "approve_session")]
    assert _cards(panel) == []

    # writes that ran: only "auto" gets a line of its own
    disp.write_executed.emit("set_mod_active", {"mod_name": "SkyUI", "active": False}, "approved")
    disp.write_executed.emit("set_mod_active", {"mod_name": "USSEP", "active": True}, "approved_session")
    disp.write_executed.emit("refresh", {}, "auto")
    FakeWorker.instances[-1].finish("done")

    lines = [m for m in panel.conversation.messages if m.role == "approval"]
    assert [m.meta["decision"] for m in lines] == ["approved", "rejected", "approved_session", "auto"]
    assert lines[0].meta["tool"] == "set_mod_active"
    assert lines[0].meta["args"] == {"mod_name": "SkyUI", "active": False}
    text = panel.transcript.toPlainText()
    assert '✔ Approved: set_mod_active({"mod_name":"SkyUI","active":false})' in text
    assert '✖ Rejected: remove_mod({"mod_name":"Lux"})' in text
    assert '✔ Approved for this conversation: set_mod_active({"mod_name":"USSEP","active":true})' in text
    assert "✔ Automatic: refresh({})" in text

    saved = _store(data_dir, fake_organizer).load(panel.conversation.id)
    assert [m.meta["decision"] for m in saved.messages if m.role == "approval"] == [
        "approved", "rejected", "approved_session", "auto",
    ]
    # the Markdown export shows each decision once
    md = to_markdown(saved)
    assert '✔ Approved: set_mod_active({"mod_name":"SkyUI","active":false})' in md
    assert "✔ Approved: ✔" not in md

    # reopening redraws the same lines
    again = make_panel()
    assert again.open_conversation(panel.conversation.id)
    assert "✔ Automatic: refresh({})" in again.transcript.toPlainText()


def test_long_arguments_are_summarised(env):
    make_panel, disp, _ = env
    panel = make_panel()
    panel.send_prompt("x")
    disp.write_executed.emit("install_mod", {"archive_path": "a" * 500}, "auto")
    line = [m for m in panel.conversation.messages if m.role == "approval"][0]
    inner = line.text[len("install_mod("):-1]
    assert len(inner) <= 120 and inner.endswith("…")
    assert line.meta["args"] == {"archive_path": "a" * 500}


def test_pending_cards_cancelled_at_turn_end(env):
    make_panel, disp, _ = env
    panel = make_panel()
    resolved = []
    disp.resolve_approval = lambda r, d: resolved.append((r, d))
    panel.send_prompt("do things")
    disp.approval_requested.emit("r1", "remove_mod", {"mod_name": "Lux"}, True)
    FakeWorker.instances[-1].finish("done")

    assert resolved == [("r1", "reject")]
    assert _cards(panel) == []
    line = [m for m in panel.conversation.messages if m.role == "approval"][-1]
    assert line.meta["decision"] == "cancelled"
    assert '✖ Cancelled: remove_mod({"mod_name":"Lux"})' in panel.transcript.toPlainText()


def test_pending_cards_cancelled_when_turn_is_cancelled(env):
    make_panel, disp, _ = env
    panel = make_panel()
    resolved = []
    disp.resolve_approval = lambda r, d: resolved.append((r, d))
    panel.send_prompt("do things")
    disp.approval_requested.emit("r1", "remove_mod", {"mod_name": "Lux"}, True)
    panel.cancel_btn.click()
    assert resolved == [("r1", "reject")]
    FakeWorker.instances[-1].finish()
    decisions = [m.meta["decision"] for m in panel.conversation.messages if m.role == "approval"]
    assert decisions == ["cancelled"]
    assert panel.conversation.messages[-1].role == "note"
    assert panel.conversation.messages[-1].text == "Cancelled."


def test_late_response_recorded_as_note(env, fake_organizer):
    make_panel, _, data_dir = env
    panel = make_panel()
    panel.send_prompt("slow question")
    worker = FakeWorker.instances[-1]
    panel.cancel_btn.click()
    worker.late_response.emit("the slow answer")
    worker.finish()

    notes = [m.text for m in panel.conversation.messages if m.role == "note"]
    assert "Reply received after cancelling (the assistant remembers it):\n\nthe slow answer" in notes
    assert "the slow answer" in panel.transcript.toPlainText()
    saved = _store(data_dir, fake_organizer).load(panel.conversation.id)
    assert any("the slow answer" in m.text for m in saved.messages if m.role == "note")


def test_worker_notes_and_errors_are_recorded(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.send_prompt("q")
    worker = FakeWorker.instances[-1]
    worker.note_ready.emit("memory was lost; continuing in a new session")
    worker.error_occurred.emit("boom")
    worker.finish()
    roles = [(m.role, m.text) for m in panel.conversation.messages]
    assert ("note", "memory was lost; continuing in a new session") in roles
    assert ("error", "boom") in roles


def test_render_transcript_redraws_from_messages(env):
    make_panel, _, _ = env
    panel = make_panel()
    _turn(panel, "<b>raw</b>", "**bold**")
    panel.transcript.clear()
    panel.render_transcript()
    text = panel.transcript.toPlainText()
    assert "<b>raw</b>" in text and "bold" in text and "**" not in text


def test_nothing_is_written_before_the_first_user_message(env, fake_organizer):
    make_panel, _, data_dir = env
    panel = make_panel()
    panel.new_conversation("agy_cli")  # adds a note, but no user message yet
    assert not (data_dir / "conversations").exists()


def test_open_conversation_keeps_text_typed_in_an_unsaved_conversation(env):
    make_panel, _, _ = env
    panel = make_panel()
    _turn(panel, "with draft", "ok")
    with_draft = panel.conversation.id
    panel.input_box.setPlainText("saved draft")
    panel.new_conversation()
    _turn(panel, "no draft", "ok")
    without_draft = panel.conversation.id

    panel.new_conversation()
    panel.input_box.setPlainText("typed in an empty one")
    assert panel.open_conversation(without_draft) is True
    assert panel.input_box.toPlainText() == "typed in an empty one"

    panel.new_conversation()
    panel.input_box.setPlainText("typed again")
    assert panel.open_conversation(with_draft) is True
    assert panel.input_box.toPlainText() == "saved draft\n\ntyped again"


def test_pending_cards_do_not_survive_a_conversation_change(env, fake_organizer):
    make_panel, disp, data_dir = env
    panel = make_panel()
    _turn(panel, "target", "ok")
    target = panel.conversation.id
    panel.new_conversation()
    _turn(panel, "left", "ok")
    left = panel.conversation.id

    resolved = []
    disp.resolve_approval = lambda r, d: resolved.append((r, d))
    # raised while no turn runs (e.g. an Antigravity client registered outside a session)
    disp.approval_requested.emit("r1", "remove_mod", {"mod_name": "Lux"}, True)
    disp._session_approved_all = True
    assert panel.open_conversation(target) is True

    assert resolved == [("r1", "reject")]
    assert _cards(panel) == []
    assert disp._session_approved_all is False
    store = _store(data_dir, fake_organizer)
    assert [m.meta["decision"] for m in store.load(left).messages if m.role == "approval"] == ["cancelled"]
    assert not [m for m in store.load(target).messages if m.role == "approval"]
    assert not [m for m in panel.conversation.messages if m.role == "approval"]

    disp.approval_requested.emit("r2", "remove_mod", {"mod_name": "Lux"}, True)
    panel.new_conversation()
    assert resolved[-1] == ("r2", "reject")
    assert _cards(panel) == []
    assert [m.meta["decision"] for m in store.load(target).messages if m.role == "approval"] == ["cancelled"]
    assert panel.conversation.messages == []
