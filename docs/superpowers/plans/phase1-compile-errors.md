# Phase 1 — `modorganizer` Linux compile errors (checklist for Tasks 9–12)

Recorded in Task 8 (fork `claude/serene-maxwell-1dyrbg`, GCC 16, Qt 6.10, local env)
with `./build.sh --only modorganizer`, then `ninja -k 0` in `build-out/modorganizer`.
Tick an item when the file compiles on Linux. Category: **Win32 API** (Win32
function/constant/type used directly), **include** (Windows-only header), **type**
(Win32 type in a shared signature), **compiler** (MSVC-only C++), **path**, **build**.

## 0. Precompiled header (blocks every file)

- [ ] `src/pch.h` — 2 errors. `pch.h:36: fatal error: Windows.h: No such file or directory`
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
| [ ] | `installationmanager.h` | 1 | 47 (incl. `mocs_compilation.cpp`, `organizercore.cpp`, `main.cpp`, `modinfo*.cpp`, `modlist*.cpp`, `downloadmanager.cpp`, …) | `:33: Windows.h: No such file or directory` | include |
| [ ] | `settings.h` | 1 | 29 (`profile.cpp`, `settingsdialog*.cpp`, `instancemanager*.cpp`, `nexusinterface.cpp`, …) | `:29: usvfs/usvfsparameters.h: No such file or directory` | include (usvfs → Task 11) |
| [ ] | `loot.h` | 1 | 5 (`lootdialog`, `pluginlist`, `pluginlistproxy`, `pluginlistsortproxy`, `genericicondelegate`) | `:8: windows.h: No such file or directory` | include |
| [ ] | `mainwindow.h` | 1 | 5 (`moapplication`, `modlistview`, `pluginlistview`, `uilocker`, `shared/util.cpp`) | `:109: Windows.h: No such file or directory` | include |
| [ ] | `shared/fileentry.h` | 5 | 4 (`fileregister`, `filesorigin`, `modinfodialogconflictsmodels`, `syncoverwritedialog`) | `:23: 'FILETIME' has not been declared` | type (Task 9) |
| [ ] | `shared/directoryentry.h` | 3 | 2 (`fileregister`, `syncoverwritedialog`) | `:231: 'FILETIME' has not been declared` | type (Task 9) |
| [ ] | `shared/util.h` | 1 | 2 (`aboutdialog`, `shared/originconnection.cpp`) | `:58: 'FILETIME' does not name a type` | type (Task 9) |
| [ ] | `shared/windows_error.h` | 1 | 1 (`shared/directoryentry.cpp`) | `:25: windows.h: No such file or directory` | include |
| [ ] | `spawn.h` | 1 | 1 (`processrunner.cpp`) | `:26: tchar.h: No such file or directory` | include (Task 11) |

## 2. Sources with their own errors

| ✓ | File | Errors | First error | Category |
|---|------|-------:|-------------|----------|
| [ ] | `settingsutilities.cpp` | 23 | `:217: 'CRED_TYPE_GENERIC' was not declared` (Windows Credential Manager: `CredWriteW`/`CredReadW`/`CredFree`) | Win32 API |
| [ ] | `filetreeitem.cpp` | 17 | `:18: 'SHGFI_TYPENAME' was not declared` (`SHGetFileInfoW` file type names) | Win32 API |
| [ ] | `shared/fileentry.cpp` | 3 | `:18: 'FILETIME' has not been declared` | type (Task 9) |
| [ ] | `iconfetcher.cpp` | 2 | `:24: 'GetSystemMetrics'` / `'SM_CXSMICON' was not declared` | Win32 API |
| [ ] | `qdirfiletree.cpp` | 2 (+5 reported in `<bits/stl_construct.h>`) | `:60: cannot define member function 'QDirFileTree::doClone' within 'QDirFileTreeImpl'` (then `make_shared` of an abstract class) | compiler (MSVC accepts) |
| [ ] | `multiprocess.cpp` | 1 | `:53: 'Sleep' was not declared in this scope` | Win32 API |
| [ ] | `browserview.cpp` | 1 | `:29: Shlwapi.h: No such file or directory` | include |
| [ ] | `messagedialog.cpp` | 1 | `:24: Windows.h: No such file or directory` | include |

## 3. Expected at link time (not compiled on Linux, need `src/linux/<name>_linux.cpp`)

Excluded from the Linux build by `src/CMakeLists.txt`; their symbols will be missing at
link time until the Linux files exist.

- [ ] `env.cpp`, `envmetrics.cpp`, `envmodule.cpp`, `envsecurity.cpp`, `envshell.cpp`,
  `envshortcut.cpp`, `envwindows.cpp` → Task 10
- [ ] `envfs.cpp` → Task 9
- [ ] `spawn.cpp`, `usvfsconnector.cpp` → Task 11
- [ ] `shared/windows_error.cpp` → Task 12
