from __future__ import annotations

from pathlib import Path
from typing import Any
from langchain_core.language_models.chat_models import BaseChatModel
from langchain_core.messages import BaseMessage, AIMessage, HumanMessage
from langchain_core.outputs import ChatResult, ChatGeneration
from ai_assistant.config import AssistantConfig
from ai_assistant.tools import build_default_registry, MainThreadDispatcher
from ai_assistant.agent.llm_factory import create_chat_model
from ai_assistant.agent.graph import build_agent_graph, build_system_prompt


class ScriptedToolChatModel(BaseChatModel):
    responses: list[AIMessage]
    call_idx: int = 0

    @property
    def _llm_type(self) -> str:
        return "scripted-test"

    def bind_tools(self, tools, **kwargs):
        return self

    def _generate(self, messages: list[BaseMessage], stop=None, run_manager=None, **kwargs: Any) -> ChatResult:
        msg = self.responses[min(self.call_idx, len(self.responses) - 1)]
        self.call_idx += 1
        return ChatResult(generations=[ChatGeneration(message=msg)])


def test_llm_factory_instantiates_providers():
    for provider, key in [("ollama", ""), ("openai", "sk-test"), ("gemini", "AIza-test"), ("anthropic", "sk-ant-test")]:
        cfg = AssistantConfig(provider=provider, api_keys={provider: key})
        llm = create_chat_model(cfg)
        assert llm is not None


def test_langgraph_executes_safe_and_write_tools_with_sqlite_checkpoint(fake_organizer, tmp_path: Path):
    reg = build_default_registry()
    disp = MainThreadDispatcher(
        fake_organizer,
        reg,
        permission_mode="autonomous",
        vfs_mounted_check=lambda: False,
    )
    scripted = ScriptedToolChatModel(
        responses=[
            AIMessage(
                content="",
                tool_calls=[{"name": "list_mods", "args": {"filter_query": "Lux"}, "id": "call_1", "type": "tool_call"}],
            ),
            AIMessage(
                content="",
                tool_calls=[
                    {"name": "set_mods_active", "args": {"mod_names": ["Lux"], "active": True}, "id": "call_2", "type": "tool_call"}
                ],
            ),
            AIMessage(content="I have enabled Lux for you."),
        ]
    )
    db_path = tmp_path / "checkpoints.sqlite"
    graph = build_agent_graph(disp, scripted, sqlite_path=db_path)
    sys_prompt = build_system_prompt(fake_organizer)
    out = graph.invoke(
        {"messages": [HumanMessage(content=f"{sys_prompt}\nEnable Lux please")]},
        config={"configurable": {"thread_id": "test-thread-1"}},
    )
    assert out["messages"][-1].content == "I have enabled Lux for you."
    assert fake_organizer.modList().state("Lux") == 0x02
    assert db_path.exists()
