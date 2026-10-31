from __future__ import annotations

import re
from pathlib import Path
from typing import Any
from pydantic import BaseModel, Field
from .registry import ToolRegistry
from .mod_tools import _ensure_contained


class ListLoadOrderArgs(BaseModel):
    active_only: bool = Field(default=False, description="If true, list only active plugins")


class SetPluginsActiveArgs(BaseModel):
    plugin_names: list[str] = Field(..., min_length=1, description="Plugin filenames (.esm/.esp/.esl)")
    active: bool = Field(..., description="True to enable, False to disable")


class SetProfileIniArgs(BaseModel):
    ini_filename: str = Field(..., description="INI filename inside current profile directory (e.g. SkyrimPrefs.ini)")
    section: str = Field(..., min_length=1, description="INI section name without brackets")
    key: str = Field(..., min_length=1, description="Setting key name")
    value: str = Field(..., description="New value to set")


def _update_ini_text(content: str, section: str, key: str, value: str) -> str:
    lines = content.splitlines(keepends=True)
    sec_pattern = re.compile(rf"^\s*\[{re.escape(section)}\]\s*$", re.IGNORECASE)
    key_pattern = re.compile(rf"^(\s*{re.escape(key)}\s*=).*$", re.IGNORECASE)

    in_section = False
    sec_found = False
    key_updated = False
    out: list[str] = []

    for line in lines:
        if line.lstrip().startswith("[") and "]" in line:
            if in_section and not key_updated:
                out.append(f"{key}={value}\n")
                key_updated = True
            in_section = bool(sec_pattern.match(line.strip()))
            if in_section:
                sec_found = True
            out.append(line)
            continue
        if in_section and not key_updated and key_pattern.match(line):
            out.append(f"{key}={value}\n")
            key_updated = True
        else:
            out.append(line)

    if in_section and not key_updated:
        if out and not out[-1].endswith("\n"):
            out[-1] += "\n"
        out.append(f"{key}={value}\n")
    elif not sec_found:
        if out and not out[-1].endswith("\n"):
            out[-1] += "\n"
        out.append(f"[{section}]\n{key}={value}\n")

    return "".join(out)


def register_plugin_tools(reg: ToolRegistry) -> None:
    @reg.register(
        name="list_load_order",
        description="List game plugins (.esm, .esp, .esl) in load order.",
        args_schema=ListLoadOrderArgs,
        is_mutating=False,
    )
    def list_load_order(organizer, active_only: bool = False) -> dict[str, Any]:
        plist = organizer.pluginList()
        names = plist.pluginNames()
        entries = []
        for n in names:
            active = plist.state(n) == 2
            if active_only and not active:
                continue
            entries.append(
                {
                    "name": n,
                    "active": active,
                    "load_order": plist.loadOrder(n),
                    "is_master": plist.isMasterFlagged(n),
                    "is_light": plist.isLightFlagged(n),
                    "origin": plist.origin(n),
                }
            )
        entries.sort(key=lambda x: (x["load_order"] < 0, x["load_order"], x["name"]))
        return {"plugins": entries}

    @reg.register(
        name="set_plugins_active",
        description="Enable or disable one or more game plugins (.esp/.esm/.esl) in the load order.",
        args_schema=SetPluginsActiveArgs,
        is_mutating=True,
    )
    def set_plugins_active(organizer, plugin_names: list[str], active: bool) -> dict[str, Any]:
        plist = organizer.pluginList()
        existing = set(plist.pluginNames())
        target_state = 2 if active else 1
        updated = []
        missing = []
        for n in plugin_names:
            if n in existing:
                plist.setState(n, target_state)
                updated.append(n)
            else:
                missing.append(n)
        return {"updated": updated, "active": active, "missing": missing}

    @reg.register(
        name="set_profile_ini_setting",
        description="Set a key=value inside a profile INI configuration file (such as Skyrim.ini or SkyrimPrefs.ini).",
        args_schema=SetProfileIniArgs,
        is_mutating=True,
    )
    def set_profile_ini_setting(organizer, ini_filename: str, section: str, key: str, value: str) -> dict[str, Any]:
        if "/" in ini_filename or "\\" in ini_filename or ".." in ini_filename or not ini_filename.lower().endswith(".ini"):
            raise ValueError("Invalid INI filename: must be a direct .ini file inside the profile folder.")
        prof_dir = Path(organizer.profilePath())
        target = _ensure_contained(prof_dir, prof_dir / ini_filename)
        if not target.is_file():
            raise FileNotFoundError(f"Profile INI '{ini_filename}' does not exist.")
        old_text = target.read_text(encoding="utf-8", errors="replace")
        new_text = _update_ini_text(old_text, section, key, value)
        target.write_text(new_text, encoding="utf-8")
        return {"ini_file": ini_filename, "section": section, "key": key, "value": value}
