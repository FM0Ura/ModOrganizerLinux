from __future__ import annotations

import json
import os
import stat

from ai_assistant.history.export import to_markdown
from ai_assistant.history.store import (
    Conversation,
    ConversationStore,
    make_title,
    session_key,
)


def _conv(provider="openai", text="hello", at=None):
    c = Conversation.new(provider)
    c.add("user", text)
    c.add("assistant", "reply")
    if at:
        c.updated_at = at
    return c


def _mode(p):
    return stat.S_IMODE(os.stat(p).st_mode)


def test_save_load_roundtrip(tmp_path):
    store = ConversationStore(tmp_path, "/inst")
    c = Conversation.new("claude_cli")
    c.add("user", "Hi there")
    c.add("assistant", "Hello")
    c.add("note", "a note")
    c.add("error", "boom")
    c.add("approval", "set_mod_active(X)", {"decision": "approved"})
    c.session = {"claude_session_id": "abc"}
    c.draft = "x"
    assert store.save(c) is True
    loaded = store.load(c.id)
    assert loaded is not None
    assert loaded.to_dict() == c.to_dict()


def test_empty_conversation_not_saved(tmp_path):
    store = ConversationStore(tmp_path, "/inst")
    assert store.save(Conversation.new("openai")) is False
    assert not store.folder.exists()


def test_permissions(tmp_path):
    store = ConversationStore(tmp_path, "/inst")
    c = _conv()
    store.save(c)
    assert _mode(store.folder) == 0o700
    assert _mode(store.folder / f"{c.id}.json") == 0o600
    assert _mode(store.folder / "index.json") == 0o600


def test_instances_are_separate(tmp_path):
    a = ConversationStore(tmp_path, "/inst/a")
    b = ConversationStore(tmp_path, "/inst/b")
    ca, cb = _conv(), _conv()
    a.save(ca)
    b.save(cb)
    assert [e["id"] for e in a.list()] == [ca.id]
    assert [e["id"] for e in b.list()] == [cb.id]
    assert (a.folder / "instance.txt").read_text(encoding="utf-8").strip() == "/inst/a"


def _two(tmp_path):
    store = ConversationStore(tmp_path, "/inst")
    old = _conv(text="old", at="2026-01-01T00:00:00+00:00")
    new = _conv(text="new", at="2026-02-01T00:00:00+00:00")
    store.save(old)
    store.save(new)
    return store, old, new


def test_index_rebuilt_when_corrupt(tmp_path):
    store, old, new = _two(tmp_path)
    idx = store.folder / "index.json"
    idx.write_text("garbage{{", encoding="utf-8")
    assert [e["id"] for e in store.list()] == [new.id, old.id]
    json.loads(idx.read_text(encoding="utf-8"))
    idx.unlink()
    assert [e["id"] for e in store.list()] == [new.id, old.id]
    assert idx.is_file()
    assert [e["id"] for e in json.loads(idx.read_text(encoding="utf-8"))["entries"]] == [new.id, old.id]


def test_corrupt_conversation_skipped(tmp_path):
    store, old, new = _two(tmp_path)
    path = store.folder / f"{old.id}.json"
    path.write_text(path.read_text(encoding="utf-8")[:20], encoding="utf-8")
    (store.folder / "index.json").unlink()
    assert [e["id"] for e in store.list()] == [new.id]
    assert [e["id"] for e in store.search("old")] == []
    assert store.load(old.id) is None


def test_title_single_line_unicode():
    assert make_title("Olá 😀\nsegunda linha") == "Olá 😀 segunda linha"
    t = make_title("a" * 80)
    assert t == "a" * 60 + "…"
    t = make_title("palavra " * 20)
    assert not t.endswith(" …") and len(t) <= 61 and t.endswith("…")


def test_rename_marks_custom_and_survives_new_messages(tmp_path):
    store = ConversationStore(tmp_path, "/inst")
    c = _conv()
    store.save(c)
    store.rename(c.id, "My title")
    loaded = store.load(c.id)
    assert loaded.title == "My title" and loaded.title_custom
    loaded.add("user", "another question")
    store.save(loaded)
    assert store.load(c.id).title == "My title"
    assert store.list()[0]["title"] == "My title"


def test_title_set_from_first_user_message():
    c = Conversation.new("openai")
    c.add("note", "n")
    assert c.title == ""
    c.add("user", "First question")
    c.add("user", "Second")
    assert c.title == "First question"


def test_search_matches_title_and_body_case_insensitive(tmp_path):
    store = ConversationStore(tmp_path, "/inst")
    a = Conversation.new("openai")
    a.add("user", "Fix the SkyUI crash")
    b = Conversation.new("openai")
    b.add("user", "other")
    b.add("assistant", "Try disabling LOOTING plugin")
    store.save(a)
    store.save(b)
    assert [e["id"] for e in store.search("skyui")] == [a.id]
    assert [e["id"] for e in store.search("looting")] == [b.id]
    assert store.search("nothing") == []


def test_delete_removes_file_and_index_entry(tmp_path):
    store, old, new = _two(tmp_path)
    store.delete(old.id)
    assert not (store.folder / f"{old.id}.json").exists()
    assert [e["id"] for e in store.list()] == [new.id]
    assert old.id not in (store.folder / "index.json").read_text(encoding="utf-8")


def test_most_recent(tmp_path):
    store, old, new = _two(tmp_path)
    assert store.most_recent().id == new.id
    assert ConversationStore(tmp_path, "/empty").most_recent() is None


def test_from_dict_missing_field():
    import pytest
    with pytest.raises(ValueError):
        Conversation.from_dict({"id": "x"})


def test_session_key():
    assert session_key("claude_cli") == "claude_session_id"
    assert session_key("codex_cli") == "codex_session_id"
    assert session_key("agy_cli") == "agy_conversation_id"
    assert session_key("openai") == "thread_id"


def test_markdown_export():
    c = Conversation.new("openai")
    c.add("user", "Disable SkyUI")
    c.add("assistant", "Done")
    c.add("approval", "set_mod_active(SkyUI, false)", {"decision": "approved"})
    c.add("approval", "delete_mod(X)", {"decision": "rejected"})
    c.add("approval", "refresh()", {"decision": "auto"})
    md = to_markdown(c)
    assert md.startswith("# Disable SkyUI")
    assert "## user" in md.lower() and "## assistant" in md.lower()
    assert c.messages[0].at in md
    assert "✔ Approved: set_mod_active(SkyUI, false)" in md
    assert "✖ Rejected: delete_mod(X)" in md
    assert "✔ Automatic: refresh()" in md
