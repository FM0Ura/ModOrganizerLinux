"""Cancel and timeout stop the whole CLI process tree at once.

A CLI (claude, codex) starts children of its own (the MCP bridge, the user's hooks) that
inherit its stdout/stderr: killing only the CLI left them running and the turn waited for
them to close the pipes.
"""

from __future__ import annotations

import os
import threading
import time

import pytest

from ai_assistant.agent import cli_runner
from ai_assistant.agent.cli_runner import TurnCancelled, _spawn


def _alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    # a zombie not yet reaped by init still answers kill(0)
    try:
        with open(f"/proc/{pid}/stat") as f:
            return f.read().split(") ", 1)[1].split()[0] != "Z"
    except FileNotFoundError:
        return False


def _grandchild_cmd(pid_file) -> list[str]:
    # the grandchild keeps the inherited pipes open, like the CLI's own children
    return ["sh", "-c", f"sleep 30 & echo $! > {pid_file}; sleep 30"]


def _wait_for(pid_file):
    end = time.monotonic() + 5
    while time.monotonic() < end:
        if pid_file.exists() and pid_file.read_text().strip():
            return int(pid_file.read_text())
        time.sleep(0.02)
    raise AssertionError("the grandchild did not start")


def test_cancel_returns_at_once_and_kills_the_grandchildren(tmp_path):
    pid_file = tmp_path / "grandchild.pid"
    ev = threading.Event()
    started = time.monotonic()
    threading.Thread(target=lambda: (_wait_for(pid_file), ev.set()), daemon=True).start()
    with pytest.raises(TurnCancelled):
        _spawn(_grandchild_cmd(pid_file), tmp_path, ev)
    assert time.monotonic() - started < 5
    grandchild = int(pid_file.read_text())
    end = time.monotonic() + 2
    while _alive(grandchild) and time.monotonic() < end:
        time.sleep(0.02)
    assert not _alive(grandchild)


def test_timeout_kills_the_grandchildren(tmp_path, monkeypatch):
    monkeypatch.setattr(cli_runner, "CLI_TIMEOUT_SECONDS", 0.5)
    pid_file = tmp_path / "grandchild.pid"
    started = time.monotonic()
    with pytest.raises(RuntimeError, match="did not answer"):
        _spawn(_grandchild_cmd(pid_file), tmp_path, None)
    assert time.monotonic() - started < 5
    grandchild = _wait_for(pid_file)
    end = time.monotonic() + 2
    while _alive(grandchild) and time.monotonic() < end:
        time.sleep(0.02)
    assert not _alive(grandchild)


def test_normal_turn_still_returns_the_output(tmp_path):
    code, out, err = _spawn(["sh", "-c", "echo hi; echo oops >&2"], tmp_path, threading.Event())
    assert (code, out, err) == (0, "hi\n", "oops\n")
