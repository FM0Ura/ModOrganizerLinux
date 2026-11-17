"""Regression tests for the defects found in the 2026-11-17 review of the AI assistant."""

from __future__ import annotations

import json
import os
import stat
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

import pytest
from langchain_core.messages import AIMessage
from PyQt6.QtCore import QObject, pyqtSignal
from PyQt6.QtWidgets import QApplication

from ai_assistant.agent import cli_runner
from ai_assistant.agent.cli_runner import (
    TurnCancelled,
    agy_register_command,
    build_cli_command,
    prepare_bridge,
    run_cli_turn,
)
from ai_assistant.agent.graph import build_agent_graph
from ai_assistant.agent.llm_factory import DEFAULT_MODELS
from ai_assistant.agent.mcp_server import LocalMcpServer
from ai_assistant.config import AssistantConfig
from ai_assistant.tools import MainThreadDispatcher, build_default_registry
from ai_assistant.ui import chat_panel as chat_panel_mod
from ai_assistant.ui.chat_panel import ChatPanel

from test_langgraph_agent import ScriptedToolChatModel
from test_mcp_and_cli import _post_json


def _dispatcher(org) -> MainThreadDispatcher:
    return MainThreadDispatcher(org, build_default_registry(), permission_mode="autonomous", vfs_mounted_check=lambda: False)


# --- MCP server: JSON-RPC notifications ----------------------------------------------


def test_mcp_notification_gets_no_json_rpc_reply(fake_organizer):
    srv = LocalMcpServer(_dispatcher(fake_organizer))
    srv.start()
    try:
        assert srv._handle_rpc({"jsonrpc": "2.0", "method": "notifications/initialized"}) is None
        req = urllib.request.Request(
            srv.endpoint_url(),
            data=json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}).encode(),
            headers={"Content-Type": "application/json", "X-MO2-Token": srv.token},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=5) as resp:  # no tool call: no Qt event loop needed
            assert (resp.status, resp.read()) == (202, b"")
    finally:
        srv.stop()


def test_mcp_answers_ping(fake_organizer):
    srv = LocalMcpServer(_dispatcher(fake_organizer))
    assert srv._handle_rpc({"jsonrpc": "2.0", "id": 7, "method": "ping"}) == {"jsonrpc": "2.0", "id": 7, "result": {}}


# --- stdio bridge: stable copy, endpoint file, no output for notifications ------------


def test_prepare_bridge_writes_private_endpoint_file(tmp_path: Path):
    bridge, endpoint = prepare_bridge(tmp_path, "http://127.0.0.1:5/mcp", "secret-token")
    assert bridge.parent == tmp_path and bridge.is_file()
    assert json.loads(endpoint.read_text()) == {"url": "http://127.0.0.1:5/mcp", "token": "secret-token"}
    assert stat.S_IMODE(endpoint.stat().st_mode) == 0o600


def test_bridge_relays_requests_and_stays_silent_on_notifications(fake_organizer, tmp_path: Path):
    srv = LocalMcpServer(_dispatcher(fake_organizer))
    srv.start()
    try:
        bridge, endpoint = prepare_bridge(tmp_path, srv.endpoint_url(), srv.token)
        lines = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
        ]
        env = {k: v for k, v in os.environ.items() if not k.startswith("MO2_MCP_")}
        proc = subprocess.run(
            [sys.executable, str(bridge), "--config", str(endpoint)],
            input="".join(json.dumps(m) + "\n" for m in lines),
            capture_output=True, text=True, timeout=20, env=env,
        )
        replies = [json.loads(l) for l in proc.stdout.splitlines() if l.strip()]
        assert [r["id"] for r in replies] == [1, 2], proc.stderr
        assert len(replies[1]["result"]["tools"]) == 14
    finally:
        srv.stop()


# --- CLI command lines: every CLI gets the MO2 tools, no token in argv ----------------


def test_claude_and_codex_commands_carry_the_mo2_mcp_server(tmp_path: Path):
    bridge, endpoint = prepare_bridge(tmp_path, "http://127.0.0.1:5/mcp", "secret-token")
    claude = build_cli_command("claude_cli", "hi", bridge, endpoint, force_host_exec=False)
    mcp_json = json.loads(claude[claude.index("--mcp-config") + 1])
    assert mcp_json["mcpServers"]["mo2"]["args"] == [str(bridge), "--config", str(endpoint)]

    codex = build_cli_command("codex_cli", "hi", bridge, endpoint, force_host_exec=False)
    assert codex[:2] == ["codex", "exec"]
    joined = " ".join(codex)
    assert "mcp_servers.mo2.command" in joined and str(endpoint) in joined

    for cmd in (claude, codex):
        assert not any("secret-token" in part for part in cmd)


def test_agy_register_command_points_to_the_stable_bridge(tmp_path: Path):
    bridge, endpoint = prepare_bridge(tmp_path, "http://127.0.0.1:5/mcp", "secret-token")
    cmd = agy_register_command(bridge, endpoint, force_host_exec=False)
    assert cmd[:4] == ["agy", "mcp", "add", "mo2"]
    assert cmd[cmd.index("--") + 1:] == ["python3", str(bridge), "--config", str(endpoint)]


# --- CLI turn: working directory and cancellation --------------------------------------


def test_cli_turn_runs_in_the_given_directory(tmp_path: Path):
    work = tmp_path / "instance"
    work.mkdir()
    reply = run_cli_turn(
        "claude_cli", "hi", "context", tmp_path / "b.py", tmp_path / "e.json", cwd=work,
        command_builder=lambda *a, **k: ["sh", "-c", "pwd"],
    )
    assert Path(reply) == work


def test_cli_turn_can_be_cancelled(tmp_path: Path):
    cancel = threading.Event()
    threading.Timer(0.3, cancel.set).start()
    start = time.monotonic()
    with pytest.raises(TurnCancelled):
        run_cli_turn(
            "claude_cli", "hi", "context", tmp_path / "b.py", tmp_path / "e.json", cwd=tmp_path,
            cancel_event=cancel, command_builder=lambda *a, **k: ["sleep", "30"],
        )
    assert time.monotonic() - start < 5


# --- LangGraph: the organizer is not touched from the worker thread --------------------


class _NoTouchOrganizer:
    def __getattr__(self, name):
        raise AssertionError(f"organizer.{name} used outside the main thread")


def test_graph_uses_the_given_system_prompt_without_the_organizer():
    seen = []

    class Recording(ScriptedToolChatModel):
        def _generate(self, messages, stop=None, run_manager=None, **kwargs):
            seen.append(messages[0].content)
            return super()._generate(messages, stop, run_manager, **kwargs)

    disp = _dispatcher(_NoTouchOrganizer())
    graph = build_agent_graph(disp, Recording(responses=[AIMessage(content="ok")]), system_prompt="CTX-123")
    out = graph.invoke({"messages": [("user", "hi")]}, config={"configurable": {"thread_id": "t"}})
    assert out["messages"][-1].content == "ok"
    assert seen == ["CTX-123"]


# --- chat panel -------------------------------------------------------------------------


class _FakeWorker(QObject):
    response_ready = pyqtSignal(str)
    error_occurred = pyqtSignal(str)
    finished = pyqtSignal()
    instances: list["_FakeWorker"] = []

    def __init__(self, **kwargs):
        super().__init__()
        self.kwargs = kwargs
        self.cancelled = False
        self.running = False
        _FakeWorker.instances.append(self)

    def start(self):
        self.running = True

    def isRunning(self):
        return self.running

    def cancel(self):
        self.cancelled = True

    def finish(self, reply: str | None = None):
        if reply is not None:
            self.response_ready.emit(reply)
        self.running = False
        self.finished.emit()


@pytest.fixture
def panel(fake_organizer, tmp_path, monkeypatch):
    _FakeWorker.instances.clear()
    monkeypatch.setattr(chat_panel_mod, "AgentTurnWorker", _FakeWorker)
    disp = _dispatcher(fake_organizer)
    srv = LocalMcpServer(disp)
    srv.start()
    p = ChatPanel(disp, srv, tmp_path / "data")
    yield p
    srv.stop()


def test_provider_and_mode_choices_are_saved(panel, tmp_path):
    panel.provider_combo.setCurrentIndex(panel.provider_combo.findData("gemini"))
    panel.mode_combo.setCurrentIndex(panel.mode_combo.findData("autonomous"))
    saved = AssistantConfig.load(tmp_path / "data")
    assert (saved.provider, saved.permission_mode) == ("gemini", "autonomous")


def test_turn_context_is_built_on_the_main_thread(panel, fake_organizer):
    panel.send_prompt("hello")
    kw = _FakeWorker.instances[-1].kwargs
    assert "Skyrim Special Edition" in kw["system_prompt"]
    assert Path(kw["cwd"]) == Path(fake_organizer.basePath())
    assert "dispatcher" in kw and kw["bridge_path"].is_file()


def test_busy_state_is_visible_and_cancellable(panel):
    panel.send_prompt("hello")
    worker = _FakeWorker.instances[-1]
    assert not panel.status_label.isHidden() and not panel.cancel_btn.isHidden()
    assert not panel.send_btn.isEnabled()

    panel.cancel_btn.click()
    assert worker.cancelled
    worker.finish("late reply")
    assert "late reply" not in panel.transcript.toPlainText()
    assert panel.status_label.isHidden() and panel.cancel_btn.isHidden()
    assert panel.send_btn.isEnabled()


def test_transcript_escapes_user_text_and_renders_markdown(panel):
    panel.send_prompt("<b>not bold</b>")
    _FakeWorker.instances[-1].finish("**bold** and <i>tag</i>")
    text = panel.transcript.toPlainText()
    assert "<b>not bold</b>" in text
    assert "bold and <i>tag</i>" in text
    assert "**" not in text


# --- defaults -------------------------------------------------------------------------


def test_default_models_are_current():
    assert DEFAULT_MODELS["anthropic"] == "claude-sonnet-5-5"
    assert DEFAULT_MODELS["gemini"] == "gemini-flash-latest"
    assert DEFAULT_MODELS["openai"] == "gpt-5.5"


def test_anthropic_content_blocks_become_plain_text():
    from ai_assistant.agent.worker import _message_text

    blocks = [{"type": "text", "text": "Hello "}, {"type": "tool_use", "id": "x"}, {"type": "text", "text": "there"}]
    assert _message_text(blocks) == "Hello there"
    assert _message_text("plain") == "plain"


def test_approval_card_escapes_tool_arguments():
    from ai_assistant.ui.approval_card import ApprovalCard
    from PyQt6.QtWidgets import QLabel

    card = ApprovalCard("r", "remove_mod", {"mod_name": "<b>x</b>"})
    texts = [lbl.text() for lbl in card.findChildren(QLabel)]
    assert any("&lt;b&gt;x&lt;/b&gt;" in t for t in texts)


def test_settings_registers_mo2_in_antigravity(tmp_path):
    from ai_assistant.ui.settings_dialog import AssistantSettingsDialog

    bridge, endpoint = prepare_bridge(tmp_path, "http://127.0.0.1:5/mcp", "tok")
    calls = []

    def fake_run(cmd):
        calls.append(cmd)
        return subprocess.CompletedProcess(cmd, 1, "", "failed to parse mcp_config.json")

    dlg = AssistantSettingsDialog(AssistantConfig(), tmp_path, bridge_path=bridge, endpoint_path=endpoint, run_command=fake_run)
    dlg.agy_register_btn.click()
    assert calls == [agy_register_command(bridge, endpoint)]
    assert "failed to parse mcp_config.json" in dlg.agy_status.text()
