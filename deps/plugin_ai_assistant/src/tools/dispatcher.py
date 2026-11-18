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


CLOSED_RESULT = {"ok": False, "error": "The AI Assistant was closed."}


class MainThreadDispatcher(QObject):
    approval_requested = pyqtSignal(str, str, dict, bool)
    # (tool_name, args, decision): decision is approved | approved_session | auto.
    write_executed = pyqtSignal(str, dict, str)
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
        # set when MO2 closes or restarts: a turn that outlives the run must not reach the
        # organizer of the previous run
        self._closed = False
        self._invoke_signal.connect(self._on_main_thread_invoke)

    def set_permission_mode(self, mode: str) -> None:
        self._permission_mode = mode
        if mode == "safe":
            self._session_approved_all = False

    def set_vfs_check(self, fn: Callable[[], bool]) -> None:
        self._vfs_check = fn

    def reset_session_approvals(self) -> None:
        with self._lock:
            self._session_approved_all = False

    def close(self) -> None:
        """Refuses every later tool call and releases the ones waiting, without running them."""
        with self._lock:
            self._closed = True
            for entry in self._pending_calls.values():
                entry["result"] = dict(CLOSED_RESULT)
                entry["event"].set()
            for info in self._pending_approvals.values():
                info["decision"] = "reject"
                info["event"].set()

    def reopen(self) -> None:
        with self._lock:
            self._closed = False

    @property
    def closed(self) -> bool:
        return self._closed

    def execute(self, tool_name: str, args: dict[str, Any]) -> dict[str, Any]:
        if self._closed:
            return dict(CLOSED_RESULT)
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

        write_decision = "auto"
        if spec.is_mutating and self._permission_mode == "safe" and not self._session_approved_all:
            decision = self._wait_for_user_approval(spec.name, validated_args, spec.destructive)
            if decision not in ("approve", "approve_session"):
                # fail closed: anything but an explicit approval is a rejection
                return {"ok": False, "error": f"Action '{tool_name}' rejected by user."}
            if decision == "approve_session":
                # the flag itself is set by resolve_approval, on the main thread
                write_decision = "approved_session"
            else:
                write_decision = "approved"

        if self._closed:  # closed while waiting for the approval
            return dict(CLOSED_RESULT)
        app = QApplication.instance()
        if app is None or QThread.currentThread() == self.thread():
            result = self._run_spec_directly(spec, validated_args)
            self._emit_if_write_ran(spec, validated_args, result, write_decision)
            return result

        call_id = str(uuid.uuid4())
        done_event = threading.Event()
        with self._lock:
            if self._closed:
                return dict(CLOSED_RESULT)
            self._pending_calls[call_id] = {
                "spec": spec,
                "args": validated_args,
                "event": done_event,
                "result": None,
                "decision": write_decision,
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
                if decision == "approve_session":
                    # set here, not on the worker thread: a reset that follows must win
                    self._session_approved_all = True
                info["event"].set()

    @pyqtSlot(str)
    def _on_main_thread_invoke(self, call_id: str) -> None:
        with self._lock:
            entry = self._pending_calls.get(call_id)
            if entry is None or self._closed or entry["result"] is not None:
                return  # an invocation queued before close() is not run
        entry["result"] = self._run_spec_directly(entry["spec"], entry["args"])
        # Emitted here, on the dispatcher's (main) thread, before waking the worker.
        self._emit_if_write_ran(entry["spec"], entry["args"], entry["result"], entry["decision"])
        entry["event"].set()

    def _emit_if_write_ran(self, spec, args: dict[str, Any], result: dict[str, Any], decision: str) -> None:
        if spec.is_mutating and result.get("ok") is True:
            self.write_executed.emit(spec.name, dict(args), decision)

    def _run_spec_directly(self, spec, validated_args: dict[str, Any]) -> dict[str, Any]:
        try:
            data = spec.func(self._organizer, **validated_args)
            return {"ok": True, "result": data}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}
