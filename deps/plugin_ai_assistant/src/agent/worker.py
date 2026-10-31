from __future__ import annotations

from pathlib import Path
from PyQt6.QtCore import QThread, pyqtSignal
from langchain_core.messages import HumanMessage
from ..config import AssistantConfig
from ..tools.dispatcher import MainThreadDispatcher
from .llm_factory import create_chat_model
from .graph import build_agent_graph
from .cli_runner import run_cli_turn


class AgentTurnWorker(QThread):
    response_ready = pyqtSignal(str)
    error_occurred = pyqtSignal(str)

    def __init__(
        self,
        dispatcher: MainThreadDispatcher,
        config: AssistantConfig,
        user_text: str,
        thread_id: str,
        mcp_endpoint: str,
        mcp_token: str,
        sqlite_path: Path | None = None,
    ) -> None:
        super().__init__()
        self._dispatcher = dispatcher
        self._config = config
        self._user_text = user_text
        self._thread_id = thread_id
        self._mcp_endpoint = mcp_endpoint
        self._mcp_token = mcp_token
        self._sqlite_path = sqlite_path

    def run(self) -> None:
        try:
            if self._config.provider in ("claude_cli", "codex_cli", "agy_cli"):
                reply = run_cli_turn(
                    self._dispatcher._organizer,
                    self._config.provider,
                    self._user_text,
                    self._mcp_endpoint,
                    self._mcp_token,
                )
                self.response_ready.emit(reply)
                return

            model = create_chat_model(self._config)
            graph = build_agent_graph(self._dispatcher, model, sqlite_path=self._sqlite_path)
            result = graph.invoke(
                {"messages": [HumanMessage(content=self._user_text)]},
                config={"configurable": {"thread_id": self._thread_id}},
            )
            last_msg = result["messages"][-1]
            self.response_ready.emit(str(last_msg.content))
        except Exception as exc:
            self.error_occurred.emit(str(exc))
