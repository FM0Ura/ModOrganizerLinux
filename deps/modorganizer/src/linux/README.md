# Linux implementations

This folder holds the Linux implementations of the parts of Mod Organizer that are
Windows-only. It is compiled on Linux only; `src/CMakeLists.txt` removes it from the
Windows build and removes the Windows-only files it replaces from the Linux build.

Rules:

- One `<name>_linux.cpp` per replaced Windows file: `envfs.cpp` is replaced by
  `envfs_linux.cpp`, `spawn.cpp` by `spawn_linux.cpp`, and so on. The header
  (`envfs.h`, `spawn.h`, ...) stays shared in `src/`.
- The Windows file is not moved or rewritten; it is only excluded from the Linux
  build by the list in `src/CMakeLists.txt` (currently `env*.cpp`, `spawn.cpp`,
  `usvfsconnector.cpp`, `shared/util.cpp` and `shared/windows_error.cpp`, plus resource
  scripts and manifests).
- `envfs_linux.cpp` and `util_linux.cpp` are built, with the data tree (`shared/`
  file register), into the static library `organizer_datatree`, which the application
  and the unit tests (`tests/`, `organizer_tests`) link.
- Code shared by both platforms stays in the shared file; `#ifdef _WIN32` is used there
  only for changes of one to three lines.
