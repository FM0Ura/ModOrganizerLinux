from __future__ import annotations

import os
import threading
import uuid
from pathlib import Path
from typing import Callable, Any
from PyQt6.QtCore import QObject, QThread, pyqtSignal, pyqtSlot
from PyQt6.QtWidgets import QApplication
from .registry import ToolRegistry


def default_vfs_mounted_check(organizer) -> bool:
    try:
        mounts = Path("/proc/mounts").read_text(encoding="utf-8", errors="replace")
        game_dir = organizer.managedGame().gameDirectory().absolutePath()
        real_game = os.path.realpath(game_dir)
        for line in mounts.splitlines():
            parts = line.split()
            if len(parts) >= 3 and ("mo2-vfs" in parts[0] or "fuse" in parts[2]):
                mount_point = parts[1].replace("\\040", " ")
                if mount_point.startswith(real_game) or mount_point.startswith(game_dir):
                    return True
    except Exception:
        pass
    return False


class MainThreadDispatcher(QObject):
    approval_requested = pyqtSignal(str, str, dict, bool)
    _invoke_signal = pyqtSignal(str)

    def __init__(
        self,
        organizer,
        registry: ToolRegistry,
        permission_mode: str = "safe",
        vfs_mounted_check: Callable[[], bool] | None = None,
    ) -> None:
        super().__init__()
        self._organizer = organizer
        self._registry = registry
        self._permission_mode = permission_mode
        self._session_approved_all = False
        self._vfs_check = vfs_mounted_check or (lambda: default_vfs_mounted_check(self._organizer))
        self._lock = threading.Lock()
        self._pending_calls: dict[str, dict[str, Any]] = {}
        self._pending_approvals: dict[str, dict[str, Any]] = {}
        self._invoke_signal.connect(self._on_main_thread_invoke)

    def set_permission_mode(self, mode: str) -> None:
        self._permission_mode = mode
        if mode == "safe":
            self._session_approved_all = False

    def set_vfs_check(self, fn: Callable[[], bool]) -> None:
        self._vfs_check = fn

    def reset_session_approvals(self) -> None:
        self._session_approved_all = False

    def execute(self, tool_name: str, args: dict[str, Any]) -> dict[str, Any]:
        spec = self._registry.get(tool_name)
        if spec is None:
            return {"ok": False, "error": f"Unknown tool: {tool_name}"}

        try:
            validated_args = (
                spec.args_schema(**(args or {})).model_dump()
                if spec.args_schema is not None
                else (args or {})
            )
        except Exception as exc:
            return {"ok": False, "error": f"Invalid arguments for {tool_name}: {exc}"}

        if spec.is_mutating and self._vfs_check():
            return {
                "ok": False,
                "error": "Blocked: Virtual filesystem is currently mounted (game or tool is running).",
            }

        if spec.is_mutating and self._permission_mode == "safe" and not self._session_approved_all:
            decision = self._wait_for_user_approval(spec.name, validated_args, spec.destructive)
            if decision == "reject":
                return {"ok": False, "error": f"Action '{tool_name}' rejected by user."}
            if decision == "approve_session":
                self._session_approved_all = True

        app = QApplication.instance()
        if app is None or QThread.currentThread() == self.thread():
            return self._run_spec_directly(spec, validated_args)

        call_id = str(uuid.uuid4())
        done_event = threading.Event()
        with self._lock:
            self._pending_calls[call_id] = {
                "spec": spec,
                "args": validated_args,
                "event": done_event,
                "result": None,
            }
        self._invoke_signal.emit(call_id)
        done_event.wait(timeout=30.0)
        with self._lock:
            entry = self._pending_calls.pop(call_id, None)
        if entry is None or entry["result"] is None:
            return {"ok": False, "error": f"Timed out executing tool '{tool_name}' on main thread."}
        return entry["result"]

    def _wait_for_user_approval(self, tool_name: str, args: dict[str, Any], destructive: bool) -> str:
        req_id = str(uuid.uuid4())
        event = threading.Event()
        with self._lock:
            self._pending_approvals[req_id] = {"event": event, "decision": "reject"}
        self.approval_requested.emit(req_id, tool_name, args, destructive)
        app = QApplication.instance()
        if app is not None and QThread.currentThread() == self.thread():
            while not event.is_set():
                app.processEvents()
        else:
            event.wait(timeout=300.0)
        with self._lock:
            info = self._pending_approvals.pop(req_id, {"decision": "reject"})
        return info["decision"]

    @pyqtSlot(str, str)
    def resolve_approval(self, request_id: str, decision: str) -> None:
        with self._lock:
            info = self._pending_approvals.get(request_id)
            if info is not None:
                info["decision"] = decision
                info["event"].set()

    @pyqtSlot(str)
    def _on_main_thread_invoke(self, call_id: str) -> None:
        with self._lock:
            entry = self._pending_calls.get(call_id)
        if entry is None:
            return
        entry["result"] = self._run_spec_directly(entry["spec"], entry["args"])
        entry["event"].set()

    def _run_spec_directly(self, spec, validated_args: dict[str, Any]) -> dict[str, Any]:
        try:
            data = spec.func(self._organizer, **validated_args)
            return {"ok": True, "result": data}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}
