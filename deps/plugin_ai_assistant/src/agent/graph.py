from __future__ import annotations

from contextlib import contextmanager
import sqlite3
from pathlib import Path
from langchain_core.language_models.chat_models import BaseChatModel
from langchain_core.messages import SystemMessage
from langgraph.graph import StateGraph, MessagesState, START, END
from langgraph.prebuilt import ToolNode
import langgraph.prebuilt.tool_node as _tool_node_mod
from langgraph.checkpoint.memory import MemorySaver
from langgraph.checkpoint.sqlite import SqliteSaver
from ..tools.dispatcher import MainThreadDispatcher


class _SyncExecutor:
    def map(self, fn, *iterables):
        return list(map(fn, *iterables))


@contextmanager
def _sync_executor_for_config(_config=None):
    yield _SyncExecutor()


class _InThreadToolNode(ToolNode):
    def _func(self, input, config, runtime):
        orig = _tool_node_mod.get_executor_for_config
        _tool_node_mod.get_executor_for_config = _sync_executor_for_config
        try:
            return super()._func(input, config, runtime)
        finally:
            _tool_node_mod.get_executor_for_config = orig



def build_system_prompt(organizer) -> str:
    game = organizer.managedGame()
    game_name = game.gameName() if game else "Unknown Game"
    profile_name = organizer.profileName()
    mod_list = organizer.modList()
    all_mods = mod_list.allModsByProfilePriority()
    active_mods = sum(1 for m in all_mods if (mod_list.state(m) & 0x02))
    return (
        "You are the Mod Organizer 2 Linux AI Assistant.\n"
        f"Active Game: {game_name} | Active Profile: {profile_name} | "
        f"Mods: {active_mods}/{len(all_mods)} active.\n"
        "Always inspect logs, file owners, or mod lists using read tools before proposing changes.\n"
        "Explain your findings concisely and accurately."
    )


def build_agent_graph(
    dispatcher: MainThreadDispatcher,
    model: BaseChatModel,
    sqlite_path: Path | None = None,
):
    safe_tools, write_tools = dispatcher._registry.to_langchain_tools(dispatcher)
    all_tools = safe_tools + write_tools
    write_names = {t.name for t in write_tools}
    bound_model = model.bind_tools(all_tools)

    def agent_node(state: MessagesState):
        sys_msg = SystemMessage(content=build_system_prompt(dispatcher._organizer))
        msgs = state["messages"]
        if not msgs or not isinstance(msgs[0], SystemMessage):
            msgs = [sys_msg] + list(msgs)
        response = bound_model.invoke(msgs)
        return {"messages": [response]}

    def route_after_agent(state: MessagesState) -> str:
        last = state["messages"][-1]
        tool_calls = getattr(last, "tool_calls", None) or []
        if not tool_calls:
            return END
        if any(tc.get("name") in write_names for tc in tool_calls):
            return "write_tools"
        return "safe_tools"

    builder = StateGraph(MessagesState)
    builder.add_node("agent", agent_node)
    builder.add_node("safe_tools", _InThreadToolNode(safe_tools))
    builder.add_node("write_tools", _InThreadToolNode(write_tools))

    builder.add_edge(START, "agent")
    builder.add_conditional_edges("agent", route_after_agent, ["safe_tools", "write_tools", END])
    builder.add_edge("safe_tools", "agent")
    builder.add_edge("write_tools", "agent")

    if sqlite_path is not None:
        sqlite_path.parent.mkdir(parents=True, exist_ok=True)
        conn = sqlite3.connect(str(sqlite_path), check_same_thread=False)
        checkpointer = SqliteSaver(conn)
    else:
        checkpointer = MemorySaver()

    return builder.compile(checkpointer=checkpointer)
