from __future__ import annotations

from pathlib import Path

import pytest
from PyQt6 import sip
from PyQt6.QtCore import QCoreApplication, QEvent, QObject, Qt, pyqtSignal
from PyQt6.QtGui import QShortcut
from PyQt6.QtWidgets import QMainWindow
from ai_assistant import createPlugin
from ai_assistant.history.store import Conversation, ConversationStore
from ai_assistant.ui import chat_panel as chat_panel_mod
from ai_assistant.ui.approval_card import ApprovalCard

INTERRUPTED = "Turn interrupted: MO2 was closed."


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

    def isRunning(self):
        return self.running

    def wait(self, *_):
        return not self.running

    def cancel(self):
        # like the CLI kill: the thread ends once it sees the cancel event
        self.cancelled = True
        self.running = False


@pytest.fixture
def plugin_env(fake_organizer, monkeypatch):
    FakeWorker.instances.clear()
    monkeypatch.setattr(chat_panel_mod, "AgentTurnWorker", FakeWorker)
    plugin = createPlugin()
    assert plugin.init(fake_organizer) is True
    yield plugin
    plugin._on_about_to_quit()
    if plugin._mcp_server is not None:
        plugin._mcp_server.stop()


def _data_dir(fake_organizer) -> Path:
    return Path(fake_organizer.pluginDataPath()) / "ai_assistant"


def _shortcuts(win):
    return [s for s in win.findChildren(QShortcut) if s.key().toString() == "Ctrl+Shift+A"]


def test_approval_card_emits_decisions():
    received = []
    card = ApprovalCard("req-1", "remove_mod", {"mod_name": "BadMod"}, destructive=True)
    card.decision_made.connect(lambda req_id, dec: received.append((req_id, dec)))
    card.approve_btn.click()
    assert received == [("req-1", "approve")]


def test_reinit_in_same_process_is_clean(fake_organizer):
    plugin = createPlugin()
    assert plugin.init(fake_organizer) is True
    assert hasattr(fake_organizer, "_ui_callback")

    win1 = QMainWindow()
    fake_organizer._ui_callback(win1)
    assert plugin._dock is not None
    first_port = plugin._mcp_server.port
    assert first_port > 0

    # Simulate MO2_SMOKE_RESTART_ONCE in same process with a new QMainWindow
    win2 = QMainWindow()
    fake_organizer._ui_callback(win2)
    assert plugin._dock.parentWidget() is win2
    assert plugin._mcp_server.port == first_port

    # Check quick action and mode toggle in chat panel
    panel = plugin._dock.chat_panel
    panel.mode_combo.setCurrentIndex(1)  # Autonomous
    assert plugin._dispatcher._permission_mode == "autonomous"
    plugin._on_about_to_quit()
    plugin._mcp_server.stop()


def test_dock_goes_to_the_left_area(plugin_env, fake_organizer):
    win = QMainWindow()
    fake_organizer._ui_callback(win)
    assert win.dockWidgetArea(plugin_env._dock) == Qt.DockWidgetArea.LeftDockWidgetArea
    win2 = QMainWindow()
    fake_organizer._ui_callback(win2)
    assert win2.dockWidgetArea(plugin_env._dock) == Qt.DockWidgetArea.LeftDockWidgetArea


def test_shortcut_exists_once_after_two_ui_callbacks(plugin_env, fake_organizer):
    win = QMainWindow()
    fake_organizer._ui_callback(win)
    fake_organizer._ui_callback(win)
    QCoreApplication.sendPostedEvents(None, 0)  # runs the deleteLater of the old one
    shortcuts = _shortcuts(win)
    assert len(shortcuts) == 1
    assert shortcuts[0].context() == Qt.ShortcutContext.ApplicationShortcut
    assert shortcuts[0] is plugin_env._shortcut


def test_shortcut_moves_to_the_new_main_window(plugin_env, fake_organizer):
    win1, win2 = QMainWindow(), QMainWindow()
    fake_organizer._ui_callback(win1)
    fake_organizer._ui_callback(win2)
    QCoreApplication.sendPostedEvents(None, 0)
    assert _shortcuts(win1) == []
    assert len(_shortcuts(win2)) == 1


def test_shortcut_focuses_the_input_box(plugin_env, fake_organizer):
    win = QMainWindow()
    fake_organizer._ui_callback(win)
    win.show()
    panel = plugin_env._dock.chat_panel
    panel.show_history()
    plugin_env._dock.hide()
    plugin_env._shortcut.activated.emit()
    assert plugin_env._dock.isVisible()
    assert panel.stack.currentWidget() is panel.chat_page
    assert win.focusWidget() is panel.input_box
    win.close()


def test_display_focuses_the_input_box(plugin_env, fake_organizer):
    win = QMainWindow()
    fake_organizer._ui_callback(win)
    win.show()
    plugin_env._dock.hide()
    plugin_env.display()
    assert plugin_env._dock.isVisible()
    assert win.focusWidget() is plugin_env._dock.chat_panel.input_box
    win.close()


def test_about_to_quit_is_connected_once(plugin_env, fake_organizer):
    keep = []  # the main windows must outlive the test, as in MO2
    app = QCoreApplication.instance()
    before = app.receivers(app.aboutToQuit)
    keep.append(QMainWindow())
    fake_organizer._ui_callback(keep[-1])
    keep.append(QMainWindow())
    fake_organizer._ui_callback(keep[-1])
    assert app.receivers(app.aboutToQuit) == before + 1
    # Qt emits aboutToQuit on every exit of the restart loop: the handler disconnects
    plugin_env._on_about_to_quit()
    assert app.receivers(app.aboutToQuit) == before
    plugin_env._on_about_to_quit()  # a second call is harmless
    assert app.receivers(app.aboutToQuit) == before


def test_shutdown_saves_draft_and_interrupt_note(plugin_env, fake_organizer):
    keep = []  # the main windows must outlive the test, as in MO2
    keep.append(QMainWindow())
    fake_organizer._ui_callback(keep[-1])
    panel = plugin_env._dock.chat_panel
    panel.send_prompt("first question")
    worker = FakeWorker.instances[-1]
    panel.enqueue("queued one")
    panel.input_box.setPlainText("still typing")

    plugin_env._on_about_to_quit()

    assert worker.cancelled
    assert not panel.is_busy()
    store = ConversationStore(_data_dir(fake_organizer), fake_organizer.basePath())
    saved = store.load(panel.conversation.id)
    assert saved.draft == "still typing\n\nqueued one"
    assert [(m.role, m.text) for m in saved.messages] == [("user", "first question"), ("note", INTERRUPTED)]
    # a late signal of the stopped worker changes nothing
    worker.finished.emit()
    worker.response_ready.emit("late")
    assert len(store.load(panel.conversation.id).messages) == 2

    # idempotent: a second shutdown adds no second note
    panel.shutdown()
    assert [m.text for m in store.load(panel.conversation.id).messages].count(INTERRUPTED) == 1


def test_shutdown_without_a_turn_only_saves_the_draft(plugin_env, fake_organizer):
    keep = []  # the main windows must outlive the test, as in MO2
    keep.append(QMainWindow())
    fake_organizer._ui_callback(keep[-1])
    panel = plugin_env._dock.chat_panel
    panel.send_prompt("hello")
    FakeWorker.instances[-1].finished.emit()
    panel.input_box.setPlainText("unsent")
    panel.shutdown()
    saved = ConversationStore(_data_dir(fake_organizer), fake_organizer.basePath()).load(panel.conversation.id)
    assert saved.draft == "unsent"
    assert INTERRUPTED not in [m.text for m in saved.messages]


def test_dock_close_saves_the_draft_without_stopping_the_turn(plugin_env, fake_organizer):
    win = QMainWindow()
    fake_organizer._ui_callback(win)
    win.show()
    panel = plugin_env._dock.chat_panel
    panel.send_prompt("question")
    worker = FakeWorker.instances[-1]
    panel.input_box.setPlainText("typed")
    plugin_env._dock.close()  # the user hides the dock: the turn keeps running
    assert not worker.cancelled and panel.is_busy()
    saved = ConversationStore(_data_dir(fake_organizer), fake_organizer.basePath()).load(panel.conversation.id)
    assert saved.draft == "typed"
    win.close()


def test_opening_mo2_opens_the_most_recent_conversation(fake_organizer, monkeypatch):
    keep = []  # the main windows must outlive the test, as in MO2
    monkeypatch.setattr(chat_panel_mod, "AgentTurnWorker", FakeWorker)
    conv = Conversation.new("claude_cli")
    conv.add("user", "earlier question")
    ConversationStore(_data_dir(fake_organizer), fake_organizer.basePath()).save(conv)

    plugin = createPlugin()
    plugin.init(fake_organizer)
    keep.append(QMainWindow())
    fake_organizer._ui_callback(keep[-1])
    try:
        assert plugin._dock.chat_panel.conversation.id == conv.id
    finally:
        plugin._on_about_to_quit()
        plugin._mcp_server.stop()


def test_reinit_with_another_instance_opens_its_history(plugin_env, fake_organizer, tmp_path):
    keep = []  # the main windows must outlive the test, as in MO2
    keep.append(QMainWindow())
    fake_organizer._ui_callback(keep[-1])
    panel = plugin_env._dock.chat_panel
    panel.send_prompt("question in A")
    worker = FakeWorker.instances[-1]
    panel.enqueue("queued in A")
    first_id = panel.conversation.id
    store_a = ConversationStore(_data_dir(fake_organizer), fake_organizer.basePath())

    other = tmp_path / "instance-b"
    other.mkdir()
    conv_b = Conversation.new("codex_cli")
    conv_b.add("user", "question in B")
    ConversationStore(_data_dir(fake_organizer), str(other)).save(conv_b)
    approvals_reset = []
    real_reset = plugin_env._dispatcher.reset_session_approvals

    def _reset():
        approvals_reset.append(True)
        real_reset()

    plugin_env._dispatcher.reset_session_approvals = _reset
    fake_organizer.base_dir = other
    keep.append(QMainWindow())
    fake_organizer._ui_callback(keep[-1])

    # the conversation of instance A was stopped and saved first
    assert worker.cancelled
    saved_a = store_a.load(first_id)
    assert saved_a.draft == "queued in A"
    assert saved_a.messages[-1].text == INTERRUPTED
    # the panel shows instance B now
    assert panel.conversation.id == conv_b.id
    assert panel.queue == [] and panel.input_box.toPlainText() == ""
    assert "question in B" in panel.transcript.toPlainText()
    assert approvals_reset


def test_reinit_with_an_instance_without_history_starts_empty(plugin_env, fake_organizer, tmp_path):
    keep = []  # the main windows must outlive the test, as in MO2
    keep.append(QMainWindow())
    fake_organizer._ui_callback(keep[-1])
    panel = plugin_env._dock.chat_panel
    panel.send_prompt("question in A")
    FakeWorker.instances[-1].finished.emit()
    first_id = panel.conversation.id

    other = tmp_path / "instance-empty"
    other.mkdir()
    fake_organizer.base_dir = other
    keep.append(QMainWindow())
    fake_organizer._ui_callback(keep[-1])

    assert panel.conversation.id != first_id
    assert panel.conversation.messages == []
    assert panel.transcript.toPlainText() == ""
    # nothing of instance A leaks into the store of the new instance
    panel.send_prompt("question in empty")
    FakeWorker.instances[-1].finished.emit()
    assert ConversationStore(_data_dir(fake_organizer), str(other)).load(first_id) is None


def test_reinit_after_the_old_main_window_was_deleted(plugin_env, fake_organizer):
    win1 = QMainWindow()
    fake_organizer._ui_callback(win1)
    plugin_env._on_about_to_quit()  # MO2 exits the event loop, then deletes the window
    win1.deleteLater()
    QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete.value)
    win2 = QMainWindow()
    fake_organizer._ui_callback(win2)
    assert plugin_env._dock.parentWidget() is win2
    assert win2.dockWidgetArea(plugin_env._dock) == Qt.DockWidgetArea.LeftDockWidgetArea
    # aboutToQuit stopped the server and closed the dispatcher: the new run starts them again
    assert plugin_env._mcp_server._httpd is not None and plugin_env._mcp_server.port > 0
    assert plugin_env._dispatcher.execute("list_mods", {}).get("error") != "The AI Assistant was closed."


class StubbornWorker(FakeWorker):
    """An API turn: cancel() does not stop it, and wait() times out."""

    def cancel(self):
        self.cancelled = True


def test_shutdown_hands_a_worker_that_does_not_stop_to_qt(plugin_env, fake_organizer, monkeypatch):
    keep = [QMainWindow()]
    fake_organizer._ui_callback(keep[-1])
    monkeypatch.setattr(chat_panel_mod, "AgentTurnWorker", StubbornWorker)
    panel = plugin_env._dock.chat_panel
    panel.send_prompt("slow api question")
    worker = FakeWorker.instances[-1]

    panel.shutdown()

    assert worker.cancelled and worker.isRunning()
    assert not panel.is_busy() and panel._worker is None
    # owned by C++ now, not by the panel's Python object (unloaded after a restart) ...
    assert sip.ispyowned(worker) is False
    # ... and deleted by Qt once its run() ends
    worker.running = False
    worker.finished.emit()
    QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete.value)
    assert sip.isdeleted(worker)


def test_shutdown_closes_the_dispatcher(plugin_env, fake_organizer):
    keep = [QMainWindow()]
    fake_organizer._ui_callback(keep[-1])
    plugin_env._dock.chat_panel.shutdown()
    assert plugin_env._dispatcher.execute("list_mods", {}) == {
        "ok": False,
        "error": "The AI Assistant was closed.",
    }


def test_switch_instance_reopens_the_dispatcher(plugin_env, fake_organizer, tmp_path):
    keep = [QMainWindow()]
    fake_organizer._ui_callback(keep[-1])
    other = tmp_path / "instance-c"
    other.mkdir()
    fake_organizer.base_dir = other
    keep.append(QMainWindow())
    fake_organizer._ui_callback(keep[-1])
    assert plugin_env._dispatcher.execute("list_mods", {}).get("error") != "The AI Assistant was closed."


def test_about_to_quit_survives_an_exception_and_stops_the_server(plugin_env, fake_organizer):
    keep = [QMainWindow()]
    fake_organizer._ui_callback(keep[-1])
    panel = plugin_env._dock.chat_panel

    def boom():
        raise OSError("disk full")

    panel.shutdown = boom
    plugin_env._on_about_to_quit()  # an exception in a PyQt6 slot would be qFatal
    assert plugin_env._mcp_server._httpd is None


def test_dock_close_uses_the_public_save_draft(plugin_env, fake_organizer):
    keep = [QMainWindow()]
    fake_organizer._ui_callback(keep[-1])
    keep[-1].show()
    panel = plugin_env._dock.chat_panel
    saved = []
    panel.save_draft = lambda: saved.append(True) or True
    plugin_env._dock.close()
    assert saved == [True]
    keep[-1].close()
