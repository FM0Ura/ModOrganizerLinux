"""Fixes from the final whole-branch review of the F1 branch."""

from __future__ import annotations

import os
import stat
import threading
import time
import uuid
from pathlib import Path

from PyQt6.QtCore import QCoreApplication
from PyQt6.QtWidgets import QApplication, QMainWindow

from ai_assistant.agent import worker as worker_mod
from ai_assistant.agent.cli_runner import CliTurnResult
from ai_assistant.agent.graph import build_agent_graph
from ai_assistant.agent.worker import AgentTurnWorker
from ai_assistant.config import AssistantConfig
from ai_assistant.history.store import Conversation, ConversationStore
from ai_assistant.tools import MainThreadDispatcher, build_default_registry
from ai_assistant.ui.approval_card import ApprovalCard
from ai_assistant.ui.slash_commands import HELP_TEXT

from test_chat_conversation import FakeWorker, env, _turn  # noqa: F401  (fixture and helpers)
from test_registry_and_dispatcher import _make_registry
from test_ui_widgets import plugin_env, _shortcuts  # noqa: F401  (fixture and helpers)
from test_worker_sessions import CountingModel, _make, _record


def _notes(panel):
    return [m.text for m in panel.conversation.messages if m.role == "note"]


def _send_typed(panel, text):
    panel.input_box.setPlainText(text)
    panel._on_send_clicked()


# --- 1. typed text is never lost on a command it does not fit ---------------------------


def test_new_with_text_after_it_keeps_the_text_and_the_conversation(env):
    make_panel, _, _ = env
    panel = make_panel()
    old = panel.conversation.id
    _send_typed(panel, "/new\nfix X")
    assert panel.conversation.id == old
    assert not FakeWorker.instances
    assert panel.input_box.toPlainText() == "/new\nfix X"


def test_unknown_command_with_text_goes_back_to_the_box(env):
    make_panel, _, _ = env
    panel = make_panel()
    _send_typed(panel, "/foo bar")
    assert not FakeWorker.instances
    assert panel.input_box.toPlainText() == "/foo bar"
    assert _notes(panel) == ["Unknown command; /help lists the commands."]


def test_help_with_text_shows_help_and_keeps_the_text(env):
    make_panel, _, _ = env
    panel = make_panel()
    _send_typed(panel, "/help me fix my load order")
    assert _notes(panel) == [HELP_TEXT]
    assert panel.input_box.toPlainText() == "/help me fix my load order"
    assert not FakeWorker.instances


def test_history_and_export_with_text_do_nothing_but_keep_it(env, monkeypatch):
    make_panel, _, _ = env
    panel = make_panel()
    calls = []
    monkeypatch.setattr(panel, "show_history", lambda: calls.append("h"))
    monkeypatch.setattr(panel, "export_current", lambda: calls.append("e"))
    _send_typed(panel, "/history of my crashes")
    assert panel.input_box.toPlainText() == "/history of my crashes"
    panel.input_box.clear()
    _send_typed(panel, "/export it please")
    assert panel.input_box.toPlainText() == "/export it please"
    assert calls == []
    assert not FakeWorker.instances


def test_queued_command_with_text_goes_back_to_the_box(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("hi")
    old = panel.conversation.id
    panel.submit("/new\nfix X")
    panel.input_box.setPlainText("typed")
    FakeWorker.instances[0].finish("r")
    QApplication.processEvents()
    assert panel.conversation.id == old
    assert len(FakeWorker.instances) == 1
    assert panel.input_box.toPlainText() == "typed\n\n/new\nfix X"


# --- 2. send stays enabled during a turn --------------------------------------------------


def test_send_button_queues_during_a_turn(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("one")
    assert panel.send_btn.isEnabled()
    panel.input_box.setPlainText("two")
    panel.send_btn.click()
    assert panel.queue == ["two"]
    assert len(FakeWorker.instances) == 1


# --- 3. an unknown approval decision fails closed -----------------------------------------


def test_unknown_decision_is_a_rejection(fake_organizer):
    disp = MainThreadDispatcher(fake_organizer, _make_registry(fake_organizer), permission_mode="safe",
                                vfs_mounted_check=lambda: False)
    disp.approval_requested.connect(lambda req_id, *_: disp.resolve_approval(req_id, "approve_maybe"))
    res = disp.execute("dummy_write", {"mod_name": "USSEP"})
    assert res["ok"] is False and "rejected by user" in res["error"]
    assert fake_organizer.modList().state("USSEP") == 0x02
    assert disp._session_approved_all is False


def test_unknown_card_decision_is_recorded_as_rejected(env):
    make_panel, disp, _ = env
    panel = make_panel()
    panel.submit("hi")
    resolved = []
    disp.resolve_approval = lambda r, d: resolved.append((r, d))
    disp.approval_requested.emit("r1", "remove_mod", {"mod_name": "Lux"}, True)
    panel._on_card_decision("r1", "approve_maybe")
    approvals = [m.meta["decision"] for m in panel.conversation.messages if m.role == "approval"]
    assert approvals == ["rejected"]


# --- 4. no card after Cancel -------------------------------------------------------------


def test_approval_after_cancel_is_rejected_without_a_card(env):
    make_panel, disp, _ = env
    panel = make_panel()
    panel.submit("hi")
    panel._on_cancel_clicked()
    resolved = []
    disp.resolve_approval = lambda r, d: resolved.append((r, d))
    disp.approval_requested.emit("r9", "remove_mod", {"mod_name": "Lux"}, True)
    assert resolved == [("r9", "reject")]
    assert panel.findChildren(ApprovalCard) == [] and panel._pending_cards == {}
    approvals = [m.meta["decision"] for m in panel.conversation.messages if m.role == "approval"]
    assert approvals == ["cancelled"]


# --- 5. plugin init order -------------------------------------------------------------------


def test_failing_history_does_not_skip_the_shortcut_and_quit_hook(plugin_env, fake_organizer, monkeypatch):
    def boom(self):
        raise OSError("unreadable history")

    monkeypatch.setattr(ConversationStore, "list", boom)
    app = QCoreApplication.instance()
    before = app.receivers(app.aboutToQuit)
    keep = [QMainWindow()]
    fake_organizer._ui_callback(keep[-1])  # must not raise
    assert app.receivers(app.aboutToQuit) == before + 1
    assert len(_shortcuts(keep[-1])) == 1
    assert plugin_env._dock is not None


def test_rebuild_index_ignores_a_failing_cache_write(tmp_path, monkeypatch):
    store = ConversationStore(tmp_path / "data", "/games/x")
    conv = Conversation.new("claude_cli")
    conv.add("user", "hello")
    store.save(conv)
    (store.folder / "index.json").unlink()

    def boom(self, entries):
        raise OSError("read-only")

    monkeypatch.setattr(ConversationStore, "_write_index", boom)
    assert [e["id"] for e in store.list()] == [conv.id]


# --- 6. the worker runs the conversation's provider ----------------------------------------


def test_panel_passes_the_conversation_provider(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.new_conversation("codex_cli")
    panel._config.provider = "gemini"  # e.g. changed elsewhere while this conversation is open
    panel.send_prompt("hello")
    assert FakeWorker.instances[-1].kwargs["provider"] == "codex_cli"


def test_worker_uses_its_provider_not_the_config(monkeypatch, tmp_path):
    calls = []
    monkeypatch.setattr(
        worker_mod, "run_cli_turn", lambda *a, **k: calls.append(a[0]) or CliTurnResult("hi", "s", None)
    )
    w = _make(AssistantConfig(provider="gemini"), tmp_path, provider="codex_cli")
    w.run()
    assert calls == ["codex_cli"]


def test_api_worker_builds_the_model_of_its_provider(monkeypatch, tmp_path, fake_organizer):
    disp = MainThreadDispatcher(fake_organizer, build_default_registry(), permission_mode="autonomous",
                                vfs_mounted_check=lambda: False)
    seen = []
    monkeypatch.setattr(worker_mod, "create_chat_model", lambda cfg: seen.append(cfg.provider) or CountingModel())
    cfg = AssistantConfig(provider="claude_cli", api_keys={"openai": "sk-test"})
    w = _make(cfg, tmp_path, dispatcher=disp, provider="openai", sqlite_path=tmp_path / "c.sqlite")
    w.run()
    assert seen == ["openai"]
    assert cfg.provider == "claude_cli"  # the shared config is not changed


# --- 7. "approve for this conversation" is set on the main thread ------------------------------


def test_reset_after_approve_session_leaves_it_off(fake_organizer):
    disp = MainThreadDispatcher(fake_organizer, _make_registry(fake_organizer), permission_mode="safe",
                                vfs_mounted_check=lambda: False)
    requests = []
    disp.approval_requested.connect(lambda req_id, *_: requests.append(req_id))
    out = []
    t = threading.Thread(target=lambda: out.append(disp.execute("dummy_write", {"mod_name": "USSEP"})))
    t.start()
    deadline = time.monotonic() + 10
    while not requests and time.monotonic() < deadline:
        QApplication.processEvents()
        time.sleep(0.005)
    disp.resolve_approval(requests[0], "approve_session")
    assert disp._session_approved_all is True
    disp.reset_session_approvals()  # e.g. the user opened another conversation
    while t.is_alive() and time.monotonic() < deadline:
        QApplication.processEvents()
        time.sleep(0.005)
    t.join(timeout=2)
    assert out and out[0]["ok"] is True
    assert disp._session_approved_all is False


# --- 8. no false "memory lost" note --------------------------------------------------------------


def test_lost_note_is_dropped_without_history(monkeypatch, tmp_path):
    monkeypatch.setattr(worker_mod, "run_cli_turn", lambda *a, **k: CliTurnResult("hi", k["session_id"], "lost"))
    w = _make(AssistantConfig(provider="claude_cli"), tmp_path, session_id="abc", has_history=False)
    events = _record(w)
    w.run()
    assert events == [("response", "hi")]

    w2 = _make(AssistantConfig(provider="claude_cli"), tmp_path, session_id="abc", has_history=True)
    events2 = _record(w2)
    w2.run()
    assert events2 == [("note", "lost"), ("response", "hi")]


def test_panel_says_whether_the_conversation_has_an_answer(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.send_prompt("one")
    first = FakeWorker.instances[-1]
    assert first.kwargs["has_history"] is False
    panel._on_cancel_clicked()
    first.finish()  # cancelled before any answer
    panel.send_prompt("two")
    assert FakeWorker.instances[-1].kwargs["has_history"] is False
    FakeWorker.instances[-1].finish("answer")
    panel.send_prompt("three")
    assert FakeWorker.instances[-1].kwargs["has_history"] is True


# --- 9. the checkpoint database is private ------------------------------------------------------


def test_checkpoints_database_is_0600(fake_organizer, tmp_path):
    disp = MainThreadDispatcher(fake_organizer, build_default_registry(), permission_mode="autonomous",
                                vfs_mounted_check=lambda: False)
    db = tmp_path / "data" / "checkpoints.sqlite"
    old = os.umask(0o022)
    try:
        build_agent_graph(disp, CountingModel(), sqlite_path=db, system_prompt="sys")
    finally:
        os.umask(old)
    assert stat.S_IMODE(db.stat().st_mode) == 0o600


# --- 11. deleting the open conversation during a turn is refused ------------------------------------


def test_delete_open_conversation_refused_during_a_turn(env, fake_organizer):
    make_panel, _, data_dir = env
    panel = make_panel()
    _turn(panel, "keep me", "ok")
    conv_id = panel.conversation.id
    panel.send_prompt("still running")
    transcript = panel.transcript.toPlainText()
    panel.history_view.delete_requested.emit(conv_id)
    store = ConversationStore(data_dir, fake_organizer.basePath())
    assert store.load(conv_id) is not None
    assert panel.conversation.id == conv_id
    assert panel.transcript.toPlainText() == transcript
    assert panel.is_busy()
