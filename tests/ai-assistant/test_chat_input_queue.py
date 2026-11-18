"""Enter-to-send, the message queue and drafts (Task 6 of the F1 plan)."""

from __future__ import annotations

from PyQt6.QtCore import QEvent, Qt
from PyQt6.QtGui import QFocusEvent, QInputMethodEvent, QKeyEvent
from PyQt6.QtWidgets import QApplication, QLabel

import time

from ai_assistant.agent import worker as worker_mod
from ai_assistant.agent.cli_runner import CliTurnResult
from ai_assistant.history.store import ConversationStore

from test_chat_conversation import FakeWorker, env  # noqa: F401  (fixture)


def _key(panel, key, mods=Qt.KeyboardModifier.NoModifier):
    for kind in (QEvent.Type.KeyPress, QEvent.Type.KeyRelease):
        QApplication.sendEvent(panel.input_box, QKeyEvent(kind, key, mods))


def _labels(panel):
    return [w.text() for w in panel.queue_view.findChildren(QLabel)]


def test_enter_sends_shift_enter_newline(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.input_box.setPlainText("hello")
    _key(panel, Qt.Key.Key_Return, Qt.KeyboardModifier.ShiftModifier)
    assert not FakeWorker.instances
    assert "\n" in panel.input_box.toPlainText()
    panel.input_box.setPlainText("hello")
    _key(panel, Qt.Key.Key_Return)
    assert len(FakeWorker.instances) == 1
    assert FakeWorker.instances[0].kwargs["user_text"] == "hello"
    assert panel.input_box.toPlainText() == ""


def test_enter_ignored_while_composing(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.input_box.setPlainText("nihon")
    QApplication.sendEvent(panel.input_box, QInputMethodEvent("に", []))
    _key(panel, Qt.Key.Key_Return)
    assert not FakeWorker.instances
    QApplication.sendEvent(panel.input_box, QInputMethodEvent("", []))
    _key(panel, Qt.Key.Key_Return)
    assert len(FakeWorker.instances) == 1


def test_enter_during_turn_queues(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("one")
    panel.input_box.setPlainText("two")
    _key(panel, Qt.Key.Key_Enter)
    assert len(FakeWorker.instances) == 1
    assert panel.queue == ["two"]
    assert _labels(panel) == ["⏳ Queued: two"]
    assert panel.input_box.toPlainText() == ""


def test_queue_runs_in_order(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("one")
    panel.submit("two")
    panel.submit("three")
    FakeWorker.instances[0].finish("r1")
    QApplication.processEvents()
    assert len(FakeWorker.instances) == 2
    assert FakeWorker.instances[1].kwargs["user_text"] == "two"
    assert panel.queue == ["three"]
    FakeWorker.instances[1].finish("r2")
    QApplication.processEvents()
    assert FakeWorker.instances[2].kwargs["user_text"] == "three"
    FakeWorker.instances[2].finish("r3")
    assert panel.queue == [] and not panel.is_busy()
    assert [m.text for m in panel.conversation.messages if m.role == "user"] == ["one", "two", "three"]


def test_remove_queued(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("one")
    panel.submit("two")
    panel.submit("three")
    panel.remove_queued(0)
    assert panel.queue == ["three"]
    panel.remove_queued(5)  # out of range: ignored
    assert panel.queue == ["three"]


def test_cancel_returns_queue_to_input(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("one")
    panel.submit("two")
    panel.submit("three")
    panel.input_box.setPlainText("typed")
    panel._on_cancel_clicked()
    assert panel.input_box.toPlainText() == "typed\n\ntwo\n\nthree"
    assert panel.queue == []
    FakeWorker.instances[0].finish()
    assert len(FakeWorker.instances) == 1  # nothing was sent after cancelling


def test_draft_saved_and_restored(env, fake_organizer):
    make_panel, _, data_dir = env
    panel = make_panel()
    panel.submit("one")
    panel.submit("queued")
    FakeWorker.instances[0].finish("r1")
    QApplication.processEvents()
    FakeWorker.instances[1].finish("r2")
    panel.submit("three")
    panel.submit("later")
    panel.input_box.setPlainText("typed")
    conv_id = panel.conversation.id
    FakeWorker.instances[-1].finish("r3")  # sends "later"; queue is now empty
    QApplication.processEvents()
    FakeWorker.instances[-1].finish("r4")
    panel.input_box.setPlainText("half-written")
    panel.new_conversation()
    saved = ConversationStore(data_dir, fake_organizer.basePath()).load(conv_id)
    assert saved.draft == "half-written"
    assert panel.input_box.toPlainText() == ""
    assert panel.open_conversation(conv_id)
    assert panel.input_box.toPlainText() == "half-written"
    panel.input_box.clear()  # what Enter does before submit()
    panel.submit("sent")
    assert panel.conversation.draft == ""


def test_stash_draft_includes_queue(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("one")
    panel.submit("queued")
    panel.input_box.setPlainText("typed")
    assert panel.draft_text() == "typed\n\nqueued"
    panel._stash_draft()
    assert panel.conversation.draft == "typed\n\nqueued"
    assert panel.queue == ["queued"]  # stashing alone leaves the queue; leaving the conversation clears it


def test_submit_while_cancelling_goes_back_to_box(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("one")
    panel.submit("two")
    panel._on_cancel_clicked()
    assert panel.input_box.toPlainText() == "two"
    panel.submit("late")
    assert panel.queue == []
    assert panel.input_box.toPlainText() == "two\n\nlate"
    FakeWorker.instances[0].finish()
    QApplication.processEvents()
    assert len(FakeWorker.instances) == 1


def test_finished_cancelled_turn_moves_leftover_queue_to_box(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("one")
    panel._on_cancel_clicked()
    panel._queue.append("stray")
    FakeWorker.instances[0].finish()
    assert panel.queue == []
    assert panel.input_box.toPlainText() == "stray"


def test_focus_out_resets_composing(env):
    make_panel, _, _ = env
    panel = make_panel()
    QApplication.sendEvent(panel.input_box, QInputMethodEvent("に", []))
    QApplication.sendEvent(panel.input_box, QFocusEvent(QEvent.Type.FocusOut))
    panel.input_box.setPlainText("x")
    _key(panel, Qt.Key.Key_Return)
    assert len(FakeWorker.instances) == 1


def test_queue_drains_with_real_worker(env, monkeypatch):
    make_panel, _, _ = env
    from ai_assistant.ui import chat_panel as cp

    monkeypatch.setattr(cp, "AgentTurnWorker", worker_mod.AgentTurnWorker)  # the real thread
    monkeypatch.setattr(worker_mod, "run_cli_turn", lambda *a, **k: CliTurnResult(text=f"re: {a[1]}", session_id=k["session_id"]))
    panel = make_panel()
    panel.submit("one")
    panel.submit("two")
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and (panel.is_busy() or panel.queue):
        QApplication.processEvents()
        time.sleep(0.01)
    QApplication.processEvents()
    texts = [(m.role, m.text) for m in panel.conversation.messages]
    assert ("assistant", "re: one") in texts and ("assistant", "re: two") in texts
