"""Entry point for the AI Assistant MO2 plugin."""

from __future__ import annotations

import os
import sys

_lib_dir = os.path.join(os.path.dirname(__file__), "lib")
if os.path.isdir(_lib_dir) and _lib_dir not in sys.path:
    sys.path.insert(0, _lib_dir)

from .plugin import AiAssistantPlugin  # noqa: E402


def createPlugin() -> AiAssistantPlugin:
    return AiAssistantPlugin()
