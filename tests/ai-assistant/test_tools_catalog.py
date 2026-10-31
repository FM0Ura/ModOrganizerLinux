from __future__ import annotations

from pathlib import Path
from ai_assistant.tools import build_default_registry, MainThreadDispatcher


def _make_dispatcher(fake_organizer) -> MainThreadDispatcher:
    reg = build_default_registry()
    return MainThreadDispatcher(
        fake_organizer,
        reg,
        permission_mode="autonomous",
        vfs_mounted_check=lambda: False,
    )


def test_all_14_tools_are_registered(fake_organizer):
    reg = build_default_registry()
    names = {s.name for s in reg.all_specs()}
    assert names == {
        "get_environment_info",
        "list_mods",
        "get_mod_details",
        "list_load_order",
        "find_file_owner",
        "list_downloads",
        "read_diagnostic_logs",
        "set_mods_active",
        "set_mod_priority",
        "set_plugins_active",
        "install_mod_from_downloads",
        "create_empty_mod",
        "remove_mod",
        "set_profile_ini_setting",
    }


def test_read_tools_return_accurate_state(fake_organizer):
    disp = _make_dispatcher(fake_organizer)
    # Create a DLL inside SkyUI to test find_file_owner
    dll = fake_organizer.mods_dir / "SkyUI/SKSE/Plugins/skyui_se.dll"
    dll.parent.mkdir(parents=True, exist_ok=True)
    dll.write_text("binary", encoding="utf-8")

    # Create a fake crash log
    crash_log = fake_organizer.logs_dir / "crash-2026-10-31.log"
    crash_log.write_text("Unhandled exception at skyui_se.dll+0x1234\nStack trace line 1\n", encoding="utf-8")

    env = disp.execute("get_environment_info", {})["result"]
    assert env["game_name"] == "Skyrim Special Edition"
    assert env["profile_name"] == "Default"

    mods = disp.execute("list_mods", {"filter_query": "sky", "active_only": True})["result"]["mods"]
    assert len(mods) == 1
    assert mods[0]["name"] == "SkyUI"

    owner = disp.execute("find_file_owner", {"virtual_subpath": "SKSE/Plugins/skyui_se.dll"})["result"]
    assert owner["found"] is True
    assert owner["winning_mod"] == "SkyUI"

    logs = disp.execute("read_diagnostic_logs", {"log_kind": "crash", "tail_lines": 50})["result"]
    assert "skyui_se.dll+0x1234" in logs["content"]


def test_write_tools_crud_operations(fake_organizer):
    disp = _make_dispatcher(fake_organizer)

    # Toggle mods
    res = disp.execute("set_mods_active", {"mod_names": ["Lux"], "active": True})
    assert res["ok"] is True
    assert fake_organizer.modList().state("Lux") == 0x02

    # Change priority
    res = disp.execute("set_mod_priority", {"mod_name": "Lux", "new_priority": 0})
    assert res["ok"] is True
    assert fake_organizer.modList().priority("Lux") == 0

    # Toggle plugin
    res = disp.execute("set_plugins_active", {"plugin_names": ["SkyUI_SE.esp"], "active": False})
    assert res["ok"] is True
    assert fake_organizer.pluginList().state("SkyUI_SE.esp") == 1

    # Install from downloads
    archive = fake_organizer.downloads_dir / "NewMod.7z"
    archive.write_text("archive-bytes", encoding="utf-8")
    res = disp.execute(
        "install_mod_from_downloads",
        {"archive_filename": "NewMod.7z", "suggested_mod_name": "NewModInstalled"},
    )
    assert res["ok"] is True
    assert fake_organizer.modList().getMod("NewModInstalled") is not None

    # Create empty mod & remove it
    res = disp.execute("create_empty_mod", {"mod_name": "TempPatch", "as_separator": False})
    assert res["ok"] is True
    assert (fake_organizer.mods_dir / "TempPatch").is_dir()

    res = disp.execute("remove_mod", {"mod_name": "TempPatch"})
    assert res["ok"] is True
    assert not (fake_organizer.mods_dir / "TempPatch").exists()

    # Profile INI setting
    ini_file = fake_organizer.profile_dir / "SkyrimPrefs.ini"
    ini_file.write_text("[Display]\nbFull Screen=0\n", encoding="utf-8")
    res = disp.execute(
        "set_profile_ini_setting",
        {"ini_filename": "SkyrimPrefs.ini", "section": "Display", "key": "bFull Screen", "value": "1"},
    )
    assert res["ok"] is True
    assert "bFull Screen = 1" in ini_file.read_text(encoding="utf-8") or "bFull Screen=1" in ini_file.read_text(encoding="utf-8")


def test_path_traversal_rejected_in_downloads_and_ini(fake_organizer, tmp_path: Path):
    disp = _make_dispatcher(fake_organizer)
    outside = tmp_path / "secret.ini"
    outside.write_text("[Secret]\nkey=1\n", encoding="utf-8")

    res_dl = disp.execute(
        "install_mod_from_downloads",
        {"archive_filename": "../secret.ini", "suggested_mod_name": "Hack"},
    )
    assert res_dl["ok"] is False

    res_ini = disp.execute(
        "set_profile_ini_setting",
        {"ini_filename": "../../secret.ini", "section": "Secret", "key": "key", "value": "2"},
    )
    assert res_ini["ok"] is False
    assert "key=1" in outside.read_text(encoding="utf-8")
