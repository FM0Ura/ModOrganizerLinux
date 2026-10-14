# ModOrganizerLinux

Superproject for a native Linux port of [Mod Organizer 2](https://github.com/ModOrganizer2/modorganizer).
The goal is a Mod Organizer 2 that runs natively on Linux (Bazzite is the reference
platform) and manages mods for Steam games running through Proton, while the application
fork (`modorganizer`) keeps building on Windows against the upstream `uibase`.

This repository holds the build scripts, CI, documentation and the upstream dependencies
(vendored under [`deps/`](deps/README.md) with `git subtree`).

- Design specification: [docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md](docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md)
- Vendored dependencies and their pinned versions: [deps/README.md](deps/README.md)
- Updating a dependency: `scripts/update-dep.sh <name> <ref>`

## Building

The build environment is defined once, in [`build/Containerfile`](build/Containerfile)
(Fedora 44: GCC, CMake, Ninja, Qt 6 incl. WebEngine/NetworkAuth/WebSockets, Boost, spdlog,
fmt, lz4, zlib, gtest, fuse3, clang-tools-extra, Rust stable). CI uses the same file. If
`fedora:44` is ever unavailable, use the latest stable Fedora tag and note it here.

`build.sh` expects the application fork checked out next to this repository
(`../modorganizer`, override with `MO2_SRC`). It builds into `build-out/` and installs into
`install/`:

```sh
./build.sh                          # all implemented targets, in dependency order
./build.sh --only uibase --tests    # one or more targets (repeatable), with ctest
./build.sh --list                   # targets in build order
```

Targets not ported yet are skipped by the default run and rejected with `--only`.

### With podman or docker

```sh
podman build -t mo2-linux-build -f build/Containerfile build/
podman run --rm -v "$PWD/..:/work:Z" -w /work/ModOrganizerLinux mo2-linux-build ./build.sh --tests
```

### With distrobox (Bazzite)

```sh
podman build -t mo2-linux-build -f build/Containerfile build/
distrobox create --name mo2-build --image localhost/mo2-linux-build
distrobox enter mo2-build -- ./build.sh --tests
```

### Local environment

No container is required: any environment that provides the dependencies listed in the
Containerfile works. `build.sh` respects `CC`, `CXX` and `CMAKE_PREFIX_PATH` from the
environment (it prepends `install/` to `CMAKE_PREFIX_PATH`).
