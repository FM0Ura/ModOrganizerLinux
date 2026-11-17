"""Persistent conversation store (no Qt, no organizer)."""
from __future__ import annotations

import hashlib
import json
import os
import tempfile
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

ROLES = ("user", "assistant", "note", "error", "approval")
DECISIONS = ("approved", "approved_session", "auto", "rejected", "cancelled")
SESSION_KEYS: dict[str, str] = {
    "claude_cli": "claude_session_id",
    "codex_cli": "codex_session_id",
    "agy_cli": "agy_conversation_id",
}

IndexEntry = dict  # keys: id, title, provider, created_at, updated_at


def session_key(provider: str) -> str:
    return SESSION_KEYS.get(provider, "thread_id")


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


def make_title(text: str, limit: int = 60) -> str:
    """One-line title: whitespace collapsed, cut at a whole word, '…' when cut."""
    line = " ".join(text.split())
    if len(line) <= limit:
        return line
    cut = line[:limit]
    if not line[limit].isspace():
        head, sep, _ = cut.rpartition(" ")
        if sep:
            cut = head
    return cut.rstrip() + "…"


def instance_key(base_path: str) -> str:
    norm = os.path.normcase(os.path.realpath(base_path))
    return hashlib.sha256(norm.encode("utf-8")).hexdigest()[:16]


@dataclass
class Message:
    role: str
    text: str
    at: str
    meta: dict | None = None


@dataclass
class Conversation:
    id: str
    provider: str
    created_at: str
    updated_at: str
    title: str = ""
    title_custom: bool = False
    session: dict[str, str] = field(default_factory=dict)
    messages: list[Message] = field(default_factory=list)
    draft: str = ""
    version: int = 1

    @classmethod
    def new(cls, provider: str) -> "Conversation":
        now = _now()
        return cls(id=str(uuid.uuid4()), provider=provider, created_at=now, updated_at=now)

    def add(self, role: str, text: str, meta: dict | None = None) -> Message:
        if role not in ROLES:
            raise ValueError(f"invalid role: {role!r}")
        msg = Message(role, text, _now(), meta)
        self.messages.append(msg)
        self.updated_at = msg.at
        if role == "user" and not self.title_custom and not self.title:
            self.title = make_title(text)
        return msg

    def has_user_message(self) -> bool:
        return any(m.role == "user" for m in self.messages)

    def to_dict(self) -> dict:
        return {
            "version": self.version,
            "id": self.id,
            "provider": self.provider,
            "created_at": self.created_at,
            "updated_at": self.updated_at,
            "title": self.title,
            "title_custom": self.title_custom,
            "session": dict(self.session),
            "draft": self.draft,
            "messages": [
                {"role": m.role, "text": m.text, "at": m.at, "meta": m.meta} for m in self.messages
            ],
        }

    @classmethod
    def from_dict(cls, d: dict) -> "Conversation":
        try:
            msgs = [
                Message(m["role"], m["text"], m["at"], m.get("meta")) for m in d["messages"]
            ]
            return cls(
                id=d["id"],
                provider=d["provider"],
                created_at=d["created_at"],
                updated_at=d["updated_at"],
                title=d.get("title", ""),
                title_custom=bool(d.get("title_custom", False)),
                session=dict(d.get("session") or {}),
                messages=msgs,
                draft=d.get("draft", ""),
                version=int(d.get("version", 1)),
            )
        except (KeyError, TypeError, AttributeError) as e:
            raise ValueError(f"invalid conversation: {e!r}") from e


def _write_private(path: Path, text: str) -> None:
    """Writes text to path with mode 0600, atomically."""
    fd, tmp = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(text)
        os.replace(tmp, path)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise


def _entry(c: Conversation) -> IndexEntry:
    return {
        "id": c.id,
        "title": c.title,
        "provider": c.provider,
        "created_at": c.created_at,
        "updated_at": c.updated_at,
    }


class ConversationStore:
    """Conversations of one MO2 instance: <data_dir>/conversations/<instance_key>/."""

    def __init__(self, data_dir: Path, instance_path: str):
        self._instance_path = instance_path
        self.folder = Path(data_dir) / "conversations" / instance_key(instance_path)

    def _ensure_folder(self) -> None:
        self.folder.mkdir(parents=True, exist_ok=True)
        # parents created by mkdir follow the umask; the history folders are private
        for p in (self.folder, self.folder.parent):
            os.chmod(p, 0o700)
        marker = self.folder / "instance.txt"
        if not marker.exists():
            _write_private(marker, self._instance_path + "\n")

    def _path(self, conv_id: str) -> Path:
        return self.folder / f"{Path(conv_id).name}.json"

    def _read_index(self) -> list[IndexEntry] | None:
        try:
            data = json.loads((self.folder / "index.json").read_text(encoding="utf-8"))
            entries = data["entries"]
            if not isinstance(entries, list):
                return None
            for e in entries:
                if not all(isinstance(e.get(k), str) for k in ("id", "title", "provider", "created_at", "updated_at")):
                    return None
            return entries
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            return None

    def _write_index(self, entries: list[IndexEntry]) -> None:
        entries = sorted(entries, key=lambda e: e["updated_at"], reverse=True)
        _write_private(self.folder / "index.json", json.dumps({"entries": entries}, ensure_ascii=False))

    def _all(self) -> list[Conversation]:
        out = []
        if self.folder.is_dir():
            for p in self.folder.glob("*.json"):
                if p.name == "index.json":
                    continue
                c = self.load(p.stem)
                if c is not None:
                    out.append(c)
        return out

    def _rebuild_index(self) -> list[IndexEntry]:
        entries = [_entry(c) for c in self._all()]
        entries.sort(key=lambda e: e["updated_at"], reverse=True)
        if entries:
            try:
                self._write_index(entries)
            except OSError:
                pass  # the index is only a cache of the conversation files
        return entries

    def save(self, conv: Conversation) -> bool:
        if not conv.has_user_message():
            return False
        self._ensure_folder()
        _write_private(self._path(conv.id), json.dumps(conv.to_dict(), ensure_ascii=False))
        entries = self._read_index()
        if entries is None:
            self._rebuild_index()
            return True
        entries = [e for e in entries if e["id"] != conv.id] + [_entry(conv)]
        self._write_index(entries)
        return True

    def load(self, conv_id: str) -> Conversation | None:
        try:
            return Conversation.from_dict(json.loads(self._path(conv_id).read_text(encoding="utf-8")))
        except (OSError, ValueError):
            return None

    def list(self) -> list[IndexEntry]:
        if not self.folder.is_dir():
            return []
        entries = self._read_index()
        if entries is None:
            return self._rebuild_index()
        return sorted(entries, key=lambda e: e["updated_at"], reverse=True)

    def most_recent(self) -> Conversation | None:
        for e in self.list():
            c = self.load(e["id"])
            if c is not None:
                return c
        return None

    def search(self, query: str) -> list[IndexEntry]:
        q = query.casefold()
        hits = []
        for c in self._all():
            if q in c.title.casefold() or any(q in m.text.casefold() for m in c.messages):
                hits.append(_entry(c))
        hits.sort(key=lambda e: e["updated_at"], reverse=True)
        return hits

    def rename(self, conv_id: str, title: str) -> None:
        c = self.load(conv_id)
        if c is None:
            return
        c.title = title
        c.title_custom = True
        self.save(c)

    def delete(self, conv_id: str) -> None:
        self._path(conv_id).unlink(missing_ok=True)
        entries = self._read_index()
        if entries is not None:
            self._write_index([e for e in entries if e["id"] != conv_id])
