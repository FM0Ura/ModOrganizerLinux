#!/usr/bin/env python3
"""Stdio-to-HTTP JSON-RPC bridge for CLI MCP clients.

Standard library only: it runs with the host's python3, not with Mod Organizer's.
The endpoint (URL and token) comes from the private file given with --config, so the
token never shows on a command line; MO2_MCP_URL / MO2_MCP_TOKEN are the fallback.
"""

from __future__ import annotations

import json
import os
import sys
import urllib.request


def _endpoint(argv: list[str]) -> tuple[str, str]:
    if "--config" in argv:
        path = argv[argv.index("--config") + 1]
        with open(path, encoding="utf-8") as f:
            cfg = json.load(f)
        return cfg["url"], cfg["token"]
    return os.environ["MO2_MCP_URL"], os.environ["MO2_MCP_TOKEN"]


def main() -> None:
    url, token = _endpoint(sys.argv[1:])
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        req = urllib.request.Request(
            url,
            data=line.encode("utf-8"),
            headers={"Content-Type": "application/json", "X-MO2-Token": token},
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=310) as resp:
                out = resp.read().decode("utf-8").strip()
            if out:  # notifications are acknowledged with an empty body
                sys.stdout.write(out + "\n")
                sys.stdout.flush()
        except Exception as exc:
            sys.stderr.write(f"mo2-mcp-bridge error: {exc}\n")
            sys.stderr.flush()
            # a request (not a notification) must still get an answer, or the client waits
            try:
                rpc_id = json.loads(line).get("id")
            except Exception:
                rpc_id = None
            if rpc_id is not None:
                err = {"jsonrpc": "2.0", "id": rpc_id,
                       "error": {"code": -32000, "message": f"Mod Organizer is not reachable: {exc}"}}
                sys.stdout.write(json.dumps(err) + "\n")
                sys.stdout.flush()


if __name__ == "__main__":
    main()
