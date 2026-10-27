import sys

import pytest

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux only")


def test_import_of_a_windows_plugin():
    # what plugins written for Windows do (RootBuilder: common_strings.py)
    import winreg

    try:
        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER, "Software\\Mod Organizer Team\\Mod Organizer"
        ) as key:
            winreg.QueryValueEx(key, "CurrentInstance")
    except FileNotFoundError:
        pass
    else:
        pytest.fail("the key exists")


@pytest.mark.parametrize(
    "call",
    [
        lambda w: w.OpenKey(w.HKEY_LOCAL_MACHINE, "Software\\LOOT"),
        lambda w: w.OpenKeyEx(w.HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, w.KEY_READ),
        lambda w: w.QueryValueEx(w.HKEY_CURRENT_USER, "SteamExe"),
        lambda w: w.QueryValue(w.HKEY_CURRENT_USER, "Software"),
        lambda w: w.EnumKey(w.HKEY_LOCAL_MACHINE, 0),
        lambda w: w.EnumValue(w.HKEY_LOCAL_MACHINE, 0),
        lambda w: w.QueryInfoKey(w.HKEY_CURRENT_USER),
        lambda w: w.DeleteValue(w.HKEY_CURRENT_USER, "x"),
        lambda w: w.ConnectRegistry(None, w.HKEY_LOCAL_MACHINE),
    ],
)
def test_every_key_is_missing(call):
    import winreg

    with pytest.raises(FileNotFoundError):
        call(winreg)


@pytest.mark.parametrize(
    "call",
    [
        lambda w: w.CreateKey(w.HKEY_CURRENT_USER, "Software\\x"),
        lambda w: w.CreateKeyEx(w.HKEY_CURRENT_USER, "Software\\x"),
        lambda w: w.SetValueEx(w.HKEY_CURRENT_USER, "x", 0, w.REG_SZ, "y"),
        lambda w: w.SetValue(w.HKEY_CURRENT_USER, "x", w.REG_SZ, "y"),
    ],
)
def test_writes_are_refused(call):
    import winreg

    with pytest.raises(PermissionError):
        call(winreg)


def test_errors_are_oserror():
    import winreg

    assert winreg.error is OSError


def test_expand_environment_strings(monkeypatch):
    import winreg

    monkeypatch.setenv("MO2_TEST_VAR", "/home/user")
    monkeypatch.delenv("MO2_TEST_MISSING", raising=False)
    assert (
        winreg.ExpandEnvironmentStrings("%MO2_TEST_VAR%\\a %MO2_TEST_MISSING%")
        == "/home/user\\a %MO2_TEST_MISSING%"
    )


def test_close_key_accepts_predefined_keys():
    import winreg

    winreg.CloseKey(winreg.HKEY_CURRENT_USER)
