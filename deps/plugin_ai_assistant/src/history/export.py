"""Markdown export of a conversation."""
from __future__ import annotations

from .store import Conversation

_APPROVAL_PREFIX = {
    "approved": "✔ Approved",
    "approved_session": "✔ Approved for this conversation",
    "auto": "✔ Automatic",
    "rejected": "✖ Rejected",
    "cancelled": "✖ Cancelled",
}


def to_markdown(conv: Conversation) -> str:
    lines = [f"# {conv.title or 'Conversation'}", ""]
    lines.append(f"Provider: {conv.provider} · Started: {conv.created_at}")
    lines.append("")
    for m in conv.messages:
        lines.append(f"## {m.role} — {m.at}")
        lines.append("")
        if m.role == "approval":
            decision = (m.meta or {}).get("decision", "")
            prefix = _APPROVAL_PREFIX.get(decision)
            lines.append(f"{prefix}: {m.text}" if prefix else m.text)
        else:
            lines.append(m.text)
        lines.append("")
    return "\n".join(lines)
