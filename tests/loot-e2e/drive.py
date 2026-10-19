#!/usr/bin/env python3
"""End-to-end test of LOOT on Linux (spec of Phase 4b, section 5).

    drive.py <install prefix> <testing-plugins SkyrimSE/Data> <work directory>

lootcli, started by mo2-vfs with the virtual file system mounted over the data
directory of a fake Skyrim Special Edition, sorts plugins that only the mods have:
- mounted, with the masterlist of tests/fixtures/loot-e2e: exit 0, the mods' plugins
  are in the sorted load order, which follows the masterlist rule, and the report is
  valid JSON;
- mounted, masterlist downloaded from a file:// source in settings.toml: the same, and
  the masterlist was written;
- not mounted: the mods' plugins are not in the sorted load order (proof that the
  virtual file system is what showed them);
- no masterlist with --skipUpdateMasterlist: exit 1, "Masterlist not found".
Everything stays in the work directory (HOME and XDG_DATA_HOME point inside it).
"""

import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

INSTALL, PLUGINS, WORK = (Path(a).resolve() for a in sys.argv[1:4])
VFS = INSTALL / "bin/mo2-vfs"
LOOTCLI = INSTALL / "bin/loot/lootcli"
FIXTURE = Path(__file__).resolve().parent.parent / "fixtures/loot-e2e"
TIMEOUT = float(os.environ.get("MO2_LOOT_E2E_TIMEOUT", "120"))

# non-ASCII and spaces on the way, like real instances
ROOT = WORK / "Módulos e Jogos"
GAME = ROOT / "Skyrim Special Edition"
DATA = GAME / "Data"
MODS = ROOT / "mods"
PROFILE = ROOT / "profile"
XDG = WORK / "xdg"
LOOT_GAME = XDG / "LOOT/games/Skyrim Special Edition"


def fail(message):
    print(f"loot e2e test FAILED: {message}", file=sys.stderr)
    sys.exit(1)


class Helper:
    """mo2-vfs and its line protocol; events that come while waiting for the answer to
    a request (exited, unmounted) are kept for until()."""

    def __init__(self, env):
        self.proc = subprocess.Popen(
            [str(VFS), "--log", str(WORK / "mo2-vfs.log"), "--log-level", "debug"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, env=env)
        self.events = queue.Queue()
        self.pending = []
        threading.Thread(target=self._read, daemon=True).start()
        hello = self._next(10)
        if hello.get("event") != "hello":
            fail(f"mo2-vfs said {hello}")

    def _read(self):
        for line in self.proc.stdout:
            self.events.put(json.loads(line))
        self.events.put(None)

    def _next(self, timeout):
        try:
            event = self.events.get(timeout=timeout)
        except queue.Empty:
            fail(f"no event from mo2-vfs within {timeout:.0f}s")
        if event is None:
            fail("mo2-vfs ended")
        return event

    def request(self, request):
        self.proc.stdin.write(json.dumps(request) + "\n")
        self.proc.stdin.flush()
        while True:
            event = self._next(30)
            if event.get("event") in ("exited", "unmounted"):
                self.pending.append(event)
                continue
            return event

    def until(self, predicate, timeout):
        for i, event in enumerate(self.pending):
            if predicate(event):
                return self.pending.pop(i)
        deadline = time.monotonic() + timeout
        while True:
            event = self._next(max(0.1, deadline - time.monotonic()))
            if predicate(event):
                return event
            self.pending.append(event)

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(30)


def set_up():
    DATA.mkdir(parents=True)
    (GAME / "SkyrimSE.exe").write_bytes(b"")
    shutil.copy(PLUGINS / "Blank.esm", DATA / "Skyrim.esm")
    (MODS / "ModA").mkdir(parents=True)
    shutil.copy(PLUGINS / "Blank.esm", MODS / "ModA/Blank.esm")
    shutil.copy(PLUGINS / "Blank.esp", MODS / "ModA/Blank.esp")
    (MODS / "ModB").mkdir(parents=True)
    shutil.copy(PLUGINS / "Blank - Different.esp", MODS / "ModB/Blank - Different.esp")
    PROFILE.mkdir(parents=True)
    (PROFILE / "plugins.txt").write_text("*Blank.esm\n*Blank.esp\n*Blank - Different.esp\n")
    (PROFILE / "loadorder.txt").write_text(
        "Skyrim.esm\nBlank.esm\nBlank.esp\nBlank - Different.esp\n")
    LOOT_GAME.mkdir(parents=True)
    shutil.copy(FIXTURE / "masterlist.yaml", LOOT_GAME / "masterlist.yaml")


def mount(helper):
    def mapping(mod):
        return {"source": str(MODS / mod), "destination": str(DATA),
                "is_directory": True, "create_target": False}

    event = helper.request({"op": "mount", "mappings": [mapping("ModB"), mapping("ModA")]})
    if event.get("event") != "ready":
        fail(f"mount: {event}")
    if not (DATA / "Blank.esp").exists():
        fail("the mounted data directory does not show the plugins of the mods")


def run_lootcli(helper, name, skip_update=True, env=None):
    """Runs lootcli through mo2-vfs; its exit code, output and sorted load order."""
    out = WORK / name
    out.mkdir()
    argv = [str(LOOTCLI), "--game", "SkyrimSE", "--gamePath", str(GAME),
            "--pluginListPath", str(PROFILE / "loadorder.txt"), "--logLevel", "debug",
            "--out", str(out / "lootreport.json"),
            "--pluginListOutputPath", str(out / "loot-loadorder.txt"),
            "--language", "en"]
    if skip_update:
        argv.append("--skipUpdateMasterlist")
    event = helper.request({"op": "spawn", "argv": argv, "env": env or {},
                            "cwd": str(LOOTCLI.parent), "output": str(out / "lootcli.log")})
    if event.get("event") != "spawned":
        fail(f"spawn of lootcli: {event}")
    pid = event["pid"]
    exited = helper.until(lambda e: e.get("event") == "exited" and e.get("pid") == pid,
                          TIMEOUT)
    output = (out / "lootcli.log").read_text(errors="replace")
    order_file = out / "loot-loadorder.txt"
    order = []
    if order_file.exists():
        order = [line for line in order_file.read_text().splitlines()
                 if line and not line.startswith("#")]
    return exited["code"], output, order, out


def check_sorted(name, code, output, order, out):
    if code != 0:
        fail(f"{name}: lootcli exited with {code}:\n{output}")
    if "[error]" in output:
        fail(f"{name}: lootcli logged an error:\n{output}")
    if "[progress]" not in output:
        fail(f"{name}: no progress in the output of lootcli:\n{output}")
    for plugin in ("Skyrim.esm", "Blank.esm", "Blank.esp", "Blank - Different.esp"):
        if plugin not in order:
            fail(f"{name}: {plugin} is not in the sorted load order {order}")
    if order.index("Blank - Different.esp") > order.index("Blank.esp"):
        fail(f"{name}: the masterlist rule was not applied: {order}")
    report = json.loads((out / "lootreport.json").read_text())
    if not isinstance(report.get("plugins"), list) or "stats" not in report:
        fail(f"{name}: unexpected report {report}")


def main():
    set_up()
    env = dict(os.environ, HOME=str(WORK / "home"), XDG_DATA_HOME=str(XDG),
               MO2_VFS_STATE_DIR=str(WORK / "state"))
    (WORK / "home").mkdir()
    helper = Helper(env)

    print("--- mounted, cached masterlist")
    mount(helper)
    check_sorted("mounted", *run_lootcli(helper, "mounted"))
    # its group ended: mo2-vfs unmounts by itself
    helper.until(lambda e: e.get("event") == "unmounted", 30)

    print("--- mounted, masterlist downloaded from file://")
    source = WORK / "served-masterlist.yaml"
    shutil.copy(FIXTURE / "masterlist.yaml", source)
    (XDG / "LOOT/settings.toml").write_text(
        '[[games]]\ngameId = "SkyrimSE"\nfolder = "Skyrim Special Edition"\n'
        f'masterlistSource = "{source.as_uri()}"\n')
    (LOOT_GAME / "masterlist.yaml").unlink()
    mount(helper)
    check_sorted("download", *run_lootcli(helper, "download", skip_update=False))
    if (LOOT_GAME / "masterlist.yaml").read_text() != source.read_text():
        fail("the downloaded masterlist was not written")
    helper.until(lambda e: e.get("event") == "unmounted", 30)

    print("--- not mounted")
    code, output, order, _ = run_lootcli(helper, "unmounted")
    if code == 0 and "Blank.esp" in order:
        fail(f"without the virtual file system lootcli still saw Blank.esp: {order}")

    print("--- no masterlist")
    empty = WORK / "empty-xdg"
    empty.mkdir()
    code, output, _, _ = run_lootcli(helper, "no-masterlist", env={"XDG_DATA_HOME": str(empty)})
    if code != 1 or "Masterlist not found" not in output:
        fail(f"no masterlist: exit {code}, expected 1 and 'Masterlist not found':\n{output}")

    helper.close()
    print("loot e2e test passed")


main()
