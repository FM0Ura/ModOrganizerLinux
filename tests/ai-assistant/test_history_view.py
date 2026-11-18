"""History screen and export (Task 8 of the F1 plan). No modal dialog is ever executed."""

from __future__ import annotations

from pathlib import Path

import pytest
from PyQt6.QtWidgets import QFileDialog, QInputDialog, QMessageBox

from ai_assistant.history.export import to_markdown
from ai_assistant.history.store import Conversation, ConversationStore
from ai_assistant.ui import history_view as history_view_mod
from ai_assistant.ui.history_view import HistoryView

from test_chat_conversation import FakeWorker, env, _turn  # noqa: F401  (fixtures and helpers)


def _seed(store: ConversationStore, text: str, stamp: str, provider: str = "claude_cli") -> Conversation:
    c = Conversation.new(provider)
    c.add("user", text)
    c.add("assistant", f"answer to {text}")
    c.updated_at = stamp
    store.save(c)
    return c


@pytest.fixture
def seeded(env, fake_organizer):  # noqa: F811
    make_panel, disp, data_dir = env
    store = ConversationStore(data_dir, fake_organizer.basePath())
    a = _seed(store, "alpha crash", "2026-01-01T10:00:00Z")
    b = _seed(store, "beta load order", "2026-01-02T10:00:00Z", provider="ollama")
    panel = make_panel()
    return panel, store, a, b


def _titles(view: HistoryView) -> list[str]:
    return [view.list.item(i).text() for i in range(view.list.count())]


def test_list_is_ordered_and_shows_provider(seeded):
    panel, _, a, b = seeded
    panel.show_history()
    view = panel.history_view
    assert panel.stack.currentWidget() is view
    assert [view.list.item(i).data(history_view_mod.ID_ROLE) for i in range(2)] == [b.id, a.id]
    assert "beta load order" in _titles(view)[0] and "Ollama (Local)" in _titles(view)[0]


def test_search_filters_on_typing(seeded):
    panel, _, a, _ = seeded
    panel.show_history()
    panel.history_view.search.setText("crash")
    assert [panel.history_view.list.item(0).data(history_view_mod.ID_ROLE)] == [a.id]
    assert panel.history_view.list.count() == 1
    panel.history_view.search.setText("")
    assert panel.history_view.list.count() == 2


def test_open_switches_to_chat_with_the_conversation(seeded):
    panel, _, a, _ = seeded
    panel.show_history()
    panel.history_view.open_requested.emit(a.id)
    assert panel.stack.currentWidget() is panel.chat_page
    assert panel.conversation.id == a.id


def test_open_missing_conversation_keeps_panel(seeded):
    panel, store, a, _ = seeded
    before = panel.conversation.id
    panel.show_history()
    panel.history_view.open_requested.emit("does-not-exist")
    assert panel.stack.currentWidget() is panel.history_view
    assert panel.conversation.id == before
    assert panel.history_view.message_label.text()


def test_back_returns_to_chat(seeded):
    panel, *_ = seeded
    panel.show_history()
    panel.history_view.back_requested.emit()
    assert panel.stack.currentWidget() is panel.chat_page


def test_rename_updates_list_and_open_conversation(seeded):
    panel, store, a, _ = seeded
    panel.open_conversation(a.id)
    panel.show_history()
    panel.history_view.rename_requested.emit(a.id, "Renamed\nfoo  bar")
    assert store.load(a.id).title == "Renamed foo bar"
    assert panel.conversation.title == "Renamed foo bar" and panel.conversation.title_custom
    assert any("Renamed foo bar" in t for t in _titles(panel.history_view))
    _turn(panel, "more", "ok")  # the next save must keep the rename
    assert store.load(a.id).title == "Renamed foo bar"


def test_delete_open_conversation_starts_new(seeded):
    panel, store, a, b = seeded
    panel.open_conversation(a.id)
    panel.show_history()
    panel.history_view.delete_requested.emit(a.id)
    assert store.load(a.id) is None
    assert panel.conversation.id != a.id and not panel.conversation.messages
    assert store.load(panel.conversation.id) is None
    assert panel.history_view.list.count() == 1


def test_delete_other_conversation_keeps_open_one(seeded):
    panel, store, a, b = seeded
    panel.open_conversation(a.id)
    panel.show_history()
    panel.history_view.delete_requested.emit(b.id)
    assert store.load(b.id) is None and panel.conversation.id == a.id


def test_export_with_path_writes_markdown(seeded, tmp_path):
    panel, store, a, _ = seeded
    target = tmp_path / "out.md"
    panel.export_conversation(a.id, str(target))
    assert target.read_text(encoding="utf-8") == to_markdown(store.load(a.id))


def test_export_without_path_uses_save_dialog(seeded, tmp_path, monkeypatch):
    panel, _, a, _ = seeded
    target = tmp_path / "chosen.md"
    seen = {}

    def fake(parent, caption, suggested, *args):
        seen["suggested"] = suggested
        return str(target), ""

    monkeypatch.setattr(QFileDialog, "getSaveFileName", staticmethod(fake))
    panel.export_conversation(a.id)
    # absolute: MO2's working directory is inside the read-only AppImage mount
    suggested = Path(seen["suggested"])
    assert suggested.is_absolute() and suggested.name == "alpha crash.md" and target.exists()
    assert suggested.parent in (Path.home() / "Documents", Path.home() / "Documentos", Path.home()) or (
        suggested.parent.is_dir() and suggested.parent != Path.cwd()
    )
    monkeypatch.setattr(QFileDialog, "getSaveFileName", staticmethod(lambda *a, **k: ("", "")))
    target.unlink()
    panel.export_conversation(a.id)
    assert not target.exists()


def test_export_current_and_empty(seeded, tmp_path, monkeypatch):
    panel, *_ = seeded
    panel.new_conversation()
    panel.export_current()  # nothing to export: a note, no dialog
    assert panel.conversation.messages[-1].role == "note"
    _turn(panel, "hello", "hi")
    target = tmp_path / "cur.md"
    monkeypatch.setattr(QFileDialog, "getSaveFileName", staticmethod(lambda *a, **k: (str(target), "")))
    panel.export_current()
    assert target.read_text(encoding="utf-8") == to_markdown(panel.conversation)


def test_view_dialogs_emit_signals(seeded, monkeypatch):
    panel, _, a, _ = seeded
    view = panel.history_view
    got = []
    view.rename_requested.connect(lambda i, t: got.append(("rename", i, t)))
    view.delete_requested.connect(lambda i: got.append(("delete", i)))
    monkeypatch.setattr(QInputDialog, "getText", staticmethod(lambda *a, **k: ("New name", True)))
    view.rename(a.id)
    monkeypatch.setattr(QMessageBox, "question", staticmethod(lambda *a, **k: QMessageBox.StandardButton.No))
    view.delete(a.id)
    monkeypatch.setattr(QMessageBox, "question", staticmethod(lambda *a, **k: QMessageBox.StandardButton.Yes))
    view.delete(a.id)
    assert got == [("rename", a.id, "New name"), ("delete", a.id)]


def test_buttons_disabled_while_busy(seeded):
    panel, *_ = seeded
    assert panel.clear_btn.text() == "+ New" and panel.history_btn.text() == "History"
    panel.send_prompt("go")
    assert not panel.clear_btn.isEnabled() and not panel.history_btn.isEnabled()
    panel.show_history()
    assert panel.stack.currentWidget() is panel.chat_page
    FakeWorker.instances[-1].finish("done")
    assert panel.clear_btn.isEnabled() and panel.history_btn.isEnabled()


def test_delete_open_keeps_typed_text_and_rejects_cards(seeded, env):
    panel, store, a, _ = seeded
    _, disp, _ = env
    panel.open_conversation(a.id)
    resolved = []
    disp.resolve_approval = lambda r, d: resolved.append((r, d))
    disp.approval_requested.emit("r1", "remove_mod", {"mod_name": "Lux"}, True)
    disp._session_approved_all = True
    panel.input_box.setPlainText("half typed")
    panel.enqueue("queued one")
    panel.show_history()
    panel.history_view.delete_requested.emit(a.id)
    assert panel.conversation.id != a.id
    assert panel.input_box.toPlainText() == "half typed\n\nqueued one"
    assert resolved == [("r1", "reject")]
    assert disp._session_approved_all is False
    assert panel.queue == []
    assert store.load(a.id) is None
    assert a.id not in [e["id"] for e in store.list()]


def test_title_with_separator_is_kept_for_prompts(seeded, monkeypatch):
    panel, store, a, _ = seeded
    store.rename(a.id, "one  ·  two")
    panel.show_history()
    view = panel.history_view
    seen = {}

    def fake_text(parent, title, label, text=""):
        seen["prefill"] = text
        return "", False

    monkeypatch.setattr(QInputDialog, "getText", staticmethod(fake_text))
    view.rename(a.id)
    assert seen["prefill"] == "one  ·  two"

    def fake_q(parent, title, text, *args):
        seen["prompt"] = text
        return QMessageBox.StandardButton.No

    monkeypatch.setattr(QMessageBox, "question", staticmethod(fake_q))
    view.delete(a.id)
    assert "one  ·  two" in seen["prompt"]


def test_export_suggestion_ignores_the_working_directory(seeded, tmp_path, monkeypatch):
    panel, _, a, _ = seeded
    monkeypatch.chdir(tmp_path)  # stands in for /tmp/.mount_*/usr/bin
    seen = {}

    def fake(_parent, _caption, suggested, _filter):
        seen["suggested"] = suggested
        return "", ""

    monkeypatch.setattr(QFileDialog, "getSaveFileName", staticmethod(fake))
    panel.export_conversation(a.id)
    assert Path(seen["suggested"]).is_absolute()
    assert Path(seen["suggested"]).parent != tmp_path
