# Phase 1 — `modorganizer` Linux compile errors (checklist for Tasks 9–12)

Recorded in Task 8 (fork `claude/serene-maxwell-1dyrbg`, GCC 16, Qt 6.10, local env)
with `./build.sh --only modorganizer`, then `ninja -k 0` in `build-out/modorganizer`.
Tick an item when the file compiles on Linux. Category: **Win32 API** (Win32
function/constant/type used directly), **include** (Windows-only header), **type**
(Win32 type in a shared signature), **compiler** (MSVC-only C++), **path**, **build**.

## 0. Precompiled header (blocks every file)

- [x] `src/pch.h` — 2 errors (fixed in Task 9: Windows includes under `#ifdef _WIN32`,
  `<uibase/win32compat.h>` otherwise, include case fixed). `pch.h:36: fatal error: Windows.h: No such file or directory`
  (include). Fix: put the `<Windows.h>`…`<windowsx.h>` block and `<tlhelp32.h>` under
  `#ifdef _WIN32`. Then `pch.h:119: QFIleIconProvider: No such file or directory`
  (path: case typo, must be `<QFileIconProvider>`).

The list below was collected with those two fixes applied locally (not committed), so
that each file reports its own errors. GCC stops a file at its first missing header, so
fixing a header can uncover more errors in the files that include it.
Totals: 154 objects (sources, moc, rcc), **104 failed**, 50 built; 70 distinct errors in 18 files.

## 1. Headers (each blocks many sources)

| ✓ | File | Errors | Sources blocked | First error | Category |
|---|------|-------:|----------------:|-------------|----------|
| [x] | `installationmanager.h` | 1 | 47 (incl. `mocs_compilation.cpp`, `organizercore.cpp`, `main.cpp`, `modinfo*.cpp`, `modlist*.cpp`, `downloadmanager.cpp`, …) | `:33: Windows.h: No such file or directory` | include |
| [x] | `settings.h` | 1 | 29 (`profile.cpp`, `settingsdialog*.cpp`, `instancemanager*.cpp`, `nexusinterface.cpp`, …) | `:29: usvfs/usvfsparameters.h: No such file or directory` | include (usvfs → Task 11: include Windows-only) |
| [x] | `loot.h` | 1 | 5 (`lootdialog`, `pluginlist`, `pluginlistproxy`, `pluginlistsortproxy`, `genericicondelegate`) | `:8: windows.h: No such file or directory` | include (Task 11: Windows-only) |
| [x] | `mainwindow.h` | 1 | 5 (`moapplication`, `modlistview`, `pluginlistview`, `uilocker`; `shared/util.cpp` is no longer built on Linux, see section 3) | `:109: Windows.h: No such file or directory` | include (Task 11: Windows-only) |
| [x] | `shared/fileentry.h` | 5 | 4 (`fileregister`, `filesorigin`, `modinfodialogconflictsmodels`, `syncoverwritedialog`) | `:23: 'FILETIME' has not been declared` | type (Task 9) |
| [x] | `shared/directoryentry.h` | 3 | 2 (`fileregister`, `syncoverwritedialog`) | `:231: 'FILETIME' has not been declared` | type (Task 9) |
| [x] | `shared/util.h` | 1 | 2 (`aboutdialog`, `shared/originconnection.cpp`) | `:58: 'FILETIME' does not name a type` | type (Task 9) |
| [x] | `shared/windows_error.h` | 1 | 1 (`shared/directoryentry.cpp`) | `:25: windows.h: No such file or directory` | include (Task 9: its include in `directoryentry.cpp` is Windows-only; the header itself → Task 12) |
| [x] | `spawn.h` | 1 | 1 (`processrunner.cpp`) | `:26: tchar.h: No such file or directory` | include (Task 11: Windows-only, `win32compat.h` otherwise) |

## 2. Sources with their own errors

| ✓ | File | Errors | First error | Category |
|---|------|-------:|-------------|----------|
| [x] | `settingsutilities.cpp` | 23 | `:217: 'CRED_TYPE_GENERIC' was not declared` (Windows Credential Manager: `CredWriteW`/`CredReadW`/`CredFree`) | Win32 API |
| [x] | `filetreeitem.cpp` | 17 | `:18: 'SHGFI_TYPENAME' was not declared` (`SHGetFileInfoW` file type names) | Win32 API |
| [x] | `shared/fileentry.cpp` | 3 | `:18: 'FILETIME' has not been declared` | type (Task 9) |
| [x] | `iconfetcher.cpp` | 2 | `:24: 'GetSystemMetrics'` / `'SM_CXSMICON' was not declared` | Win32 API |
| [x] | `qdirfiletree.cpp` | 2 (+5 reported in `<bits/stl_construct.h>`) | `:60: cannot define member function 'QDirFileTree::doClone' within 'QDirFileTreeImpl'` (then `make_shared` of an abstract class) | compiler (MSVC accepts) |
| [x] | `multiprocess.cpp` | 1 | `:53: 'Sleep' was not declared in this scope` | Win32 API |
| [x] | `browserview.cpp` | 1 | `:29: Shlwapi.h: No such file or directory` | include |
| [x] | `messagedialog.cpp` | 1 | `:24: Windows.h: No such file or directory` | include |

## 3. Expected at link time (not compiled on Linux, need `src/linux/<name>_linux.cpp`)

Excluded from the Linux build by `src/CMakeLists.txt`; their symbols will be missing at
link time until the Linux files exist.

- [x] `env.cpp`, `envmetrics.cpp`, `envmodule.cpp`, `envsecurity.cpp`, `envshell.cpp`,
  `envshortcut.cpp`, `envwindows.cpp` → Task 10: built on both platforms with their
  Win32 functions in `#ifdef _WIN32` blocks, the Linux ones in
  `src/linux/env*_linux.cpp` (crash dumps of `envdump.h`: `envdump_linux.cpp`); all
  `env::` symbols are defined on Linux. `env.cpp` itself still stops at `settings.h`
  (usvfs, Task 11), like the other sources that include it (checked by compiling it
  with an empty `usvfs/usvfsparameters.h`: no other error).
- [x] `envfs.cpp` → Task 9 (`src/linux/envfs_linux.cpp`)
- [x] `spawn.cpp`, `usvfsconnector.cpp` → Task 11: `src/linux/spawn_linux.cpp` and
  `src/linux/vfsconnector_linux.cpp` (`NullVfsConnector`, `createVfsConnector()`); the
  usvfs process list (`getRunningUSVFSProcesses()`) is only used by
  `ProcessRunner::waitForAllUSVFSProcessesWithLock()`, whose Linux version is in
  `src/linux/processrunner_linux.cpp`
- [x] `shared/windows_error.cpp` → Task 12 (`src/linux/windows_error_linux.cpp`)
- [x] `shared/util.cpp` (excluded in Task 9) → Task 12 (built on both platforms again, see "After Task 12"): `src/linux/util_linux.cpp` has the
  string, file and thread-name helpers (and `getUsvfsVersionString()`, "none", since
  Task 11); still missing on Linux: `createVersionInfo()`,
  `checkDuplicateShortcuts()`, `ExitModOrganizer()`,
  `ModOrganizerCanCloseNow()`, `ModOrganizerExiting()`, `ResetExitFlag()`, `isNxmLink()`

## After Task 9

`shared/` data tree (`directoryentry`, `fileentry`, `fileregister`, `filesorigin`,
`originconnection`) and `src/linux/{envfs,util}_linux.cpp` build on Linux as the static
library `organizer_datatree` (tested by `organizer_tests`). The uibase headers are now
searched after the system ones on Linux (`-idirafter`): uibase's `strings.h` hid the
system `<strings.h>` and gave the `MOBase` string functions C linkage in every source.
`ninja -k 0` after Task 9: 151 objects, **95 failed**; the remaining first errors are
the unticked items above (`installationmanager.h` 46, `settings.h` 29,
`settingsutilities.cpp`, `filetreeitem.cpp`, `loot.h`, `mainwindow.h`, `qdirfiletree.cpp`,
`iconfetcher.cpp`, `spawn.h`, `multiprocess.cpp`, `messagedialog.cpp`, `browserview.cpp`).

## After Task 10

The environment layer except `env.cpp`/`env_linux.cpp` (`envmetrics`, `envmodule`,
`envsecurity`, `envshell`, `envshortcut`, `envwindows` and their `*_linux.cpp`, plus
`envdump_linux.cpp`) is the static library `organizer_env`, and `archivefiletree.cpp`
(the file tree of archives used by the installation) is `organizer_archivetree`; both
are linked by the application and by `organizer_tests` (22 tests). The paths of the data tree
(`FileEntry::getFullPath()`, `getRelativePath()`) use the separator of the system,
and `MOShared::normalizeSeparators()` (`shared/util.h`) converts the backslashes of
paths from user files. `ninja -k 0` after Task 10: 148 objects for the application,
**96 failed** (one more than after Task 9: `env.cpp` is now built and stops at
`settings.h`); the first errors are the same unticked items as above.

Path call sites left to the tasks that port their files (Win32 code around them):
`pluginlist.cpp` (~829, `CreateFile` on the origin path + `\` + plugin name, Task
12), `mainwindow.cpp` (~2527, `FindFirstFileW` on the origin path + `\` + file, Task
12), `organizercore.cpp` `fileMapping()` (VFS mappings joined with `\`, Task 11).

## After Task 11

`VfsConnector` (`vfsconnector.h`) replaces the usvfs member of `OrganizerCore`; usvfs
headers are only included on Windows (`settings.h`, `organizercore.{h,cpp}`,
`mainwindow.cpp`, `spawn.h`). The Linux parts of running programs (`spawn_linux.cpp`,
`vfsconnector_linux.cpp`, `loot_linux.cpp`) are the static library `organizer_launch`,
linked by the application and by `organizer_tests` (30 tests);
`processrunner_linux.cpp` is in the application. `env.cpp` now builds.
`organizercore.cpp` `fileMapping()` joins the relative paths of the data tree with
`normalizeSeparators(... + "\\")`, and `NullVfsConnector` stores every mapping with `/`.

`ninja -k 0` after Task 11: 149 objects for the application, **73 failed** (96 after
Task 10). 60 of them stop at `installationmanager.h:33: Windows.h` (Task 12). With that
include guarded locally (not committed), `organizercore.cpp`, `processrunner.cpp`,
`linux/processrunner_linux.cpp`, `selfupdater.cpp`, `sanitychecks.cpp`, `uilocker.cpp`,
`aboutdialog.cpp` and `pluginlistview.cpp` build; the files of Task 11 that still fail
stop at errors of Task 12:

- [x] `json.h:117` `typeid(T).name` without `()` (MSVC accepts it in an uninstantiated
  template; blocks `loot.cpp`, which builds once it is fixed)
- [x] `mainwindow.cpp`: `<QFIleIconProvider>` (case), `<shlobj.h>`, then
  `FindFirstFileW` (~2527), `GetLastError` (~3908/3933), `UINT32`
- [x] `moapplication.cpp`: `<QSSLSocket>` (case: `<QSslSocket>`), `GetCommandLineW()` (~207)
- [x] `lootdialog.cpp:319`: `GetLastError`

Uncovered by the `settings.h` fix (they stopped at `settings.h` before):

- [x] `settings.cpp`: `GetLastError`; `settingsutilities.h`: `QVariant` from `long` /
  `unsigned long` is ambiguous (instantiated from `settings.cpp:266` and `:2472`)
- [x] `profile.cpp`: `Windows.h`
- [x] `instancemanagerdialog.cpp`: `GetLastError`, `ERROR_CANCELLED`
- [x] `nexusinterface.cpp`: uibase `moassert.h` uses `IsDebuggerPresent()`/`DebugBreak()`
  (needs a Linux branch in uibase), then `__FUNCSIG__` (MSVC-only)

## After Task 12

The application compiles and links on Linux (`./build.sh --tests`, no `--allow-fail`)
and `install/bin/ModOrganizer --help` runs headless (smoke test of `build.sh --tests`
and of the Linux CI). How the remaining items were fixed:

- Windows-only includes (`installationmanager.{h,cpp}`, `profile.cpp`,
  `profilesdialog.cpp`, `messagedialog.cpp`, `browserview.cpp`, `motddialog.cpp`,
  `overwriteinfodialog.cpp`, `problemsdialog.cpp`, `editexecutablesdialog.cpp`,
  `mainwindow.cpp`, `shared/windows_error.h`) are under `#ifdef _WIN32`;
  `usvfsconnector.h` is not given to moc on Linux.
- `src/linux/win32compat_linux.h` (included by `pch.h` on Linux) has the few Win32
  functions the shared code calls directly, with their Windows meaning:
  `GetLastError()` (errno), `ERROR_CANCELLED`, `Sleep()`, `UINT32`, `GetProcessId()`
  (a `HANDLE` is a pid), `GetCommandLineW()` (from `/proc/self/cmdline`, parsed back by
  `split_unix()` in `commandline.cpp`), `GetPrivateProfileStringW()` /
  `GetPrivateProfileSectionNamesW()` (`profile.cpp` INI tweaks, `savestab.cpp`) and
  `__FUNCSIG__`. This covers `settings.cpp`, `lootdialog.cpp`, `instancemanagerdialog.cpp`,
  `modlist.cpp`, `modlistviewactions.cpp`, `modinforegular.cpp`, `organizerproxy.cpp`,
  `savestab.cpp`, `profile.cpp`, `multiprocess.cpp`, `installationmanager.cpp`,
  `filetreemodel.cpp` and `nexusinterface.cpp` without changing them.
- Mixed files with Win32 functions in `#ifdef _WIN32` and the Linux ones in
  `src/linux/`: `shared/util.cpp` (`util_linux.cpp`, now with `createVersionInfo()`
  from `version.rc`), `shared/windows_error.cpp` (`windows_error_linux.cpp`, still
  excluded), `main.cpp` (`main_linux.cpp`), `commandline.cpp`
  (`commandline_linux.cpp`), `filetreeitem.cpp` (`filetreeitem_linux.cpp`),
  `pluginlist.cpp` (`pluginlist_linux.cpp`), `settingsutilities.cpp`
  (`settingsutilities_linux.cpp`); one-line `#ifdef`s in `iconfetcher.cpp`,
  `mainwindow.cpp` (`fileMoved()`), `loglist.cpp` (crash log) and `shared/util.h`
  (`ToFILETIME()`).
- Portable C++ fixes visible on Windows (same meaning with MSVC): `json.h`
  (`typeid(T).name()`), `qdirfiletree.cpp` (`doClone() const override`),
  `modlistdropinfo.cpp` (`m_url.clear()`), `modinfodialog.cpp` (`path().wstring()`),
  `settings.cpp` (`qulonglong`/`qlonglong` casts), `mainwindow.cpp` (`QFileIconProvider`
  case, `filetree.h` include, `QString::fromStdWString()`), `moapplication.cpp`
  (`QSslSocket` case).
- `uibase`: `moassert.h` has a Linux branch (debugger check through
  `/proc/self/status`, `__PRETTY_FUNCTION__`); the type information of the plugin
  requirements is exported on Linux (`pluginrequirements.h`, `dynamic_cast` in
  `plugincontainer.cpp`).
