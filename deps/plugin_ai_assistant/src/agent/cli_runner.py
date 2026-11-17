from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import threading
import time
from pathlib import Path
from typing import Callable

from ..config import _in_container

CLI_TIMEOUT_SECONDS = 600


class TurnCancelled(Exception):
    """The user cancelled the turn before the CLI answered."""


def _write_private(path: Path, text: str) -> None:
    """Writes text to path with mode 0600, atomically."""
    fd, tmp = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(text)
        os.replace(tmp, path)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise


def prepare_bridge(data_dir: Path, endpoint_url: str, token: str) -> tuple[Path, Path]:
    """Copies the stdio bridge to the plugin's data folder and writes the endpoint file.

    The CLIs start the bridge with the host's python3. Inside the AppImage the plugin's
    own folder is a mount point that changes on every start, so the bridge is copied
    to a stable path; the endpoint (URL + token) goes to a 0600 file, so the token is
    never on a command line. Returns (bridge_path, endpoint_path).
    """
    data_dir = Path(data_dir)
    data_dir.mkdir(parents=True, exist_ok=True)
    src = Path(__file__).with_name("mcp_stdio_bridge.py")
    bridge = data_dir / "mcp_stdio_bridge.py"
    if not bridge.is_file() or bridge.read_bytes() != src.read_bytes():
        shutil.copyfile(src, bridge)
    endpoint = data_dir / "mcp-endpoint.json"
    _write_private(endpoint, json.dumps({"url": endpoint_url, "token": token}))
    return bridge, endpoint


def _host_prefix(force_host_exec: bool | None) -> list[str]:
    use_host = force_host_exec if force_host_exec is not None else (
        _in_container() and shutil.which("distrobox-host-exec") is not None
    )
    return ["distrobox-host-exec"] if use_host else []


def _bridge_args(bridge_path: Path, endpoint_path: Path) -> list[str]:
    return [str(bridge_path), "--config", str(endpoint_path)]


def build_cli_command(
    provider: str,
    prompt: str,
    bridge_path: Path,
    endpoint_path: Path,
    force_host_exec: bool | None = None,
) -> list[str]:
    prefix = _host_prefix(force_host_exec)
    args = _bridge_args(bridge_path, endpoint_path)

    if provider == "claude_cli":
        mcp = {"mcpServers": {"mo2": {"command": "python3", "args": args}}}
        return prefix + [
            "claude",
            "-p",
            prompt,
            "--mcp-config",
            json.dumps(mcp),
            "--allowedTools",
            "mcp__mo2__*",
        ]
    if provider == "codex_cli":
        return prefix + [
            "codex",
            "exec",
            "--skip-git-repo-check",
            "--sandbox",
            "read-only",
            "-c",
            'mcp_servers.mo2.command="python3"',
            "-c",
            f"mcp_servers.mo2.args={json.dumps(args)}",
            prompt,
        ]
    if provider == "agy_cli":
        # agy has no per-session MCP option: the server is registered once with
        # agy_register_command (Settings → "Register in Antigravity")
        return prefix + ["agy", "-p", prompt]
    raise ValueError(f"Unsupported CLI provider: {provider}")


def agy_register_command(
    bridge_path: Path, endpoint_path: Path, force_host_exec: bool | None = None
) -> list[str]:
    """`agy mcp add` for the MO2 server; the endpoint file is rewritten on every start."""
    return _host_prefix(force_host_exec) + [
        "agy", "mcp", "add", "mo2", "--", "python3", *_bridge_args(bridge_path, endpoint_path)
    ]


def run_cli_turn(
    provider: str,
    user_message: str,
    system_prompt: str,
    bridge_path: Path,
    endpoint_path: Path,
    cwd: Path | str | None = None,
    cancel_event: threading.Event | None = None,
    command_builder: Callable[..., list[str]] = build_cli_command,
) -> str:
    """Runs one turn of a CLI agent. Never touches the organizer: the caller builds
    system_prompt on the main thread. Raises TurnCancelled when cancel_event is set."""
    full_prompt = (
        f"{system_prompt}\n"
        "Use the tools of the 'mo2' MCP server to inspect and change this Mod Organizer instance.\n\n"
        f"User request: {user_message}"
    )
    cmd = command_builder(provider, full_prompt, bridge_path, endpoint_path)
    try:
        proc = subprocess.Popen(
            cmd,
            cwd=str(cwd) if cwd else None,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except FileNotFoundError as exc:
        raise RuntimeError(f"'{cmd[0]}' was not found in PATH.") from exc

    out: dict[str, str] = {}
    reader = threading.Thread(target=lambda: out.update(zip(("stdout", "stderr"), proc.communicate())))
    reader.start()
    deadline = time.monotonic() + CLI_TIMEOUT_SECONDS
    while reader.is_alive():
        if cancel_event is not None and cancel_event.is_set():
            proc.kill()
            reader.join()
            raise TurnCancelled()
        if time.monotonic() > deadline:
            proc.kill()
            reader.join()
            raise RuntimeError(f"The CLI did not answer within {CLI_TIMEOUT_SECONDS} s.")
        reader.join(timeout=0.1)

    stdout, stderr = out.get("stdout", ""), out.get("stderr", "")
    if proc.returncode != 0:
        raise RuntimeError((stderr or stdout or f"CLI exited with code {proc.returncode}").strip())
    return stdout.strip() or "(Empty response from CLI)"
