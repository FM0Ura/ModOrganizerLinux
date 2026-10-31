from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from ..config import _in_container
from .graph import build_system_prompt


def write_mcp_config_file(endpoint_url: str, token: str) -> Path:
    bridge_script = str(Path(__file__).with_name("mcp_stdio_bridge.py").resolve())
    cfg = {
        "mcpServers": {
            "mo2": {
                "command": "python3",
                "args": [bridge_script],
                "env": {
                    "MO2_MCP_URL": endpoint_url,
                    "MO2_MCP_TOKEN": token,
                },
            }
        }
    }
    fd, path = tempfile.mkstemp(prefix="mo2-mcp-", suffix=".json")
    os.chmod(path, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
    return Path(path)


def build_cli_command(
    provider: str,
    prompt: str,
    mcp_config_path: Path,
    force_host_exec: bool | None = None,
) -> list[str]:
    use_host = force_host_exec if force_host_exec is not None else (
        _in_container() and shutil.which("distrobox-host-exec") is not None
    )
    prefix = ["distrobox-host-exec"] if use_host else []

    if provider == "claude_cli":
        return prefix + [
            "claude",
            "-p",
            prompt,
            "--mcp-config",
            str(mcp_config_path),
            "--allowedTools",
            "mcp__mo2__*",
        ]
    if provider == "codex_cli":
        return prefix + ["codex", "exec", prompt]
    if provider == "agy_cli":
        return prefix + ["agy", "-p", prompt]
    raise ValueError(f"Unsupported CLI provider: {provider}")


def run_cli_turn(
    organizer,
    provider: str,
    user_message: str,
    endpoint_url: str,
    token: str,
) -> str:
    sys_context = build_system_prompt(organizer)
    full_prompt = (
        f"{sys_context}\n"
        f"An MCP server for Mod Organizer 2 is available at {endpoint_url}.\n\n"
        f"User request: {user_message}"
    )
    cfg_path = write_mcp_config_file(endpoint_url, token)
    try:
        cmd = build_cli_command(provider, full_prompt, cfg_path)
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=300,
            check=False,
        )
        if proc.returncode != 0:
            err = (proc.stderr or proc.stdout or f"CLI exited with code {proc.returncode}").strip()
            raise RuntimeError(err)
        return proc.stdout.strip() or "(Empty response from CLI)"
    finally:
        cfg_path.unlink(missing_ok=True)
