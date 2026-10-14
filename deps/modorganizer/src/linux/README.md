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
  `usvfsconnector.cpp`, `shared/util.cpp` and `shared/windows_error.cpp`, plus resource
  scripts and manifests) and the Linux file replaces all of it, or, for files that mix
  shared and Win32 code, it is built on both platforms with its Win32 functions in
  `#ifdef _WIN32` blocks and the Linux file only implements those functions (the
  other `env*.cpp`: `env.cpp`, `envmetrics.cpp`, `envmodule.cpp`, `envsecurity.cpp`,
  `envshell.cpp`, `envshortcut.cpp`, `envwindows.cpp`; `envdump_linux.cpp` implements
  the crash dumps of `envdump.h`, which are in `env.cpp` on Windows).
- `envfs_linux.cpp` and `util_linux.cpp` are built, with the data tree (`shared/`
  file register), into the static library `organizer_datatree`; the rest of env,
  except `env.cpp` and `env_linux.cpp` (which need the settings), into
  `organizer_env`. The application and the unit tests (`tests/`, `organizer_tests`)
  link both.
- Code shared by both platforms stays in the shared file; `#ifdef _WIN32` is used there
  only for changes of one to three lines, or to wrap whole Win32 functions as above.
