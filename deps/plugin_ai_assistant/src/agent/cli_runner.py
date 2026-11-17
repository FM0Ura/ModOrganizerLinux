from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import threading
import time
import uuid
from dataclasses import dataclass
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
    *,
    session_id: str | None = None,
    resume: bool = False,
    system_prompt: str | None = None,
) -> list[str]:
    """Command line for one turn. `session_id` + `resume=False` starts a session (Claude
    only: the id is chosen by the caller); `resume=True` continues the session."""
    prefix = _host_prefix(force_host_exec)
    args = _bridge_args(bridge_path, endpoint_path)
    mcp_command = ["-c", 'mcp_servers.mo2.command="python3"', "-c", f"mcp_servers.mo2.args={json.dumps(args)}"]

    if provider == "claude_cli":
        mcp = {"mcpServers": {"mo2": {"command": "python3", "args": args}}}
        cmd = ["claude", "-p", prompt]
        if session_id:
            cmd += ["--resume" if resume else "--session-id", session_id]
        if system_prompt:
            cmd += ["--append-system-prompt", system_prompt]
        return prefix + cmd + ["--mcp-config", json.dumps(mcp), "--allowedTools", "mcp__mo2__*"]
    if provider == "codex_cli":
        if resume:
            if not session_id:
                raise ValueError("resume=True requires a session_id")
            return prefix + [
                "codex", "exec", "resume", "--json", "--skip-git-repo-check",
                "-c", 'sandbox_mode="read-only"', *mcp_command, session_id, prompt,
            ]
        return prefix + [
            "codex", "exec", "--json", "--skip-git-repo-check", "--sandbox", "read-only",
            *mcp_command, prompt,
        ]
    if provider == "agy_cli":
        # agy has no per-session MCP option: the server is registered once with
        # agy_register_command (Settings → "Register in Antigravity")
        cmd = ["agy", "-p", prompt, "--output-format", "json"]
        if resume:
            if not session_id:
                raise ValueError("resume=True requires a session_id")
            cmd += ["--conversation", session_id]
        return prefix + cmd
    raise ValueError(f"Unsupported CLI provider: {provider}")


def agy_register_command(
    bridge_path: Path, endpoint_path: Path, force_host_exec: bool | None = None
) -> list[str]:
    """`agy mcp add` for the MO2 server; the endpoint file is rewritten on every start."""
    return _host_prefix(force_host_exec) + [
        "agy", "mcp", "add", "mo2", "--", "python3", *_bridge_args(bridge_path, endpoint_path)
    ]


@dataclass
class CliTurnResult:
    text: str
    session_id: str | None
    note: str | None = None


# Printed by the CLIs when asked to resume a session they no longer have. Claude exits 0
# with the first one; Codex fails with the second. agy has no marker: a failed resume is
# detected by the exit code alone.
LOST_SESSION_MARKERS = (
    "No conversation found with session ID",
    "no rollout found for thread id",
)

_PROVIDER_NAMES = {"claude_cli": "Claude", "codex_cli": "Codex", "agy_cli": "Antigravity"}
_HISTORY_LIMIT = 8000
_INSTRUCTION = "Use the tools of the 'mo2' MCP server to inspect and change this Mod Organizer instance."


def parse_codex_jsonl(stdout: str) -> tuple[str | None, str]:
    """(thread id, text of the last agent message) from `codex exec --json` output."""
    thread_id: str | None = None
    text = ""
    for line in stdout.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            event = json.loads(line)
        except ValueError:
            continue
        if not isinstance(event, dict):
            continue
        if event.get("type") == "thread.started" and event.get("thread_id"):
            thread_id = str(event["thread_id"])
        elif event.get("type") == "item.completed":
            item = event.get("item") or {}
            if item.get("type") == "agent_message" and isinstance(item.get("text"), str):
                text = item["text"]
    return thread_id, text


def _parse_agy_json(stdout: str) -> tuple[str | None, str]:
    try:
        data = json.loads(stdout)
    except ValueError:
        return None, stdout
    if not isinstance(data, dict):
        return None, stdout
    cid = data.get("conversation_id")
    return (str(cid) if cid else None), str(data.get("response") or "")


def _format_history(history: list[tuple[str, str]] | None) -> str:
    """Most recent messages first until the limit, printed oldest to newest."""
    if not history:
        return ""
    kept: list[str] = []
    used = 0
    for role, content in reversed(history):
        entry = f"{'Assistant' if role == 'assistant' else 'User'}: {content}"
        if used + len(entry) > _HISTORY_LIMIT and kept:
            break
        kept.append(entry[:_HISTORY_LIMIT])
        used += len(entry)
    return "Conversation so far:\n" + "\n".join(reversed(kept)) + "\n\n"


def _spawn(cmd: list[str], cwd, cancel_event) -> tuple[int, str, str]:
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
    return proc.returncode, out.get("stdout", ""), out.get("stderr", "")


def run_cli_turn(
    provider: str,
    user_message: str,
    system_prompt: str,
    bridge_path: Path,
    endpoint_path: Path,
    cwd: Path | str | None = None,
    cancel_event: threading.Event | None = None,
    command_builder: Callable[..., list[str]] = build_cli_command,
    *,
    session_id: str | None = None,
    resume: bool = False,
    history: list[tuple[str, str]] | None = None,
) -> CliTurnResult:
    """Runs one turn of a CLI agent. Never touches the organizer: the caller builds
    system_prompt on the main thread. Raises TurnCancelled when cancel_event is set.

    resume=False starts a session (Claude needs the caller's uuid in session_id; Codex and
    agy report theirs). resume=True needs session_id; if the CLI lost the session, the turn
    is run once more as a first turn and the result carries a note."""
    if resume and not session_id:
        raise ValueError("resume=True requires a session_id")

    def attempt(sid: str | None, resuming: bool) -> tuple[int, str, str, CliTurnResult]:
        if provider == "claude_cli":
            prompt = f"{_INSTRUCTION}\n\nUser request: {user_message}"
        else:
            prompt = f"{system_prompt}\n{_INSTRUCTION}\n\n"
            if provider == "agy_cli" and not resuming:
                prompt += _format_history(history)
            prompt += f"User request: {user_message}"
        cmd = command_builder(
            provider, prompt, bridge_path, endpoint_path,
            session_id=sid, resume=resuming, system_prompt=system_prompt,
        )
        code, stdout, stderr = _spawn(cmd, cwd, cancel_event)
        new_sid, text = sid, stdout.strip()
        if provider == "codex_cli":
            thread, text = parse_codex_jsonl(stdout)
            new_sid = thread or sid
        elif provider == "agy_cli":
            cid, text = _parse_agy_json(stdout)
            new_sid = cid or sid
        return code, stdout, stderr, CliTurnResult(text.strip() or "(Empty response from CLI)", new_sid)

    code, stdout, stderr, result = attempt(session_id, resume)
    lost = resume and (code != 0 or any(m in stdout or m in stderr for m in LOST_SESSION_MARKERS))
    if lost:
        new_id = str(uuid.uuid4()) if provider == "claude_cli" else None
        code, stdout, stderr, result = attempt(new_id, False)
        result.note = (
            f"The {_PROVIDER_NAMES.get(provider, provider)} memory of this conversation was lost; "
            "continuing in a new session."
        )
    if code != 0:
        raise RuntimeError((stderr or stdout or f"CLI exited with code {code}").strip())
    return result
