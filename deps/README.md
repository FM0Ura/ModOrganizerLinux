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
| `bsa_extractor` | https://github.com/ModOrganizer2/modorganizer-bsa_extractor | `master` (`d6b32b9d9d894bb71734843d62528748b4f06f03`) | 2026-10-06 |
| `diagnose_basic` | https://github.com/ModOrganizer2/modorganizer-diagnose_basic | `master` (`558e3583bd97905126c88bb1a3b3219def3bd394`) | 2026-10-06 |
| `check_fnis` | https://github.com/ModOrganizer2/modorganizer-check_fnis | `master` (`53522c3157607bd1c8d6a9639b3bbda9795f27fe`) | 2026-10-06 |
| `tool_inieditor` | https://github.com/ModOrganizer2/modorganizer-tool_inieditor | `master` (`18b0e1c5869c1f6b6c129b7a3758142b582fea8c`) | 2026-10-06 |
| `tool_inibakery` | https://github.com/ModOrganizer2/modorganizer-tool_inibakery | `master` (`355cac9619175016d9cee45589ecb01bba408a07`) | 2026-10-06 |
| `plugin_python` | https://github.com/ModOrganizer2/modorganizer-plugin_python | `master` (`baf09209f51566a39c6247ed93999ae54a6046df`) | 2026-10-06 |
| `preview_dds` | https://github.com/ModOrganizer2/modorganizer-preview_dds | `master` (`5cfbf5dd38829731c0aa2e472747e4c4fa20f679`) | 2026-05-21 |
| `form43_checker` | https://github.com/ModOrganizer2/modorganizer-form43_checker | `master` (`70709437486cccbd8fd03da403ad4f5dc5bf3441`) | 2026-05-21 |
| `script_extender_plugin_checker` | https://github.com/ModOrganizer2/modorganizer-script_extender_plugin_checker | `master` (`975e95db3602fcfd747bd3c9e9620f12b7fd9b78`) | 2026-05-21 |
| `tool_configurator` | https://github.com/ModOrganizer2/modorganizer-tool_configurator | `master` (`22b6243ea47b4e28ee788d3b184db42489c7d4dc`) | 2026-05-21 |
| `installer_wizard` | https://github.com/ModOrganizer2/modorganizer-installer_wizard | `master` (`d38019a994a42609376975227201531007cba9ae`) | 2025-05-29 |
| `fnistool` | https://github.com/ModOrganizer2/modorganizer-fnistool | `master` (`6cd125da7494450f77dfbafb61a8294088f9a8fb`) | 2026-06-03 |
| `basic_games` | https://github.com/ModOrganizer2/modorganizer-basic_games | `master` (`f1207d6398fbb4adda281f2bd5c563105391baaf`) | 2026-10-05 |
| `bsapacker` | https://github.com/ModOrganizer2/modorganizer-bsapacker | `master` (`de1d783fbbec6b58f87cbd99b40b7ac89a78adf1`) | 2025-05-29 |
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
- **`check_fnis`** and **`game_bethesda`** carry fixes that are not upstream, besides
  their Linux patches: `check_fnis` passes the exit code pointer to
  `waitForApplication` (`9ec0e72`; upstream passes it as `refresh`); `game_bethesda`
  finds the INIs of the Proton prefix ignoring case, restores empty profile INIs and
  maps the local saves to `__MO_Saves` (`62fe2cf`, `9d6b312`). A `git subtree pull`
  of either must keep them (check the conflicts and the tests of `game_bethesda`).
- **`plugin_python`** is built with the system's Python 3.14, pybind11 and PyQt6 (the
  Fedora packages, never pip) and needs `sip-module` (package `sip6`) to generate
  `sip.h`. Besides its Linux branches, it carries fixes for pybind11 3 and for the
  imported configuration mapping of mo2-cmake (see the `deps/plugin_python:` commits);
  a `git subtree pull` must keep them. On Linux the proxy is not linked to the runner:
  it loads `libpython` and `plugin_python/dlls/librunner.so` with `dlopen` (the runner
  exports `mo2_create_python_runner`), so that a missing Python is reported by the
  plugin; `build.sh --tests` checks that neither is `NEEDED` by `plugin_python.so`.
  On Linux only (`#ifndef _WIN32`), `mobase.utils.steamApp(app_id)` exposes
  `MOBase::steam::findApp()` of uibase to Python (a dict with `appId`, `root`, `library`,
  `gamePath` and `compatData`, or `None` when the app is not installed); `basic_games` is
  the user. It sits in `src/mobase/wrappers/utils.cpp`, with its pytest in
  `tests/python/test_steam.py` (`deps/plugin_python: Linux: mobase.utils.steamApp`).
  On Linux only, `src/libs/winreg.py` is installed in `plugin_python/libs`: an always
  empty registry (opening or querying a key raises `FileNotFoundError`, writing raises
  `PermissionError`), so that plugins written for Windows that import `winreg` at the
  top of a module (e.g., RootBuilder) load; its pytest is `tests/python/test_winreg.py`
  (`deps/plugin_python: Linux: an empty winreg`).
- **`fnistool`**, **`installer_wizard`**, **`script_extender_plugin_checker`** and
  **`tool_configurator`** (Python plugins) carry Linux fixes that are not upstream, each
  in `if sys.platform == "win32": ... else: ...`: `fnistool` passes `RedirectFiles` as a
  `Z:` path; `installer_wizard` opens the wizard script with the desktop and converts the
  backslashes of the wizard paths; `script_extender_plugin_checker` converts the Windows
  paths of the script extender log (drive `Z:`, `/home` against `/var/home`, case);
  `tool_configurator` finds the INI files ignoring case. **`cmake_common`** also carries one:
  the success file of the pip requirements (`mo2_python.cmake`) is touched with
  `cmake -E touch` on Linux. A `git subtree pull` of any of them must keep these (see the
  `deps/<name>: Linux:` commits).
- **`basic_games`** (Python meta-plugin of about 75 game modules that are not Bethesda's, with
  its pip requirements installed in `lib/`) carries Linux fixes that are not upstream:
  the stores are only asked for what Linux has. `steam_utils.find_games()` returns a lazy
  mapping of app ID to game folder whose queries call `mobase.utils.steamApp` (it cannot
  be listed, so `items()` is empty), `find_steam_path()` returns `None`; `gog_utils`,
  `epic_utils`, `origin_utils` and `eadesktop_utils` return `{}` and log once that only
  Steam games are detected; `winreg` is imported only on Windows (also in
  `games/game_oblivion_remaster.py`, whose `getLootPath()` returns `None`); and, on
  Linux only, `createPlugins()` skips a game module that failed to import instead of
  registering the classes of the previous one again, and `BasicGame.setGamePath()`
  records which of the game's Steam app IDs is installed in the given path (the Windows
  code iterates the list of Steam games, which Linux cannot produce). The new
  `proton_utils.py` resolves `%USERPROFILE%`, `%DOCUMENTS%` and the default documents
  folder (`replace_variables()` and `_default_documents_directory()` in `basic_game.py`)
  into the Proton prefix of the game's Steam app ID (`<compatData>/pfx/drive_c/users/steamuser`),
  falling back to the XDG folders with a one-time warning when a Steam game has no
  prefix. In `games/baldursgate3`, `LSLibRetriever.download_lslib_if_missing()` (the one
  gate of the load-order generation, the mod-installed hook and the "Check For LsLib
  Updates" tool) returns `False` on Linux before any download or process, because LSLib's
  `Divine` rejects every Unix path; `BG3Utils.warn_load_order_unavailable()` logs the
  reason and tells the user once per session (the explicit "Check For LsLib Updates" and
  "Reparse Pak Metadata" tools force it every time). In `games/game_cyberpunk2077.py`, the
  `-modlist=...` argument of `redMod.exe` (run through Proton, and MO2 does not convert
  the contents of arguments) is a `Z:\...` path (`windows_path()`). The Windows
  behaviour is the upstream one, but not every patch sits behind a platform branch. Most
  run behind `if sys.platform == "win32": ... else: ...` or `if sys.platform != "win32":`
  (`setGamePath()`, `download_lslib_if_missing()`, the BG3 tools, `windows_path()` for
  the REDmod argument, the stores' `find_games()`). These are restructured or added
  without a branch, with the same result on Windows: `replace_variables()` and
  `_default_documents_directory()` in `basic_game.py` (they now compute the folder once,
  around a Linux-only lookup that returns `None` on Windows); the store modules, which
  import `functools`/`qInfo` and define `_only_steam_on_linux` unconditionally; BG3's
  `warn_load_order_unavailable()` and `_load_order_warned`; and the guarded `continue` of
  `createPlugins()`, which sits inside the existing `except` clauses. A
  `git subtree pull` that touches these upstream functions will conflict, and must keep
  the patches (see the `deps/basic_games: Linux:` commits); `./build.sh --only basic_games
  --tests` runs `tests/basic-games` against the installed package.
  `games/game_stellarblade.py` (Stellar Blade, Phase 6a) is a file of ours, not from
  upstream: a `git subtree pull` neither brings nor touches it, and it has no `sys.platform`
  branch.
- **`bsapacker`** (the BSA Packer tool, Phase 4e) carries Linux patches that are not
  upstream, all inside `deps/bsapacker` (spec of Phase 4e, section 3; the subtree itself is
  byte-identical to upstream `de1d783`). On Windows it keeps using `libbsarch`; on Linux:
  - `linux/include/libbsarch/` (new): replacements of `libbsarch.h` and
    `bs_archive_auto.hpp` with the names the sources use; `linux/bs_archive_auto.cpp` (new)
    writes the archives with libbsarchpp (`build/libbsarchpp`, 1.0.7, static), through its
    7-argument constructor only and on a single thread. Not taken from Kaedras's fork (its
    wrapper never adds the files). A write failure logs `failed to write archive <path>:
    <message>` (warning) and removes the partial file (only if the file did not exist before
    or writing had started); packing with nothing to pack logs only at debug level
    (`no files to pack into <path>`), creates nothing and keeps an existing archive of that
    name. The zlib archive types (TES4, FO3, FO4, FO4dds, SF) get compression level 6,
    because libbsarchpp's default level 0 writes stored blocks. Git checks these new
    `linux/` files out with CRLF (the subtree's `.gitattributes`): harmless.
  - `src/CMakeLists.txt`: the current Windows lines under `if (WIN32)`; on Linux,
    libbsarchpp, `linux/`, no `BSArchive*.cpp`, and `CXX_VISIBILITY_PRESET default`
    (`cmake_common` hides symbols, and only some upstream headers use `BSAPACKER_EXPORT`;
    the test executable needs them to link).
  - `CMakeLists.txt`: `linux/tests` (Linux only, with `BUILD_TESTING`; round trips of every
    archive format, read back with libbsarchpp and, where it reads them, `bsatk`).
  - `src/TextureArchiveBuilder.cpp`: `DirectXTex.h` and the body of `DDSCallback` under
    `#ifdef _WIN32` (libbsarchpp reads the DDS headers).
  - Test fixes that apply on every platform (`deps/bsapacker: tests:`):
    `tests/MockOrganizer.h` follows the current `IOrganizer` (the upstream mock did not
    compile against `deps/uibase`) and `tests/ModDtoFactoryFacts.cpp` (the New Vegas test
    names its archive `plugin.esp`; upstream it fails on every platform).

  libbsarchpp is not a subtree: `build/libbsarchpp/build-libbsarchpp.sh` (target
  `libbsarchpp` of `build.sh`) downloads libbsarchpp 1.0.7 (`211a681`,
  https://github.com/Kaedras/libbsarchpp) and boost-ext/di v1.3.2 (`20b6454`,
  https://github.com/boost-ext/di, the header `boost/di.hpp` only) at pinned commits into
  `build-out/libbsarchpp`. It builds the library static and PIC with its own
  `config.cmake.in`: the package config libbsarchpp installs calls `find_dependency`
  without including its macro and redefines `lz4::lz4`. Defects found in 1.0.7 and worked
  around: the 4-argument create constructor leaves the object uninitialized (only the
  7-argument one is used); the zlib compression level starts at 0 (see above);
  `extractFileData` unlocks a lock that is held only when multithreaded (EPERM when reading
  FO4dds/SFdds single-threaded; reading only, MO2 reads archives with `bsatk`). Its own
  `Fast.*` tests are commented out upstream, and the `ctest` run excludes gtest's
  uninstantiated-suite self-check (the per-game suites need game data). At run time
  `libbsa_packer.so` needs `libtbb`, `liblz4`, `libz` and Qt6Concurrent of the box's system
  Qt. A `git subtree pull` of `bsapacker` conflicts only where these files changed upstream.
- **AppImage tools** (Phase 5) are not subtrees either: `build/appimage/build-appimage.sh`
  downloads them into `build-out/appimage-tools`, checks the SHA-256 pinned at the top of
  the script and extracts them (no FUSE needed, e.g. in a container):

  | tool | version | file | SHA-256 |
  |---|---|---|---|
  | [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) | `1-alpha-20251107-1` | `linuxdeploy-x86_64.AppImage` | `c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d` |
  | [linuxdeploy-plugin-qt](https://github.com/linuxdeploy/linuxdeploy-plugin-qt) | `1-alpha-20250213-1` | `linuxdeploy-plugin-qt-x86_64.AppImage` | `15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724` |
  | [appimagetool](https://github.com/AppImage/appimagetool) | `1.9.1` | `appimagetool-x86_64.AppImage` | `ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0` |
  | [type2-runtime](https://github.com/AppImage/type2-runtime) (static, no libfuse2) | `20251108` | `runtime-x86_64` | `2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d` |

  The release tags are the newest non-`continuous` ones on 2026-10-24 (the `continuous`
  assets change in place); the hashes match the digests GitHub reports for the assets,
  except linuxdeploy-plugin-qt's, whose release has none. linuxdeploy runs with the
  container's `patchelf` (0.18, `PATCHELF`): the 0.15 inside linuxdeploy rewrites some
  libraries of Fedora 44 into ones that crash when loaded (`libcrypt.so.2`).
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
