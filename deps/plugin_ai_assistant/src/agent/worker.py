from __future__ import annotations

import threading
from pathlib import Path
from PyQt6.QtCore import QThread, pyqtSignal
from langchain_core.messages import HumanMessage
from ..config import AssistantConfig
from ..tools.dispatcher import MainThreadDispatcher
from .llm_factory import create_chat_model
from .graph import build_agent_graph
from .cli_runner import TurnCancelled, run_cli_turn

CLI_PROVIDERS = ("claude_cli", "codex_cli", "agy_cli")


def _message_text(content) -> str:
    """Anthropic models answer with a list of content blocks, the others with a string."""
    if isinstance(content, str):
        return content
    parts = []
    for block in content or []:
        if isinstance(block, dict):
            if block.get("type", "text") == "text":
                parts.append(block.get("text", ""))
        else:
            parts.append(str(block))
    return "".join(parts)


class AgentTurnWorker(QThread):
    """Runs one agent turn off the GUI thread.

    Everything that needs the organizer (system_prompt, cwd) is computed by the caller
    on the main thread; the tools reach the organizer only through the dispatcher.
    """

    response_ready = pyqtSignal(str)
    error_occurred = pyqtSignal(str)

    def __init__(
        self,
        dispatcher: MainThreadDispatcher,
        config: AssistantConfig,
        user_text: str,
        thread_id: str,
        system_prompt: str,
        cwd: Path | str | None,
        bridge_path: Path,
        endpoint_path: Path,
        sqlite_path: Path | None = None,
    ) -> None:
        super().__init__()
        self._dispatcher = dispatcher
        self._config = config
        self._user_text = user_text
        self._thread_id = thread_id
        self._system_prompt = system_prompt
        self._cwd = cwd
        self._bridge_path = bridge_path
        self._endpoint_path = endpoint_path
        self._sqlite_path = sqlite_path
        self._cancel = threading.Event()

    def cancel(self) -> None:
        """Stops a CLI turn at once; an API turn is abandoned and its answer dropped."""
        self._cancel.set()

    def run(self) -> None:
        try:
            if self._config.provider in CLI_PROVIDERS:
                reply = run_cli_turn(
                    self._config.provider,
                    self._user_text,
                    self._system_prompt,
                    self._bridge_path,
                    self._endpoint_path,
                    cwd=self._cwd,
                    cancel_event=self._cancel,
                ).text  # Task 3 wires the session id and note
            else:
                model = create_chat_model(self._config)
                graph = build_agent_graph(
                    self._dispatcher, model, sqlite_path=self._sqlite_path, system_prompt=self._system_prompt
                )
                result = graph.invoke(
                    {"messages": [HumanMessage(content=self._user_text)]},
                    config={"configurable": {"thread_id": self._thread_id}},
                )
                reply = _message_text(result["messages"][-1].content)
            if not self._cancel.is_set():
                self.response_ready.emit(reply)
        except TurnCancelled:
            pass
        except Exception as exc:
            if not self._cancel.is_set():
                self.error_occurred.emit(str(exc) or type(exc).__name__)
