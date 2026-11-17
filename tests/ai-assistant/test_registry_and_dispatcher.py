from __future__ import annotations

import threading
from pydantic import BaseModel, Field
from ai_assistant.tools.registry import ToolRegistry
from ai_assistant.tools.dispatcher import MainThreadDispatcher


class DummyReadArgs(BaseModel):
    query: str = Field(default="", description="Filter")


class DummyWriteArgs(BaseModel):
    mod_name: str = Field(..., description="Mod to toggle")


def _make_registry(fake_organizer) -> ToolRegistry:
    reg = ToolRegistry()

    @reg.register(
        name="dummy_read",
        description="Read mods",
        args_schema=DummyReadArgs,
        is_mutating=False,
    )
    def dummy_read(org, query: str = "") -> dict:
        return {"mods": org.modList().allModsByProfilePriority(), "query": query}

    @reg.register(
        name="dummy_write",
        description="Disable mod",
        args_schema=DummyWriteArgs,
        is_mutating=True,
    )
    def dummy_write(org, mod_name: str) -> dict:
        org.modList().setActive([mod_name], False)
        return {"disabled": mod_name}

    return reg


def test_registry_exports_mcp_and_langchain_tools(fake_organizer):
    reg = _make_registry(fake_organizer)
    disp = MainThreadDispatcher(fake_organizer, reg, permission_mode="autonomous")
    mcp_list = reg.to_mcp_tools()
    assert len(mcp_list) == 2
    assert {t["name"] for t in mcp_list} == {"dummy_read", "dummy_write"}
    safe_lc, write_lc = reg.to_langchain_tools(disp)
    assert [t.name for t in safe_lc] == ["dummy_read"]
    assert [t.name for t in write_lc] == ["dummy_write"]


def test_write_tool_blocked_when_vfs_mounted(fake_organizer):
    reg = _make_registry(fake_organizer)
    disp = MainThreadDispatcher(
        fake_organizer,
        reg,
        permission_mode="autonomous",
        vfs_mounted_check=lambda: True,
    )
    res = disp.execute("dummy_write", {"mod_name": "USSEP"})
    assert res["ok"] is False
    assert "Virtual filesystem is currently mounted" in res["error"]
    assert fake_organizer.modList().state("USSEP") == 0x02


def test_safe_mode_rejection_does_not_mutate(fake_organizer):
    reg = _make_registry(fake_organizer)
    disp = MainThreadDispatcher(
        fake_organizer,
        reg,
        permission_mode="safe",
        vfs_mounted_check=lambda: False,
    )

    def auto_reject(req_id: str, tool_name: str, args: dict, destructive: bool):
        disp.resolve_approval(req_id, "reject")

    disp.approval_requested.connect(auto_reject)
    result_box = {}

    def worker_call():
        result_box["res"] = disp.execute("dummy_write", {"mod_name": "USSEP"})

    t = threading.Thread(target=worker_call)
    t.start()
    from PyQt6.QtWidgets import QApplication
    while t.is_alive():
        QApplication.processEvents()
    t.join(timeout=2)

    assert result_box["res"]["ok"] is False
    assert "rejected by user" in result_box["res"]["error"].lower()
    assert fake_organizer.modList().state("USSEP") == 0x02


def test_safe_mode_approve_session_allows_subsequent_writes(fake_organizer):
    reg = _make_registry(fake_organizer)
    disp = MainThreadDispatcher(
        fake_organizer,
        reg,
        permission_mode="safe",
        vfs_mounted_check=lambda: False,
    )
    prompts = []

    def approve_all_once(req_id: str, tool_name: str, args: dict, destructive: bool):
        prompts.append(tool_name)
        disp.resolve_approval(req_id, "approve_session")

    disp.approval_requested.connect(approve_all_once)
    out = []

    def worker_call():
        out.append(disp.execute("dummy_write", {"mod_name": "USSEP"}))
        out.append(disp.execute("dummy_write", {"mod_name": "SkyUI"}))

    t = threading.Thread(target=worker_call)
    t.start()
    from PyQt6.QtWidgets import QApplication
    while t.is_alive():
        QApplication.processEvents()
    t.join(timeout=2)

    assert len(prompts) == 1
    assert out[0]["ok"] is True
    assert out[1]["ok"] is True
    assert fake_organizer.modList().state("USSEP") == 0x01
    assert fake_organizer.modList().state("SkyUI") == 0x01


def _run_in_thread(disp, calls):
    from PyQt6.QtWidgets import QApplication

    out = []

    def worker_call():
        for name, args in calls:
            out.append(disp.execute(name, args))

    t = threading.Thread(target=worker_call)
    t.start()
    while t.is_alive():
        QApplication.processEvents()
    t.join(timeout=2)
    QApplication.processEvents()
    return out


def _collect(disp, approval=None):
    events = []
    disp.write_executed.connect(lambda n, a, d: events.append((n, a, d)))
    if approval is not None:
        disp.approval_requested.connect(
            lambda rid, n, a, dest: disp.resolve_approval(rid, approval)
        )
    return events


def _disp(fake_organizer, mode, vfs=False):
    return MainThreadDispatcher(
        fake_organizer,
        _make_registry(fake_organizer),
        permission_mode=mode,
        vfs_mounted_check=lambda: vfs,
    )


def test_write_executed_decisions(fake_organizer):
    # Autonomous -> auto (cross-thread path)
    disp = _disp(fake_organizer, "autonomous")
    events = _collect(disp)
    _run_in_thread(disp, [("dummy_write", {"mod_name": "USSEP"})])
    assert events == [("dummy_write", {"mod_name": "USSEP"}, "auto")]

    # Safe + approve -> approved
    disp = _disp(fake_organizer, "safe")
    events = _collect(disp, "approve")
    _run_in_thread(disp, [("dummy_write", {"mod_name": "USSEP"})])
    assert events == [("dummy_write", {"mod_name": "USSEP"}, "approved")]

    # Safe + approve_session -> approved_session, then auto
    disp = _disp(fake_organizer, "safe")
    events = _collect(disp, "approve_session")
    _run_in_thread(
        disp,
        [("dummy_write", {"mod_name": "USSEP"}), ("dummy_write", {"mod_name": "SkyUI"})],
    )
    assert [e[2] for e in events] == ["approved_session", "auto"]
    assert events[1][1] == {"mod_name": "SkyUI"}

    # reject -> nothing
    disp = _disp(fake_organizer, "safe")
    events = _collect(disp, "reject")
    _run_in_thread(disp, [("dummy_write", {"mod_name": "USSEP"})])
    assert events == []

    # VFS mounted -> nothing
    disp = _disp(fake_organizer, "autonomous", vfs=True)
    events = _collect(disp)
    _run_in_thread(disp, [("dummy_write", {"mod_name": "USSEP"})])
    assert events == []

    # read tool -> nothing
    disp = _disp(fake_organizer, "autonomous")
    events = _collect(disp)
    _run_in_thread(disp, [("dummy_read", {})])
    assert events == []


def test_write_executed_direct_main_thread_path(fake_organizer):
    disp = _disp(fake_organizer, "autonomous")
    events = _collect(disp)
    res = disp.execute("dummy_write", {"mod_name": "USSEP"})
    assert res["ok"] is True
    assert events == [("dummy_write", {"mod_name": "USSEP"}, "auto")]


def test_write_executed_not_emitted_when_tool_fails(fake_organizer):
    reg = ToolRegistry()

    @reg.register(
        name="boom", description="x", args_schema=DummyWriteArgs, is_mutating=True
    )
    def boom(org, mod_name: str) -> dict:
        raise RuntimeError("nope")

    disp = MainThreadDispatcher(
        fake_organizer, reg, permission_mode="autonomous", vfs_mounted_check=lambda: False
    )
    events = _collect(disp)
    assert disp.execute("boom", {"mod_name": "a"})["ok"] is False
    _run_in_thread(disp, [("boom", {"mod_name": "a"})])
    assert events == []
