"""Slash commands typed in the chat box (Task 7 of the F1 plan)."""

from __future__ import annotations

import pytest
from PyQt6.QtWidgets import QApplication

from ai_assistant.ui.slash_commands import HELP_TEXT, PROVIDER_ALIASES, SlashCommand, parse_slash

from test_chat_conversation import FakeWorker, env  # noqa: F401  (fixture)


@pytest.mark.parametrize("name", ["new", "history", "export", "help", "mode", "provider"])
def test_parse_each_command(name):
    assert parse_slash(f"/{name}") == SlashCommand(name, "")
    assert parse_slash(f"/{name.upper()}") == SlashCommand(name, "")


def test_parse_arg_and_unknown():
    assert parse_slash("/mode  autonomous ") == SlashCommand("mode", "autonomous")
    assert parse_slash("/provider codex") == SlashCommand("provider", "codex")
    assert parse_slash("/bogus x") == SlashCommand("bogus", "x")


def test_parse_not_commands():
    assert parse_slash("hello") is None
    assert parse_slash("/") is None
    assert parse_slash("/ new") is None
    assert parse_slash("/usr/bin/foo broke") is None
    assert parse_slash("//x") == "/x"
    assert parse_slash("//new chat") == "/new chat"


def test_help_text_and_aliases():
    for token in ("/new", "/history", "/export", "/help", "/mode", "/provider", "Enter", "Shift+Enter", "Ctrl+Shift+A"):
        assert token in HELP_TEXT
    assert PROVIDER_ALIASES["codex"] == "codex_cli"
    assert PROVIDER_ALIASES["claude"] == "claude_cli"


def _notes(panel):
    return [m.text for m in panel.conversation.messages if m.role == "note"]


def test_unknown_command_sends_nothing(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("/bogus")
    assert not FakeWorker.instances
    assert _notes(panel) == ["Unknown command; /help lists the commands."]


def test_escaped_slash_is_sent_literally(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("//etc is odd")
    assert FakeWorker.instances[0].kwargs["user_text"] == "/etc is odd"


def test_provider_switches_and_starts_new_conversation(env):
    make_panel, _, _ = env
    panel = make_panel()
    old = panel.conversation.id
    panel.submit("/provider codex")
    assert panel.conversation.id != old
    assert panel.conversation.provider == "codex_cli"
    assert panel.provider_combo.currentData() == "codex_cli"
    assert not FakeWorker.instances


def test_provider_without_arg_lists_and_invalid_notes(env):
    make_panel, _, _ = env
    panel = make_panel()
    old = panel.conversation.id
    panel.submit("/provider")
    panel.submit("/provider nope")
    assert panel.conversation.id == old
    notes = _notes(panel)
    assert "codex" in notes[0] and "claude" in notes[0]
    assert "nope" in notes[1]


def test_mode_command(env):
    make_panel, disp, _ = env
    panel = make_panel()
    panel.submit("/mode")
    assert "safe" in _notes(panel)[0]
    panel.submit("/mode autonomous")
    assert disp._permission_mode == "autonomous"
    assert panel.mode_combo.currentData() == "autonomous"
    panel.submit("/mode weird")
    assert "weird" in _notes(panel)[-1]
    assert disp._permission_mode == "autonomous"


def test_new_command(env):
    make_panel, _, _ = env
    panel = make_panel()
    old = panel.conversation.id
    panel.submit("/new")
    assert panel.conversation.id != old
    assert not FakeWorker.instances


def test_history_and_export_call_hooks(env, monkeypatch):
    make_panel, _, _ = env
    panel = make_panel()
    calls = []
    monkeypatch.setattr(panel, "show_history", lambda: calls.append("h"))
    monkeypatch.setattr(panel, "export_current", lambda: calls.append("e"))
    panel.submit("/history")
    panel.submit("/export")
    assert calls == ["h", "e"]
    assert not FakeWorker.instances


def test_help_runs_immediately_while_busy(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("hi")
    panel.submit("/help")
    assert panel.queue == []
    assert _notes(panel) == [HELP_TEXT]


def test_queued_command_runs_as_command_when_dequeued(env):
    make_panel, disp, _ = env
    panel = make_panel()
    panel.submit("hi")
    panel.submit("/mode autonomous")
    panel.submit("after")
    assert panel.queue == ["/mode autonomous", "after"]
    FakeWorker.instances[0].finish("r")
    QApplication.processEvents()
    assert disp._permission_mode == "autonomous"
    assert [w.kwargs["user_text"] for w in FakeWorker.instances] == ["hi", "after"]
    assert panel.queue == []


def test_queued_unknown_command_not_sent(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("hi")
    panel.submit("/bogus")
    FakeWorker.instances[0].finish("r")
    QApplication.processEvents()
    assert len(FakeWorker.instances) == 1
    assert "Unknown command" in _notes(panel)[-1]


def test_queued_new_keeps_items_queued_after_it(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.submit("hi")
    old = panel.conversation
    panel.submit("/new")
    panel.submit("after")
    FakeWorker.instances[0].finish("r")
    QApplication.processEvents()
    assert panel.conversation.id != old.id
    assert [w.kwargs["user_text"] for w in FakeWorker.instances] == ["hi", "after"]
    assert "after" not in old.draft
    assert [m.text for m in panel.conversation.messages if m.role == "user"] == ["after"]


def test_provider_missing_from_combo_notes_and_changes_nothing(env):
    make_panel, _, _ = env
    panel = make_panel()
    panel.provider_combo.removeItem(panel.provider_combo.findData("codex_cli"))
    old = panel.conversation.id
    panel.submit("/provider codex")
    assert panel.conversation.id == old
    assert "codex" in _notes(panel)[-1] and "Unknown provider" in _notes(panel)[-1]
