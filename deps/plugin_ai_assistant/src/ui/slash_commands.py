"""Parsing of the local slash commands typed in the chat box."""

from __future__ import annotations

import re
from dataclasses import dataclass

PROVIDER_ALIASES = {
    "claude": "claude_cli",
    "codex": "codex_cli",
    "agy": "agy_cli",
    "ollama": "ollama",
    "gemini": "gemini",
    "openai": "openai",
    "anthropic": "anthropic",
}

HELP_TEXT = (
    "Commands:\n"
    "/new - start a new conversation\n"
    "/history - browse past conversations\n"
    "/export - export the current conversation\n"
    "/mode [safe|autonomous] - show or change the permission mode\n"
    "/provider [name] - show or change the provider (" + ", ".join(PROVIDER_ALIASES) + ")\n"
    "/help - show this help\n"
    "Type //text to send a message that starts with a slash.\n"
    "Shortcuts: Enter sends, Shift+Enter adds a new line, Ctrl+Shift+A opens the assistant."
)

_COMMAND = re.compile(r"/([A-Za-z][A-Za-z0-9_-]*)(?:\s+(.*))?", re.DOTALL)


@dataclass
class SlashCommand:
    name: str
    arg: str


def parse_slash(text: str) -> SlashCommand | str | None:
    """None: plain text. str: literal text to send ('//x' -> '/x'). SlashCommand otherwise."""
    text = text.strip()
    if text.startswith("//"):
        return text[1:]
    match = _COMMAND.fullmatch(text)
    if match is None:
        return None
    return SlashCommand(match.group(1).lower(), (match.group(2) or "").strip())
