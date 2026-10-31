from __future__ import annotations

import os
from pathlib import Path
from typing import Any
from pydantic import BaseModel, Field
from .registry import ToolRegistry


class EmptyArgs(BaseModel):
    pass


class FindFileOwnerArgs(BaseModel):
    virtual_subpath: str = Field(..., min_length=1, description="Relative path inside Data (e.g. 'SKSE/Plugins/po3_Tweaks.dll')")


class ReadDiagnosticLogsArgs(BaseModel):
    log_kind: str = Field(default="auto", description="One of: 'auto', 'crash', 'mo2', 'skse', 'vfs'")
    tail_lines: int = Field(default=150, ge=10, le=500, description="Number of trailing lines to read")


def register_diagnostic_tools(reg: ToolRegistry) -> None:
    @reg.register(
        name="get_environment_info",
        description="Get active game, profile, instance paths, and mod/plugin counts.",
        args_schema=EmptyArgs,
        is_mutating=False,
    )
    def get_environment_info(organizer) -> dict[str, Any]:
        game = organizer.managedGame()
        mod_list = organizer.modList()
        all_mods = mod_list.allModsByProfilePriority()
        active_mods = [m for m in all_mods if (mod_list.state(m) & 0x02)]
        plist = organizer.pluginList()
        all_plugins = plist.pluginNames()
        active_plugins = [p for p in all_plugins if plist.state(p) == 2]
        return {
            "game_name": game.gameName() if game else "",
            "game_short_name": game.gameShortName() if game else "",
            "game_directory": game.gameDirectory().absolutePath() if game else "",
            "profile_name": organizer.profileName(),
            "instance_path": organizer.basePath(),
            "mods_path": organizer.modsPath(),
            "downloads_path": organizer.downloadsPath(),
            "total_mods": len(all_mods),
            "active_mods": len(active_mods),
            "total_plugins": len(all_plugins),
            "active_plugins": len(active_plugins),
        }

    @reg.register(
        name="find_file_owner",
        description="Find which mod provides a specific virtual path inside Data (useful for DLLs or assets in crash logs).",
        args_schema=FindFileOwnerArgs,
        is_mutating=False,
    )
    def find_file_owner(organizer, virtual_subpath: str) -> dict[str, Any]:
        clean = virtual_subpath.lstrip("/\\").replace("\\", "/")
        resolved = organizer.resolvePath(clean)
        if not resolved:
            return {"found": False, "virtual_subpath": clean, "winning_mod": None, "resolved_path": None}
        res_path = Path(os.path.realpath(resolved))
        mods_root = Path(os.path.realpath(organizer.modsPath()))
        overwrite_root = Path(os.path.realpath(organizer.overwritePath()))
        winning_mod = "Base Game / External"
        if mods_root in res_path.parents:
            rel = res_path.relative_to(mods_root)
            winning_mod = rel.parts[0]
        elif overwrite_root in res_path.parents:
            winning_mod = "Overwrite"
        return {
            "found": True,
            "virtual_subpath": clean,
            "winning_mod": winning_mod,
            "resolved_path": str(res_path),
        }

    @reg.register(
        name="read_diagnostic_logs",
        description="Read recent lines from MO2 logs or game crash logs.",
        args_schema=ReadDiagnosticLogsArgs,
        is_mutating=False,
    )
    def read_diagnostic_logs(organizer, log_kind: str = "auto", tail_lines: int = 150) -> dict[str, Any]:
        logs_dir = Path(organizer.basePath()) / "logs"
        candidates: list[Path] = []
        if logs_dir.is_dir():
            for p in logs_dir.glob("*.log"):
                if p.is_file() and p.stat().st_size > 0:
                    candidates.append(p)
        game = organizer.managedGame()
        if game and hasattr(game, "documentsDirectory"):
            try:
                docs_dir = Path(game.documentsDirectory().absolutePath())
                skse_dir = docs_dir / "SKSE"
                if skse_dir.is_dir():
                    for p in skse_dir.glob("*.log"):
                        if p.is_file() and p.stat().st_size > 0:
                            candidates.append(p)
            except Exception:
                pass

        if not candidates:
            return {"found": False, "log_file": None, "content": "No non-empty log files found."}

        kind = log_kind.lower()
        filtered = candidates
        if kind == "crash":
            filtered = [p for p in candidates if "crash" in p.name.lower()] or candidates
        elif kind == "mo2":
            filtered = [p for p in candidates if "mo_" in p.name.lower() or "mo2" in p.name.lower()] or candidates
        elif kind == "skse":
            filtered = [p for p in candidates if "skse" in p.name.lower()] or candidates

        filtered.sort(key=lambda p: p.stat().st_mtime, reverse=True)
        chosen = filtered[0]
        lines = chosen.read_text(encoding="utf-8", errors="replace").splitlines()
        snippet = "\n".join(lines[-min(tail_lines, 500):])
        return {
            "found": True,
            "log_file": str(chosen),
            "total_lines": len(lines),
            "content": snippet,
        }
