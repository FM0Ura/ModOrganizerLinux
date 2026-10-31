from __future__ import annotations

import os
import shutil
from pathlib import Path
from typing import Any
from pydantic import BaseModel, Field
from .registry import ToolRegistry


def _ensure_contained(base_dir: Path, target: Path) -> Path:
    real_base = Path(os.path.realpath(base_dir))
    real_target = Path(os.path.realpath(target))
    if real_target == real_base or real_base not in real_target.parents:
        raise ValueError(f"Path '{target.name}' escapes allowed directory '{base_dir}'.")
    return real_target


class ListModsArgs(BaseModel):
    filter_query: str = Field(default="", description="Case-insensitive substring to filter mod names")
    active_only: bool = Field(default=False, description="If true, return only enabled mods")
    limit: int = Field(default=200, ge=1, le=1000, description="Maximum number of mods to return")


class ModDetailsArgs(BaseModel):
    mod_name: str = Field(..., description="Exact name of the mod")


class ListDownloadsArgs(BaseModel):
    limit: int = Field(default=100, ge=1, le=500, description="Maximum downloads to list")


class SetModsActiveArgs(BaseModel):
    mod_names: list[str] = Field(..., min_length=1, description="List of mod names to enable or disable")
    active: bool = Field(..., description="True to enable, False to disable")


class SetModPriorityArgs(BaseModel):
    mod_name: str = Field(..., description="Exact mod name")
    new_priority: int = Field(..., ge=0, description="New priority index (0 is lowest priority)")


class InstallModArgs(BaseModel):
    archive_filename: str = Field(..., description="File name inside the downloads directory")
    suggested_mod_name: str = Field(default="", description="Optional name for the installed mod")


class CreateEmptyModArgs(BaseModel):
    mod_name: str = Field(..., min_length=1, description="Name for the new empty mod or separator")
    as_separator: bool = Field(default=False, description="If True, creates a visual separator")


class RemoveModArgs(BaseModel):
    mod_name: str = Field(..., min_length=1, description="Exact name of the installed mod to delete")


def register_mod_tools(reg: ToolRegistry) -> None:
    @reg.register(
        name="list_mods",
        description="List installed mods ordered by profile priority, optionally filtered by substring or active state.",
        args_schema=ListModsArgs,
        is_mutating=False,
    )
    def list_mods(organizer, filter_query: str = "", active_only: bool = False, limit: int = 200) -> dict[str, Any]:
        mod_list = organizer.modList()
        names = mod_list.allModsByProfilePriority()
        q = filter_query.strip().lower()
        items = []
        for name in names:
            is_active = bool(mod_list.state(name) & 0x02)
            if active_only and not is_active:
                continue
            if q and q not in name.lower():
                continue
            mod = mod_list.getMod(name)
            items.append(
                {
                    "name": name,
                    "active": is_active,
                    "priority": mod_list.priority(name),
                    "version": str(mod.newestVersion()) if mod else "",
                    "nexus_id": int(mod.nexusId()) if mod else 0,
                    "is_separator": bool(mod.isSeparator()) if mod else name.endswith("_separator"),
                    "is_foreign": bool(mod.isForeign()) if mod else False,
                }
            )
            if len(items) >= limit:
                break
        return {"total_matching": len(items), "mods": items}

    @reg.register(
        name="get_mod_details",
        description="Inspect details and top-level files of a specific mod.",
        args_schema=ModDetailsArgs,
        is_mutating=False,
    )
    def get_mod_details(organizer, mod_name: str) -> dict[str, Any]:
        mod_list = organizer.modList()
        mod = mod_list.getMod(mod_name)
        if mod is None:
            raise ValueError(f"Mod '{mod_name}' not found.")
        mod_path = Path(mod.absolutePath())
        files = []
        if mod_path.is_dir():
            for p in sorted(mod_path.rglob("*")):
                if p.is_file() and p.name != "meta.ini":
                    files.append(str(p.relative_to(mod_path)))
                    if len(files) >= 150:
                        break
        return {
            "name": mod.name(),
            "active": bool(mod_list.state(mod_name) & 0x02),
            "priority": mod_list.priority(mod_name),
            "version": str(mod.newestVersion()),
            "nexus_id": int(mod.nexusId()),
            "path": str(mod_path),
            "sample_files": files,
        }

    @reg.register(
        name="list_downloads",
        description="List archive files in the instance downloads folder available for installation.",
        args_schema=ListDownloadsArgs,
        is_mutating=False,
    )
    def list_downloads(organizer, limit: int = 100) -> dict[str, Any]:
        dl_dir = Path(organizer.downloadsPath())
        archives = []
        if dl_dir.is_dir():
            for p in sorted(dl_dir.iterdir(), key=lambda x: x.stat().st_mtime, reverse=True):
                if p.is_file() and not p.name.endswith(".meta"):
                    archives.append({"filename": p.name, "size_bytes": p.stat().st_size})
                    if len(archives) >= limit:
                        break
        return {"downloads": archives}

    @reg.register(
        name="set_mods_active",
        description="Enable or disable one or more mods in the active profile.",
        args_schema=SetModsActiveArgs,
        is_mutating=True,
    )
    def set_mods_active(organizer, mod_names: list[str], active: bool) -> dict[str, Any]:
        mod_list = organizer.modList()
        valid = []
        skipped = []
        for n in mod_names:
            mod = mod_list.getMod(n)
            if mod is None or mod.isSeparator() or mod.isForeign():
                skipped.append(n)
            else:
                valid.append(n)
        if valid:
            mod_list.setActive(valid, active)
        return {"updated": valid, "active": active, "skipped": skipped}

    @reg.register(
        name="set_mod_priority",
        description="Change the priority order of a mod in the current profile.",
        args_schema=SetModPriorityArgs,
        is_mutating=True,
    )
    def set_mod_priority(organizer, mod_name: str, new_priority: int) -> dict[str, Any]:
        ok = organizer.modList().setPriority(mod_name, new_priority)
        if not ok:
            raise ValueError(f"Could not set priority for mod '{mod_name}'.")
        return {"mod_name": mod_name, "new_priority": new_priority}

    @reg.register(
        name="install_mod_from_downloads",
        description="Install a mod archive located in the instance downloads directory.",
        args_schema=InstallModArgs,
        is_mutating=True,
    )
    def install_mod_from_downloads(organizer, archive_filename: str, suggested_mod_name: str = "") -> dict[str, Any]:
        if "/" in archive_filename or "\\" in archive_filename or ".." in archive_filename:
            raise ValueError("Invalid archive filename: must be a direct filename inside downloads.")
        dl_dir = Path(organizer.downloadsPath())
        target = _ensure_contained(dl_dir, dl_dir / archive_filename)
        if not target.is_file():
            raise FileNotFoundError(f"Archive '{archive_filename}' does not exist in downloads.")
        installed = organizer.installMod(str(target), suggested_mod_name)
        if installed is None:
            raise RuntimeError(f"Installation of '{archive_filename}' failed or was cancelled.")
        return {"installed_mod": installed.name(), "archive": archive_filename}

    @reg.register(
        name="create_empty_mod",
        description="Create a new empty mod folder or separator in the mod list.",
        args_schema=CreateEmptyModArgs,
        is_mutating=True,
    )
    def create_empty_mod(organizer, mod_name: str, as_separator: bool = False) -> dict[str, Any]:
        clean = mod_name.strip().replace("/", "_").replace("\\", "_")
        if not clean or clean in (".", ".."):
            raise ValueError("Invalid mod name.")
        if as_separator and not clean.endswith("_separator"):
            clean = f"{clean}_separator"
        created = organizer.createMod(clean)
        if created is None:
            raise RuntimeError(f"Failed to create mod '{clean}'.")
        organizer.refresh()
        return {"created": created.name(), "is_separator": as_separator}

    @reg.register(
        name="remove_mod",
        description="Delete an installed mod folder from the instance mods directory.",
        args_schema=RemoveModArgs,
        is_mutating=True,
        destructive=True,
    )
    def remove_mod(organizer, mod_name: str) -> dict[str, Any]:
        if "/" in mod_name or "\\" in mod_name or ".." in mod_name:
            raise ValueError("Invalid mod name.")
        mod_list = organizer.modList()
        mod = mod_list.getMod(mod_name)
        if mod is None or mod.isForeign():
            raise ValueError(f"Mod '{mod_name}' not found or cannot be removed.")
        mods_dir = Path(organizer.modsPath())
        target = _ensure_contained(mods_dir, Path(mod.absolutePath()))
        shutil.rmtree(target)
        organizer.refresh()
        return {"removed": mod_name}
