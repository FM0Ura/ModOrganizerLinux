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

Build instructions are added together with the build system.
