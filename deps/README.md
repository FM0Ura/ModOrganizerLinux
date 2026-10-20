# Vendored upstream dependencies

Everything under `deps/` is a copy of an upstream repository, imported with
`git subtree add --squash` and updated with `git subtree pull --squash`.

**Version rule:** every dependency tracks the `master` branch of its upstream
repository, pinned to the `master` HEAD at the time of the last update (the table
below). This is what upstream MO2's own CI does: `ModOrganizer2/build-with-mob-action`
builds every dependency from `master`, and the application fork needs that API (for
example uibase's `IPluginGame::blueprintPrefix()` and bsatk's `std::filesystem::path`
overloads of `BSA::Archive::read/extract/extractAll`, both newer than any release).
The only exception is `dds-header`, which follows the `mo2-dds-header` port of
[`ModOrganizer2/vcpkg-registry`](https://github.com/ModOrganizer2/vcpkg-registry)
(see below). Until Task 7b of the Phase 1 plan the pins were the ports of the registry
baseline `1316300c059f9dc6eddbdd19c57d3415a1779fee`.

| name | repository | ref (commit) | date |
|---|---|---|---|
| `cmake_common` | https://github.com/ModOrganizer2/cmake_common | `master` (`8287612282c77028dedf54151fabfddff47d72bf`) | 2026-06-06 |
| `uibase` | https://github.com/ModOrganizer2/modorganizer-uibase | `master` (`edc09d390348244c2e00d189058140bae5d92934`) | 2026-10-06 |
| `archive` | https://github.com/ModOrganizer2/modorganizer-archive | `master` (`422262be70ade2d145a6e8d74ea59d94609f8c1b`) | 2026-10-06 |
| `bsatk` | https://github.com/ModOrganizer2/modorganizer-bsatk | `master` (`a0005bf41921c130c6e9cd561dc29cba0621d56b`) | 2026-10-06 |
| `esptk` | https://github.com/ModOrganizer2/modorganizer-esptk | `master` (`15b46feb76cf9859ecf18b01422cf960dc6f8261`) | 2026-07-08 |
| `lootcli` | https://github.com/ModOrganizer2/modorganizer-lootcli | `master` (`465d4106951417bf409831cb79289b1413dfbca5`) | 2026-05-06 |
| `dds-header` | https://github.com/microsoft/DirectXTex | tag `jun2024` (`af1c8b3`) | 2024-06 (port `mo2-dds-header`) |
| `game_bethesda` | https://github.com/ModOrganizer2/modorganizer-game_bethesda | `master` (`a6a23403134afe08645aa97d6d376aab9d2ba861`) | 2026-10-06 |
| `installer_quick` | https://github.com/ModOrganizer2/modorganizer-installer_quick | `master` (`1773580d2ecad6f4c857f0a39564883f4b268d49`) | 2026-10-06 |
| `installer_manual` | https://github.com/ModOrganizer2/modorganizer-installer_manual | `master` (`3ff03327656662a5ce3a7e9fb6b44952d1bb379f`) | 2025-05-29 |
| `installer_fomod` | https://github.com/ModOrganizer2/modorganizer-installer_fomod | `master` (`7c3995682134a63b5040b7865b88a3749b056d68`) | 2026-10-06 |
| `installer_bain` | https://github.com/ModOrganizer2/modorganizer-installer_bain | `master` (`0e60c19b2d64eef37d299754ff3c29497eccc478`) | 2026-10-06 |
| `installer_bundle` | https://github.com/ModOrganizer2/modorganizer-installer_bundle | `master` (`1d7f9dc09a3283f8d59b7d8669a7018ed7840fe0`) | 2025-05-29 |
| `preview_base` | https://github.com/ModOrganizer2/modorganizer-preview_base | `master` (`a8b2c5137d6b011275ea6f113a930b3481331cde`) | 2026-10-06 |
| `preview_bsa` | https://github.com/ModOrganizer2/modorganizer-preview_bsa | `master` (`fb477c0850de24cebfb2922c437a97fbd014e1e3`) | 2026-10-06 |
| `modorganizer` | https://github.com/ModOrganizer2/modorganizer | `master` (`4ba22f0d8b6f9e08e44008b8395cd774551b4ebe`) | 2026-10-05 |

The date is the upstream commit date. `git log -1 --grep 'git-subtree-dir: deps/<name>$'`
shows the same commit in the `git-subtree-split:` line of the latest squash commit.

## Notes on individual pins

- **`modorganizer`** is the application itself and carries most of the port (Linux
  `env` layer, `VfsConnector`, CMake preset `linux-gcc`), so it is the exception to the
  "small changes" rule below. Until 2026-10-15 it lived in a separate fork,
  [`FM0Ura/modorganizer`](https://github.com/FM0Ura/modorganizer) (branch
  `claude/serene-maxwell-1dyrbg`, `bb46b18`); its 11 commits on top of upstream
  `4ba22f0` were replayed here one by one, with the `deps/modorganizer:` prefix. To send
  a change upstream, extract it with `git subtree split --prefix deps/modorganizer`.

- **`dds-header`**: the vcpkg port `mo2-dds-header` downloads one file,
  `DirectXTex/DDS.h`, from the `jun2024` tag of Microsoft's DirectXTex. There is no
  repository dedicated to it, so the whole DirectXTex tag is imported (about 4 MB).
  Only `deps/dds-header/DirectXTex/DDS.h` is used: `./build.sh --only dds-header`
  installs it like the port does (`include/DDS/DDS.h` plus the config exporting
  `mo2::dds-header`, from `build/dds-header/`), together with a `dxgiformat.h`
  replacing the Windows SDK header that DDS.h users expect. The header is
  byte-identical to the file the port downloads (SHA-512 `49ED2C45...`, checked by
  the build). Upstream's
  `.gitattributes` forces CRLF for `*.h` in the working tree; the committed blob is LF.
  DirectXTex's own `CMakeLists.txt` is not meant to be built.
- **`cmake_common`** has no `CMakeLists.txt`: it only ships CMake modules
  (`mo2.cmake`, `mo2-cmake-config.cmake`, ...) that are consumed through
  `find_package(mo2-cmake)` / `include()`.
- Dependencies with a `vcpkg.json` (Windows-oriented) are not built through vcpkg on
  Linux; the native build takes system packages instead.

## Rules for changes inside `deps/`

- Changes to the contents of a subtree live in their own commits, with the subject
  prefixed by `deps/<name>:` (for example `deps/uibase: add win32compat.h`), and never
  mix files outside that directory.
- Keep such changes small and additive, and prefer Linux-only files (`*_linux.cpp`,
  `win32compat.h`) so the code keeps building on Windows against upstream.
- To update a dependency, run `scripts/update-dep.sh <name> master` (a wrapper around
  `git subtree pull --prefix deps/<name> <url> <ref> --squash`; the working tree must be
  clean). Resolve conflicts by keeping upstream's code and re-applying the Linux change
  on top of it, in the merge commit or in a follow-up `deps/<name>:` commit, then update
  the table above. Check that the Windows view of every file we changed still equals
  upstream: `unifdef -t -D_WIN32 -DWIN32` of our file and of upstream's must match.
