#!/usr/bin/env python3
"""Stdio-to-HTTP JSON-RPC bridge for CLI MCP clients."""

from __future__ import annotations

import os
import sys
import urllib.request


def main() -> None:
    url = os.environ["MO2_MCP_URL"]
    token = os.environ["MO2_MCP_TOKEN"]
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
                out = resp.read().decode("utf-8")
                sys.stdout.write(out + "\n")
                sys.stdout.flush()
        except Exception as exc:
            sys.stderr.write(f"mo2-mcp-bridge error: {exc}\n")
            sys.stderr.flush()


if __name__ == "__main__":
    main()
