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
  and `loot.cpp`, whose `processrunner_linux.cpp` and `loot_linux.cpp` disable
  starting programs and LOOT until Phase 3; `loot_linux.h` declares the Linux
  `AsyncPipe` class, defined in `loot.cpp` on Windows; `shared/util.cpp` →
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
  `usvfsconnector.cpp` on Windows and by `vfsconnector_linux.cpp` (`NullVfsConnector`)
  on Linux.
- `envfs_linux.cpp`, `util_linux.cpp` and `win32compat_linux.cpp` are built, with the
  data tree (`shared/` file register), into the static library `organizer_datatree`
  (so `util_linux.cpp` also keeps `ToLowerCopy(std::wstring_view)`, which is in the
  `_WIN32` block of `shared/util.cpp`: the rest of that file needs the main window);
  the rest of env,
  except `env.cpp` and `env_linux.cpp` (which need the settings), into
  `organizer_env`; `loot_linux.cpp`, `spawn_linux.cpp` and `vfsconnector_linux.cpp`
  into `organizer_launch`. The application and the unit tests (`tests/`,
  `organizer_tests`) link them.
- Code shared by both platforms stays in the shared file; `#ifdef _WIN32` is used there
  only for changes of one to three lines, or to wrap whole Win32 functions as above.
