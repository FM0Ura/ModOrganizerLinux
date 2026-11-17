from __future__ import annotations

import json
import threading
import urllib.request
import urllib.error
from pathlib import Path
import pytest
from PyQt6.QtWidgets import QApplication
from ai_assistant.tools import build_default_registry, MainThreadDispatcher
from ai_assistant.agent.mcp_server import LocalMcpServer
from ai_assistant.agent.cli_runner import build_cli_command


def _post_json(url: str, payload: dict, token: str | None = None) -> tuple[int, dict]:
    data = json.dumps(payload).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if token is not None:
        headers["X-MO2-Token"] = token
    req = urllib.request.Request(url, data=data, headers=headers, method="POST")
    out: dict[str, tuple[int, dict] | BaseException] = {}

    def _do_req() -> None:
        try:
            with urllib.request.urlopen(req, timeout=5) as resp:
                out["res"] = (resp.status, json.loads(resp.read().decode("utf-8")))
        except urllib.error.HTTPError as e:
            out["res"] = (e.code, {})
        except BaseException as exc:
            out["err"] = exc

    t = threading.Thread(target=_do_req)
    t.start()
    app = QApplication.instance()
    while t.is_alive():
        if app is not None:
            app.processEvents()
        t.join(timeout=0.01)
    if "err" in out:
        raise out["err"]  # type: ignore[misc]
    return out["res"]  # type: ignore[return-value]


def test_mcp_rejects_unauthorized_requests(fake_organizer):
    reg = build_default_registry()
    disp = MainThreadDispatcher(fake_organizer, reg, permission_mode="autonomous", vfs_mounted_check=lambda: False)
    srv = LocalMcpServer(disp)
    srv.start()
    try:
        code, _ = _post_json(srv.endpoint_url(), {"jsonrpc": "2.0", "id": 1, "method": "tools/list"}, token="wrong-token")
        assert code == 403
    finally:
        srv.stop()


def test_mcp_lists_and_calls_tools(fake_organizer):
    reg = build_default_registry()
    disp = MainThreadDispatcher(fake_organizer, reg, permission_mode="autonomous", vfs_mounted_check=lambda: False)
    srv = LocalMcpServer(disp)
    srv.start()
    try:
        code, resp = _post_json(
            srv.endpoint_url(),
            {"jsonrpc": "2.0", "id": 1, "method": "tools/list"},
            token=srv.token,
        )
        assert code == 200
        tools = resp["result"]["tools"]
        assert len(tools) == 14

        code, call_resp = _post_json(
            srv.endpoint_url(),
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "tools/call",
                "params": {"name": "set_mods_active", "arguments": {"mod_names": ["Lux"], "active": True}},
            },
            token=srv.token,
        )
        assert code == 200
        assert call_resp["result"]["isError"] is False
        assert fake_organizer.modList().state("Lux") == 0x02
    finally:
        srv.stop()


def test_build_cli_command_supports_claude_codex_and_agy(tmp_path: Path):
    bridge, endpoint = tmp_path / "bridge.py", tmp_path / "endpoint.json"
    cmd_claude = build_cli_command("claude_cli", "Check crash log", bridge, endpoint, force_host_exec=True)
    assert cmd_claude[0] == "distrobox-host-exec"
    assert "claude" in cmd_claude

    cmd_codex = build_cli_command("codex_cli", "List mods", bridge, endpoint, force_host_exec=False)
    assert cmd_codex[0] == "codex"

    cmd_agy = build_cli_command("agy_cli", "List mods", bridge, endpoint, force_host_exec=False)
    assert cmd_agy[0] == "agy"
