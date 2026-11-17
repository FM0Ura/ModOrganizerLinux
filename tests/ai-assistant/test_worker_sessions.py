from __future__ import annotations

from typing import Any
from langchain_core.language_models.chat_models import BaseChatModel
from langchain_core.messages import AIMessage, BaseMessage, HumanMessage
from langchain_core.outputs import ChatGeneration, ChatResult
from ai_assistant.config import AssistantConfig
from ai_assistant.tools import MainThreadDispatcher, build_default_registry
from ai_assistant.agent import worker as worker_mod
from ai_assistant.agent.cli_runner import CliTurnResult
from ai_assistant.agent.worker import AgentTurnWorker


class CountingModel(BaseChatModel):
    on_call: Any = None

    @property
    def _llm_type(self) -> str:
        return "counting-test"

    def bind_tools(self, tools, **kwargs):
        return self

    def _generate(self, messages: list[BaseMessage], stop=None, run_manager=None, **kwargs: Any) -> ChatResult:
        if self.on_call:
            self.on_call()
        n = sum(1 for m in messages if isinstance(m, HumanMessage))
        return ChatResult(generations=[ChatGeneration(message=AIMessage(content=f"{n} messages"))])


def _make(cfg, tmp_path, dispatcher=None, session_id=None, **kw):
    return AgentTurnWorker(
        dispatcher=dispatcher,
        config=cfg,
        user_text="hello",
        session_id=session_id,
        system_prompt="sys",
        cwd=None,
        bridge_path=tmp_path / "bridge.py",
        endpoint_path=tmp_path / "endpoint.json",
        **kw,
    )


def _record(w):
    events = []
    w.session_started.connect(lambda s: events.append(("session", s)))
    w.note_ready.connect(lambda s: events.append(("note", s)))
    w.response_ready.connect(lambda s: events.append(("response", s)))
    w.late_response.connect(lambda s: events.append(("late", s)))
    w.error_occurred.connect(lambda s: events.append(("error", s)))
    return events


def test_cli_worker_emits_session_then_response(monkeypatch, tmp_path):
    calls = []

    def fake(*args, **kwargs):
        calls.append(kwargs)
        return CliTurnResult("hi", "sid-1", "note")

    monkeypatch.setattr(worker_mod, "run_cli_turn", fake)
    w = _make(AssistantConfig(provider="codex_cli"), tmp_path)
    events = _record(w)
    w.run()
    assert events == [("session", "sid-1"), ("note", "note"), ("response", "hi")]
    assert calls[0]["resume"] is False and calls[0]["session_id"] is None


def test_claude_worker_generates_uuid_and_resume(monkeypatch, tmp_path):
    calls = []

    def fake(*args, **kwargs):
        calls.append(kwargs)
        return CliTurnResult("hi", kwargs["session_id"], None)

    monkeypatch.setattr(worker_mod, "run_cli_turn", fake)
    w = _make(AssistantConfig(provider="claude_cli"), tmp_path)
    events = _record(w)
    w.run()
    sid = calls[0]["session_id"]
    assert sid and calls[0]["resume"] is False
    assert events == [("session", sid), ("response", "hi")]

    w2 = _make(AssistantConfig(provider="claude_cli"), tmp_path, session_id="abc")
    events2 = _record(w2)
    w2.run()
    assert calls[1]["session_id"] == "abc" and calls[1]["resume"] is True
    assert events2 == [("response", "hi")]


def test_lost_session_emits_new_id(monkeypatch, tmp_path):
    monkeypatch.setattr(worker_mod, "run_cli_turn", lambda *a, **k: CliTurnResult("hi", "new", "lost"))
    w = _make(AssistantConfig(provider="codex_cli"), tmp_path, session_id="old")
    events = _record(w)
    w.run()
    assert events == [("session", "new"), ("note", "lost"), ("response", "hi")]


def test_api_thread_id_reused_across_workers(monkeypatch, tmp_path, fake_organizer):
    disp = MainThreadDispatcher(fake_organizer, build_default_registry(), permission_mode="autonomous", vfs_mounted_check=lambda: False)
    monkeypatch.setattr(worker_mod, "create_chat_model", lambda cfg: CountingModel())
    cfg = AssistantConfig(provider="openai", api_keys={"openai": "sk-test"})
    db = tmp_path / "checkpoints.sqlite"
    replies = []
    for _ in range(2):
        w = _make(cfg, tmp_path, dispatcher=disp, session_id="thread-1", sqlite_path=db)
        w.response_ready.connect(replies.append)
        w.run()
    assert replies == ["1 messages", "2 messages"]


def test_cancelled_api_turn_emits_late_response(monkeypatch, tmp_path, fake_organizer):
    disp = MainThreadDispatcher(fake_organizer, build_default_registry(), permission_mode="autonomous", vfs_mounted_check=lambda: False)
    holder = {}
    monkeypatch.setattr(worker_mod, "create_chat_model", lambda cfg: CountingModel(on_call=lambda: holder["w"].cancel()))
    cfg = AssistantConfig(provider="openai", api_keys={"openai": "sk-test"})
    w = _make(cfg, tmp_path, dispatcher=disp, sqlite_path=tmp_path / "c.sqlite")
    holder["w"] = w
    events = _record(w)
    w.run()
    kinds = [k for k, _ in events]
    assert "response" not in kinds
    assert ("late", "1 messages") in events
