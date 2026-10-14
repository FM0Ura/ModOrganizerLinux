# Vendored upstream dependencies

Everything under `deps/` is a copy of an upstream repository, imported with
`git subtree add --squash`. The versions match the ones pinned by the
[`ModOrganizer2/vcpkg-registry`](https://github.com/ModOrganizer2/vcpkg-registry) at
baseline `1316300c059f9dc6eddbdd19c57d3415a1779fee` (the Windows build of MO2 consumes
the same versions through vcpkg). The four plugins are not in the registry; their pins
are explained below.

| name | repository | ref | version |
|---|---|---|---|
| `cmake_common` | https://github.com/ModOrganizer2/cmake_common | `v2.5.3-beta.2` (`e7c4285`) | 2.5.3-beta.2 (port `mo2-cmake`) |
| `uibase` | https://github.com/ModOrganizer2/modorganizer-uibase | `v2.5.3-beta.2.1` (`bf780a6`) | 2.5.3-beta.2.1 (port `mo2-uibase`) |
| `archive` | https://github.com/ModOrganizer2/modorganizer-archive | `2.0.0` (`0571bf2`) | 2.0.0 (port `mo2-archive`) |
| `bsatk` | https://github.com/ModOrganizer2/modorganizer-bsatk | `1.4.1` (`c42a252`) | 1.4.1 (port `mo2-bsatk`) |
| `esptk` | https://github.com/ModOrganizer2/modorganizer-esptk | `1.4.0` (`87e0ec8`) | 1.4.0 (port `mo2-esptk`) |
| `lootcli` | https://github.com/ModOrganizer2/modorganizer-lootcli | `1.7.0` (`e9cbec7`) | 1.7.0 (port `mo2-lootcli-header`) |
| `dds-header` | https://github.com/microsoft/DirectXTex | `jun2024` (`af1c8b3`) | jun2024 (port `mo2-dds-header`) |
| `game_bethesda` | https://github.com/ModOrganizer2/modorganizer-game_bethesda | `c2b2597` | unversioned, 2026-04-15 |
| `installer_quick` | https://github.com/ModOrganizer2/modorganizer-installer_quick | `1773580` | unversioned, 2026-10-06 |
| `installer_manual` | https://github.com/ModOrganizer2/modorganizer-installer_manual | `3ff0332` | unversioned, 2025-05-29 |
| `installer_fomod` | https://github.com/ModOrganizer2/modorganizer-installer_fomod | `7c39956` | unversioned, 2026-10-06 |

Full hashes for the plugin pins: `c2b25978e094513701e7f1d94b28c192f5bc849a`,
`1773580d2ecad6f4c857f0a39564883f4b268d49`, `3ff03327656662a5ce3a7e9fb6b44952d1bb379f`,
`7c3995682134a63b5040b7865b88a3749b056d68`.

## Notes on individual pins

- **`dds-header`**: the vcpkg port `mo2-dds-header` downloads one file,
  `DirectXTex/DDS.h`, from the `jun2024` tag of Microsoft's DirectXTex. There is no
  repository dedicated to it, so the whole DirectXTex tag is imported (about 4 MB).
  Only `deps/dds-header/DirectXTex/DDS.h` is used; consumers must add
  `deps/dds-header/DirectXTex` to their include path (the registry's
  `mo2::dds-header` target points at the directory holding `DDS.h`). The header is
  byte-identical to the file the port downloads (SHA-512 `49ED2C45...`). Upstream's
  `.gitattributes` forces CRLF for `*.h` in the working tree; the committed blob is LF.
  DirectXTex's own `CMakeLists.txt` is not meant to be built.
- **`cmake_common`** has no `CMakeLists.txt`: it only ships CMake modules
  (`mo2.cmake`, `mo2-cmake-config.cmake`, ...) that are consumed through
  `find_package(mo2-cmake)` / `include()`.
- **`game_bethesda`**: pinned to `c2b2597` ("Fix loot detection", 2026-04-15), not to the
  newest commit of `master` (`a6a2340`). The very next commit, `c06ec48` ("Starfield:
  Updates for blueprint plugins"), makes `GameStarfield` override and call
  `IPluginGame::blueprintPrefix()`, which was added to uibase in `61451bd`, after the
  pinned uibase tag `v2.5.3-beta.2.1`. `c2b2597` is the newest commit that does not use
  any uibase API newer than the pinned tag (checked for `blueprintPrefix`,
  `instanceName`, `executablesList`, `instanceManager`, `profileNames`, `getProfile`).
  It can be moved forward when `uibase` is bumped to a release containing `61451bd`.
- **`installer_quick`, `installer_manual`, `installer_fomod`**: pinned to the newest
  commit of the default branch. None of them references uibase API newer than the pinned
  tag (same symbol check as above, plus `IExecutable`, `IInstance`, `NXMUrl`); building
  them against the pinned uibase is verified by the build system tasks, not here.
- Dependencies with a `vcpkg.json` (Windows-oriented) are not built through vcpkg on
  Linux; the native build takes system packages instead.

## Rules for changes inside `deps/`

- Changes to the contents of a subtree live in their own commits, with the subject
  prefixed by `deps/<name>:` (for example `deps/uibase: add win32compat.h`), and never
  mix files outside that directory.
- Keep such changes small and additive, and prefer Linux-only files (`*_linux.cpp`,
  `win32compat.h`) so the code keeps building on Windows against upstream.
- To move a dependency to a new upstream ref, run `scripts/update-dep.sh <name> <ref>`
  (a wrapper around `git subtree pull --prefix deps/<name> <url> <ref> --squash`) and
  update the table above in the same change.
