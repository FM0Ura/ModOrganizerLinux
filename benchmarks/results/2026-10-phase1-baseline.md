# Phase 1 performance baseline

Refresh of the data tree (`DirectoryEntry` built from every mod of a synthetic instance),
measured by `benchmarks/run.sh`. Each run appends a section below.

## 2026-10-15

- superproject commit: `3f713d2b`; fork (modorganizer) commit: `e6e4b49` plus the uncommitted benchmark sources of the commit that adds this file (`bench: refresh benchmark and phase 1 baseline`)
- CPU: Intel(R) Xeon(R) Processor @ 2.10GHz (4 logical CPUs); kernel: Linux 6.18.44-fc-v114
- size: 2000 mods, 200000 files, seed 1, 10 refresh threads
- build: RelWithDebInfo, x86_64-conda-linux-gnu-g++
- **refresh_ms_median = 1580** (5 timed repetitions after one warm-up: 1562.0,1551.2,1585.8,1579.6,1587.5 ms)
- **peak_rss_kib = 339656**
- first run (before the timed ones): 10290.8 ms; page cache dropped before the first run
- files registered: 178073

### Caveats (apply to every number above)

- The timed region excludes tree teardown: the real refresh destroys the old tree inside its
  timer, so the application's refresh takes somewhat longer than `refresh_ms_median`.
- Only loose files are covered; no BSA/archive parsing is part of the measurement.
- The data is synthetic (see `benchmarks/gen_instance.py`); real instances differ in file
  sizes, name lengths and conflict ratio.

## 2026-10-15: startup time to the main window (spec 4.6)

- superproject commit: `460063a8`; fork (modorganizer) commit: `bb46b18`; binary: `install/bin/ModOrganizer` built in the local conda-forge environment (RelWithDebInfo)
- **startup_to_main_window_s_median = 1.207** (3 timed runs: 1.207, 1.211, 1.198 s; one untimed first run of the instance gave 1.225 s)

### Method

- Headless, as a non-root user (a temporary user, deleted afterwards; QtWebEngine refuses to run as root):
  `QT_QPA_PLATFORM=offscreen QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu`, `HOME` and
  `XDG_RUNTIME_DIR` of that user, `ModOrganizer --logs -i bench`.
- Instance: a hand-made global instance (`~/.local/share/ModOrganizer/bench/ModOrganizer.ini` with
  `gameName=Skyrim Special Edition`, `game_edition=Steam`, `selected_profile=Default`,
  `log_level=0` for debug) pointing to a fake game directory containing an empty `SkyrimSE.exe`
  and `Data/Skyrim.esm`; no mods installed.
- Timer: a small Python wrapper takes `time.monotonic()` just before starting the process
  (`subprocess.Popen`) and stops at the first stdout line containing `displaying main window`
  (logged by `MOApplication::doOneRun()` right before `MainWindow::show()`), then terminates the
  process. This covers process start, plugin loading, instance setup and construction of the main
  window (~1.04 s of the total is `MainWindow` setup, from the log timing `doOneRun() splash`).
- Runs were consecutive, warm (files already in the page cache), 2 s apart.

### Caveats

- Offscreen Qt platform, no GPU (`--disable-gpu`), local conda environment with its own Qt 6.10.2,
  not the Fedora 44 container used on the Bazzite target; 4 logical CPUs, 16 GB RAM.
- An empty instance: no mods, no plugins list, no downloads. A real instance with many mods adds
  the refresh time measured above (about 1.6 s for 2000 mods / 200000 files) and plugin/BSA work.
- The time to the window being actually painted on a real display (compositor, GPU process of
  QtWebEngine) is not measured here.
