from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import secrets
import threading
from typing import Any
from ..tools.dispatcher import MainThreadDispatcher


class LocalMcpServer:
    def __init__(self, dispatcher: MainThreadDispatcher) -> None:
        self._dispatcher = dispatcher
        self.token = secrets.token_hex(24)
        self._httpd: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None
        self.port: int = 0

    def endpoint_url(self) -> str:
        return f"http://127.0.0.1:{self.port}/mcp"

    def start(self) -> tuple[int, str]:
        if self._httpd is not None:
            return self.port, self.token

        outer = self

        class _Handler(BaseHTTPRequestHandler):
            def log_message(self, format: str, *args: Any) -> None:
                return

            def do_POST(self) -> None:
                req_token = self.headers.get("X-MO2-Token", "")
                if not secrets.compare_digest(req_token, outer.token):
                    self.send_response(403)
                    self.end_headers()
                    return
                length = int(self.headers.get("Content-Length", "0"))
                raw = self.rfile.read(length)
                try:
                    msg = json.loads(raw.decode("utf-8"))
                except Exception:
                    self.send_response(400)
                    self.end_headers()
                    return

                reply = outer._handle_rpc(msg)
                if reply is None:
                    # a JSON-RPC notification: acknowledged, never answered
                    self.send_response(202)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                body = json.dumps(reply, ensure_ascii=False).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self._httpd = ThreadingHTTPServer(("127.0.0.1", 0), _Handler)
        self.port = int(self._httpd.server_address[1])
        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)
        self._thread.start()
        return self.port, self.token

    def stop(self) -> None:
        if self._httpd is not None:
            self._httpd.shutdown()
            self._httpd.server_close()
            self._httpd = None

    def _handle_rpc(self, msg: dict[str, Any]) -> dict[str, Any] | None:
        if not isinstance(msg, dict) or "id" not in msg:
            # notifications (e.g. notifications/initialized) must not be answered
            return None
        rpc_id = msg.get("id")
        method = msg.get("method", "")
        params = msg.get("params") or {}

        if method == "ping":
            return {"jsonrpc": "2.0", "id": rpc_id, "result": {}}

        if method == "initialize":
            return {
                "jsonrpc": "2.0",
                "id": rpc_id,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "mo2-ai-assistant", "version": "1.0.0"},
                },
            }
        if method == "tools/list":
            return {
                "jsonrpc": "2.0",
                "id": rpc_id,
                "result": {"tools": self._dispatcher._registry.to_mcp_tools()},
            }
        if method == "tools/call":
            tool_name = params.get("name", "")
            tool_args = params.get("arguments") or {}
            exec_res = self._dispatcher.execute(tool_name, tool_args)
            is_err = not exec_res.get("ok", False)
            text = json.dumps(exec_res, ensure_ascii=False)
            return {
                "jsonrpc": "2.0",
                "id": rpc_id,
                "result": {
                    "content": [{"type": "text", "text": text}],
                    "isError": is_err,
                },
            }
        return {
            "jsonrpc": "2.0",
            "id": rpc_id,
            "error": {"code": -32601, "message": f"Method not found: {method}"},
        }
