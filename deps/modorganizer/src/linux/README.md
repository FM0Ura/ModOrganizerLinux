# Linux implementations

This folder holds the Linux implementations of the parts of Mod Organizer that are
Windows-only. It is compiled on Linux only; `src/CMakeLists.txt` removes it from the
Windows build and removes the Windows-only files it replaces from the Linux build.

Rules:

- One `<name>_linux.cpp` per replaced Windows file: `envfs.cpp` is replaced by
  `envfs_linux.cpp`, `spawn.cpp` by `spawn_linux.cpp`, and so on. The header
  (`envfs.h`, `spawn.h`, ...) stays shared in `src/`.
- The Windows file is not moved or rewritten. Either it is excluded from the Linux
  build by the list in `src/CMakeLists.txt` (currently `envfs.cpp`, `spawn.cpp`,
  `usvfsconnector.{h,cpp}` and `shared/windows_error.cpp`, plus resource scripts and
  manifests) and the Linux file replaces all of it, or, for files that mix shared and
  Win32 code, it is built on both platforms with its Win32 functions in
  `#ifdef _WIN32` blocks and the Linux file only implements those functions (the
  other `env*.cpp`: `env.cpp`, `envmetrics.cpp`, `envmodule.cpp`, `envsecurity.cpp`,
  `envshell.cpp`, `envshortcut.cpp`, `envwindows.cpp`; `envdump_linux.cpp` implements
  the crash dumps of `envdump.h`, which are in `env.cpp` on Windows; `processrunner.cpp`
  and `loot.cpp`, whose `processrunner_linux.cpp` runs native and Windows programs
  through `mo2-vfs` (`VfsConnector::spawn()`, with the launch decided by
  `protonlauncher_linux.cpp`: Windows programs through `umu-run` in the Proton prefix of
  their Steam game), gets the exit code from the helper (`exitcodes_linux.cpp`) and waits
  for the programs it started, and `loot_linux.cpp` disables LOOT until Phase 4;
  `loot_linux.h` declares the Linux `AsyncPipe` class, defined in `loot.cpp` on Windows; `shared/util.cpp` →
  `util_linux.cpp`; `main.cpp` → `main_linux.cpp` (crash handlers, see below);
  `commandline.cpp` → `commandline_linux.cpp`; `filetreeitem.cpp` →
  `filetreeitem_linux.cpp` (file type names from the shared MIME database);
  `pluginlist.cpp` → `pluginlist_linux.cpp` (plugin file times);
  `settingsutilities.cpp` → `settingsutilities_linux.cpp` (credentials, kept in
  memory only until there is a secret store)).
- `win32compat_linux.h`, included by `pch.h` on Linux after `<uibase/win32compat.h>`,
  has the few Win32 functions and MSVC macros that shared code calls directly
  (`GetLastError()` is `errno`, `Sleep()`, `GetProcessId()` of a pid `HANDLE`,
  `GetCommandLineW()` from `/proc/self/cmdline` in the format that
  `boost::program_options::split_unix()` parses, `GetPrivateProfileStringW()` and
  `GetPrivateProfileSectionNamesW()` with the Windows INI rules, `__FUNCSIG__`), with
  their Windows meaning; `win32compat_linux.cpp` implements the ones that are not
  inline.
- Crash handlers: `setExceptionHandlers()` (`main_linux.cpp`) installs the signal
  handlers of `envdump.h` once; `setLogDirectory()` (`loglist.cpp`) points them to
  `crash.log` in the log directory of the instance, next to the Mod Organizer log.
- New platform interfaces are shared headers with one implementation per platform:
  `vfsconnector.h` (`VfsConnector`, `createVfsConnector()`) is implemented by
  `usvfsconnector.cpp` on Windows and by `vfsconnector_linux.cpp` (`LinuxVfsConnector`)
  on Linux. `LinuxVfsConnector` does not link any VFS code: it starts the `mo2-vfs`
  helper process (found next to the executable, or in `MO2_VFS_BINARY`) and talks to it
  with JSON lines over its stdin/stdout through `vfsclient_linux.{h,cpp}`; the helper
  mounts only while a program runs and unmounts when the last watched process group
  ends (spec: `docs/superpowers/specs/2026-10-15-phase2-vfs-fuse-design.md`).
  `posixspawn_linux.{h,cpp}` (its own small library) starts programs in a process group of their own.
  `vfsbridge_linux.{h,cpp}` decides where `mo2-vfs` runs: directly, or on the host through
  `distrobox-host-exec` when Mod Organizer is in a container (`MO2_VFS_BRIDGE=direct|host|auto`;
  auto looks for `/run/.containerenv` or `/.dockerenv`), so that the mounts and the programs
  are those of the host.
- `envfs_linux.cpp`, `util_linux.cpp` and `win32compat_linux.cpp` are built, with the
  data tree (`shared/` file register), into the static library `organizer_datatree`
  (so `util_linux.cpp` also keeps `ToLowerCopy(std::wstring_view)`, which is in the
  `_WIN32` block of `shared/util.cpp`: the rest of that file needs the main window);
  the rest of env,
  except `env.cpp` and `env_linux.cpp` (which need the settings), into
  `organizer_env`; `exitcodes_linux.cpp`, `loot_linux.cpp`, `protonlauncher_linux.cpp`,
  `spawn_linux.cpp`, `vfsbridge_linux.cpp`, `vfsclient_linux.cpp` and
  `vfsconnector_linux.cpp` into `organizer_launch`. The application and the unit tests
  (`tests/`, `organizer_tests`) link them.
- `protonsettings_linux.{h,cpp}` is a Linux-only widget of the application (not a
  replacement): the Proton version and the environment of an executable in the
  executables dialog.
- Code shared by both platforms stays in the shared file; `#ifdef _WIN32` is used there
  only for changes of one to three lines, or to wrap whole Win32 functions as above.
