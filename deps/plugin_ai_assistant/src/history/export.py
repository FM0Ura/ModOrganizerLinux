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


def approval_line(text: str, decision: str) -> str:
    """'✔ Approved: tool(args)' for an approval message; the bare text for an unknown decision."""
    prefix = _APPROVAL_PREFIX.get(decision)
    return f"{prefix}: {text}" if prefix else text


def to_markdown(conv: Conversation) -> str:
    lines = [f"# {conv.title or 'Conversation'}", ""]
    lines.append(f"Provider: {conv.provider} · Started: {conv.created_at}")
    lines.append("")
    for m in conv.messages:
        lines.append(f"## {m.role} — {m.at}")
        lines.append("")
        if m.role == "approval":
            lines.append(approval_line(m.text, (m.meta or {}).get("decision", "")))
        else:
            lines.append(m.text)
        lines.append("")
    return "\n".join(lines)
