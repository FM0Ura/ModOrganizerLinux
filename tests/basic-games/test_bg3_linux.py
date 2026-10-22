import subprocess
import urllib.request
from unittest.mock import MagicMock

import pytest
from PyQt6.QtCore import qInstallMessageHandler
from PyQt6.QtWidgets import QMessageBox
from test_detection import _load_basic_games


@pytest.fixture
def qt_messages():
    messages = []
    previous = qInstallMessageHandler(lambda mode, ctx, msg: messages.append(msg))
    yield messages
    qInstallMessageHandler(previous)


@pytest.fixture
def bg3(steam, tmp_path, monkeypatch):
    # nothing may be started or downloaded; every dialog is recorded, never shown
    calls = []
    boxes = []
    for owner, name in (
        (subprocess, "run"),
        (subprocess, "Popen"),
        (urllib.request, "urlopen"),
        (urllib.request, "urlretrieve"),
    ):
        monkeypatch.setattr(owner, name, lambda *a, _n=name, **k: calls.append(_n))
    for name in ("warning", "information", "critical", "question"):
        monkeypatch.setattr(
            QMessageBox, name, staticmethod(lambda *a, **k: boxes.append(a))
        )
    monkeypatch.setattr(QMessageBox, "exec", lambda self: boxes.append(self))

    organizer = MagicMock()
    organizer.pluginDataPath.return_value = str(tmp_path / "plugin-data")
    organizer.profilePath.return_value = str(tmp_path / "profile")
    organizer.basePath.return_value = str(tmp_path / "base")
    organizer.overwritePath.return_value = str(tmp_path / "overwrite")
    game = _load_basic_games()["BG3Game"]
    game.detectGame()
    game.utils.init(organizer)
    return game, tmp_path, calls, boxes


def test_load_order_does_not_use_lslib_on_linux(bg3, qt_messages):
    game, tmp_path, calls, boxes = bg3
    mod = MagicMock()

    # what the plugin does before the game runs, and after a mod is installed
    assert game.utils.construct_modsettings_xml(exec_path="/g/bin/bg3.exe") is True
    game.utils.on_mod_installed(mod)
    assert game.utils.lslib_retriever.download_lslib_if_missing(True) is False

    assert calls == []
    assert not (tmp_path / "plugin-data").exists() or not list(
        (tmp_path / "plugin-data").rglob("*.*")
    )
    assert not (tmp_path / "profile" / "modsettings.lsx").exists()
    mod.absolutePath.assert_not_called()


def test_the_user_is_told_once_and_the_log_has_the_reason(bg3, qt_messages):
    game, _, _, boxes = bg3
    for _ in range(3):
        game.utils.construct_modsettings_xml(exec_path="/g/bin/bg3.exe")
    game.utils.on_mod_installed(MagicMock())

    assert len(boxes) == 1
    text = " ".join(str(a) for a in boxes[0])
    assert "Linux" in text and "load order" in text
    logged = [m for m in qt_messages if "load order" in m and "Linux" in m]
    assert len(logged) == 1
    assert "Divine" in logged[0]


def test_other_executables_do_not_warn(bg3, qt_messages):
    game, _, calls, boxes = bg3
    assert game.utils.construct_modsettings_xml(exec_path="/tools/other.exe") is True
    assert boxes == [] and calls == []
