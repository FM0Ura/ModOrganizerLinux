from __future__ import annotations

import json
import sys
import threading
from pathlib import Path

import pytest

from ai_assistant.agent.cli_runner import (
    LOST_SESSION_MARKERS,
    CliTurnResult,
    TurnCancelled,
    build_cli_command,
    parse_codex_jsonl,
    run_cli_turn,
)

FIXTURE = Path(__file__).parent / "fixtures" / "codex_exec_json.jsonl"
UUID = "11111111-2222-3333-4444-555555555555"


def _paths(tmp_path: Path):
    return tmp_path / "bridge.py", tmp_path / "endpoint.json"


def _bridge_args(bridge: Path, endpoint: Path) -> list[str]:
    return [str(bridge), "--config", str(endpoint)]


def test_claude_first_turn_and_resume_args(tmp_path: Path):
    bridge, endpoint = _paths(tmp_path)
    mcp = json.dumps({"mcpServers": {"mo2": {"command": "python3", "args": _bridge_args(bridge, endpoint)}}})
    first = build_cli_command(
        "claude_cli", "hello", bridge, endpoint, False, session_id=UUID, resume=False, system_prompt="CTX"
    )
    assert first == [
        "claude", "-p", "hello", "--session-id", UUID, "--append-system-prompt", "CTX",
        "--mcp-config", mcp, "--allowedTools", "mcp__mo2__*",
    ]
    again = build_cli_command(
        "claude_cli", "hello", bridge, endpoint, False, session_id=UUID, resume=True, system_prompt="CTX"
    )
    assert again[3:5] == ["--resume", UUID]
    assert "--session-id" not in again


def test_codex_first_turn_and_resume_args(tmp_path: Path):
    bridge, endpoint = _paths(tmp_path)
    args = json.dumps(_bridge_args(bridge, endpoint))
    first = build_cli_command("codex_cli", "hi", bridge, endpoint, False)
    assert first == [
        "codex", "exec", "--json", "--skip-git-repo-check", "--sandbox", "read-only",
        "-c", 'mcp_servers.mo2.command="python3"', "-c", f"mcp_servers.mo2.args={args}", "hi",
    ]
    again = build_cli_command("codex_cli", "hi", bridge, endpoint, False, session_id="tid", resume=True)
    assert again == [
        "codex", "exec", "resume", "--json", "--skip-git-repo-check",
        "-c", 'sandbox_mode="read-only"',
        "-c", 'mcp_servers.mo2.command="python3"', "-c", f"mcp_servers.mo2.args={args}", "tid", "hi",
    ]
    assert "--sandbox" not in again


def test_agy_json_and_conversation_args(tmp_path: Path):
    bridge, endpoint = _paths(tmp_path)
    first = build_cli_command("agy_cli", "hi", bridge, endpoint, False)
    assert first == ["agy", "-p", "hi", "--output-format", "json"]
    again = build_cli_command("agy_cli", "hi", bridge, endpoint, False, session_id="cid", resume=True)
    assert again == ["agy", "-p", "hi", "--output-format", "json", "--conversation", "cid"]


def test_parse_codex_jsonl_fixture():
    thread_id, text = parse_codex_jsonl(FIXTURE.read_text(encoding="utf-8"))
    assert thread_id == "01a1ea54-c8c2-7413-8afd-e59bb6882b65"
    assert text.strip().lower() == "ok"


def _py_builder(script_for_call, calls: list):
    """command_builder that records its kwargs and runs a python snippet instead of the CLI."""
    def builder(provider, prompt, bridge, endpoint, force_host_exec=None, **kw):
        calls.append({"provider": provider, "prompt": prompt, **kw})
        return [sys.executable, "-c", script_for_call(len(calls))]
    return builder


def test_run_cli_turn_returns_session_id(tmp_path: Path):
    bridge, endpoint = _paths(tmp_path)
    calls: list = []
    script = f"import sys; sys.stdout.write({FIXTURE.read_text(encoding='utf-8')!r})"
    res = run_cli_turn(
        "codex_cli", "hi", "CTX", bridge, endpoint, cwd=tmp_path,
        command_builder=_py_builder(lambda n: script, calls),
    )
    assert isinstance(res, CliTurnResult)
    assert res.session_id == "01a1ea54-c8c2-7413-8afd-e59bb6882b65"
    assert res.text.strip().lower() == "ok"
    assert res.note is None
    assert "CTX" in calls[0]["prompt"]  # codex keeps the context in the prompt

    calls.clear()
    res = run_cli_turn(
        "claude_cli", "hi", "CTX", bridge, endpoint, cwd=tmp_path,
        command_builder=_py_builder(lambda n: "print('hello')", calls),
        session_id=UUID, resume=False,
    )
    assert res.session_id == UUID and res.text == "hello"
    assert calls[0]["session_id"] == UUID and calls[0]["resume"] is False
    assert calls[0]["system_prompt"] == "CTX"
    assert "CTX" not in calls[0]["prompt"]


def test_run_cli_turn_agy_json(tmp_path: Path):
    bridge, endpoint = _paths(tmp_path)
    payload = json.dumps({"conversation_id": "conv-1", "status": "SUCCESS", "response": "ok\n"})
    res = run_cli_turn(
        "agy_cli", "hi", "CTX", bridge, endpoint, cwd=tmp_path,
        command_builder=_py_builder(lambda n: f"print({payload!r})", []),
    )
    assert (res.text, res.session_id) == ("ok", "conv-1")


def test_agy_history_is_truncated_keeping_most_recent(tmp_path: Path):
    bridge, endpoint = _paths(tmp_path)
    calls: list = []
    history = [("user", "OLD" + "x" * 7000), ("assistant", "MID" + "y" * 7000), ("user", "NEWEST")]
    run_cli_turn(
        "agy_cli", "hi", "CTX", bridge, endpoint, cwd=tmp_path,
        command_builder=_py_builder(lambda n: "print('{}')", calls), history=history,
    )
    prompt = calls[0]["prompt"]
    assert "Conversation so far:" in prompt and "NEWEST" in prompt
    assert "OLD" not in prompt
    assert len(prompt) < 8000 + 1500


@pytest.mark.parametrize(
    "provider,script0,note_name",
    [
        ("claude_cli", f"print({LOST_SESSION_MARKERS[0]!r} + ': x')", "Claude"),
        ("codex_cli", f"import sys; sys.stderr.write({LOST_SESSION_MARKERS[1]!r}); sys.exit(1)", "Codex"),
    ],
)
def test_lost_session_restarts_with_note(tmp_path: Path, provider, script0, note_name):
    bridge, endpoint = _paths(tmp_path)
    calls: list = []
    fixture = FIXTURE.read_text(encoding="utf-8")
    second = "print('hi')" if provider == "claude_cli" else f"import sys; sys.stdout.write({fixture!r})"
    res = run_cli_turn(
        provider, "hi", "CTX", bridge, endpoint, cwd=tmp_path,
        command_builder=_py_builder(lambda n: script0 if n == 1 else second, calls),
        session_id=UUID if provider == "claude_cli" else "old-thread", resume=True,
    )
    assert len(calls) == 2
    assert calls[0]["resume"] is True and calls[1]["resume"] is False
    assert res.note == f"The {note_name} memory of this conversation was lost; continuing in a new session."
    if provider == "claude_cli":
        assert res.text == "hi"
        assert res.session_id not in (None, UUID) and calls[1]["session_id"] == res.session_id
    else:
        assert res.session_id == "01a1ea54-c8c2-7413-8afd-e59bb6882b65"


def test_cancel_still_raises_turn_cancelled(tmp_path: Path):
    bridge, endpoint = _paths(tmp_path)
    ev = threading.Event()
    ev.set()
    with pytest.raises(TurnCancelled):
        run_cli_turn(
            "claude_cli", "hi", "CTX", bridge, endpoint, cwd=tmp_path, cancel_event=ev,
            command_builder=_py_builder(lambda n: "import time; time.sleep(30)", []),
            session_id=UUID,
        )


def _run_resume(tmp_path, provider, scripts, sid=UUID):
    bridge, endpoint = _paths(tmp_path)
    calls: list = []
    res = run_cli_turn(
        provider, "hi", "CTX", bridge, endpoint, cwd=tmp_path,
        command_builder=_py_builder(lambda n: scripts[min(n, len(scripts)) - 1], calls),
        session_id=sid, resume=True,
    )
    return res, calls


def test_successful_resume_runs_once(tmp_path: Path):
    res, calls = _run_resume(tmp_path, "claude_cli", ["print('fine')"])
    assert len(calls) == 1 and res.note is None and res.text == "fine"


def test_retry_failure_raises_without_third_attempt(tmp_path: Path):
    calls: list = []
    bridge, endpoint = _paths(tmp_path)
    scripts = [f"print({LOST_SESSION_MARKERS[0]!r})", "import sys; sys.stderr.write('boom'); sys.exit(1)"]
    with pytest.raises(RuntimeError, match="boom"):
        run_cli_turn(
            "claude_cli", "hi", "CTX", bridge, endpoint, cwd=tmp_path,
            command_builder=_py_builder(lambda n: scripts[min(n, 2) - 1], calls),
            session_id=UUID, resume=True,
        )
    assert len(calls) == 2


def test_agy_lost_session_retries_once_with_note(tmp_path: Path):
    payload = json.dumps({"conversation_id": "new", "response": "hi"})
    scripts = ["import sys; sys.exit(1)", f"print({payload!r})"]
    res, calls = _run_resume(tmp_path, "agy_cli", scripts, sid="old")
    assert len(calls) == 2
    assert res.note == "The Antigravity memory of this conversation was lost; continuing in a new session."
    assert (res.text, res.session_id) == ("hi", "new")


def test_quoted_marker_in_claude_answer_does_not_retry(tmp_path: Path):
    res, calls = _run_resume(
        tmp_path, "claude_cli", [f"print('The CLI says: ' + {LOST_SESSION_MARKERS[0]!r})"]
    )
    assert len(calls) == 1 and res.note is None


def test_quoted_marker_in_codex_message_does_not_retry(tmp_path: Path):
    events = [
        {"type": "thread.started", "thread_id": "t1"},
        {"type": "item.completed", "item": {"type": "agent_message", "text": LOST_SESSION_MARKERS[1]}},
    ]
    out = "\n".join(json.dumps(e) for e in events)
    res, calls = _run_resume(tmp_path, "codex_cli", [f"print({out!r})"], sid="t1")
    assert len(calls) == 1 and res.note is None and LOST_SESSION_MARKERS[1] in res.text


@pytest.mark.parametrize("provider", ["claude_cli", "codex_cli"])
def test_non_marker_resume_failure_raises_without_retry(tmp_path: Path, provider):
    bridge, endpoint = _paths(tmp_path)
    calls: list = []
    with pytest.raises(RuntimeError, match="rate limit"):
        run_cli_turn(
            provider, "hi", "CTX", bridge, endpoint, cwd=tmp_path,
            command_builder=_py_builder(
                lambda n: "import sys; sys.stderr.write('rate limit'); sys.exit(1)", calls),
            session_id="s", resume=True,
        )
    assert len(calls) == 1
