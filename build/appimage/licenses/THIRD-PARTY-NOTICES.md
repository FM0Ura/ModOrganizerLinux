# Mod Organizer 2 for Linux: licences and third-party notices

This AppImage contains Mod Organizer 2, ported to Linux, and the libraries it needs.
This folder (`usr/share/licenses/ModOrganizer/` in the AppImage) has the licence of
each part. The licence texts of the libraries that come from Fedora 44 packages are in
`fedora/<package>/`, and `fedora-packages.txt` lists every such package with its
version and licence, as built.

## Mod Organizer 2 and its plugins

Mod Organizer 2 (https://github.com/ModOrganizer2/modorganizer) and its plugins
(uibase, archive, bsatk, esptk, lootcli, the game, installer, preview, diagnose and tool
plugins, plugin_python, basic_games, bsapacker, ...) are licensed under the GNU General
Public License, version 3 (`ModOrganizer-LICENSE.txt`). The Linux port
(https://github.com/FM0Ura/ModOrganizerLinux) is distributed under the same licence.

## Libraries built from source

| component | licence | text |
|---|---|---|
| libloot 0.29.5 (https://github.com/loot/libloot), with the Rust crates it links statically (see its `Cargo.lock`) | GPL-3.0; the crates are under MIT, Apache-2.0 or similar permissive licences | `libloot-LICENSE.txt` |
| libbsarchpp 1.0.7 (https://github.com/Kaedras/libbsarchpp), linked into the BSA Packer plugin | GPL-3.0 | `libbsarchpp-LICENSE.txt` |
| boost-ext/di 1.3.2 (header, built into libbsarchpp) | BSL-1.0 | `fedora/boost-devel/` (same licence) |
| 7-Zip (`7z.so`, https://www.7-zip.org) | LGPL-2.1-or-later, with the unRAR restriction for the RAR code | `7zip/License.txt`, `7zip/unRarLicense.txt` |
| DirectXTex `DDS.h`, tag jun2024 (https://github.com/microsoft/DirectXTex) | MIT | `DirectXTex-LICENSE.txt` |
| `mo2-vfs` (the FUSE helper, this port) and the Rust crates it links statically | GPL-3.0; the crates are under MIT or Apache-2.0 | `ModOrganizer-LICENSE.txt` |

### Provenance of `dxgiformat.h`

`dxgiformat.h` (`build/dds-header/dxgiformat.h` in the repository of the port, installed
next to `DDS.h` and compiled into bsatk and the plugins that read DDS textures) was
written for this port (commit `8f214c2f`, 2026-10-14). On Windows the header comes from
the Windows SDK; on Linux the port provides only the `DXGI_FORMAT` enumeration that
`DDS.h` and bsatk use: the names and numeric values of the formats as documented by
Microsoft (https://learn.microsoft.com/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format).
These values are part of the DDS and BA2 file formats. The file was not copied from the
Windows SDK, Wine or mingw-w64; it is distributed under the licence of the port
(GPL-3.0).

## Libraries from Fedora 44

The Qt, Python and system libraries of the AppImage are the binaries of Fedora 44
packages (`fedora-packages.txt`, texts in `fedora/`). The main ones:

| component | licence |
|---|---|
| Qt 6 (qtbase, qtdeclarative, qtwayland, qtsvg, qtwebsockets, qtnetworkauth, qtwebchannel, qtpositioning, ...) | LGPL-3.0-only (or GPL-2.0/GPL-3.0) |
| Qt WebEngine, with Chromium and its third-party code | LGPL-3.0 and the licences of Chromium's components: `fedora/qt6-qtwebengine/LICENSE.Chromium` |
| Python 3.14 (libpython and the standard library) | the PSF licence, Python-2.0.1 (`fedora/python3-libs/LICENSE.txt`) |
| PyQt6 | GPL-3.0-only (`fedora/python3-pyqt6-base/LICENSE`) |
| boost | BSL-1.0 |
| fmt, spdlog, gsl-lite, toml++ | MIT |
| lz4 | BSD-2-Clause |
| zlib | Zlib |
| oneTBB | Apache-2.0 |
| OpenSSL | Apache-2.0 |
| curl | curl |
| GLib, libsecret | LGPL-2.1-or-later |
| libstdc++, libgcc_s | GPL-3.0-or-later with the GCC Runtime Library Exception |

The source code of the Fedora packages is available from Fedora
(https://src.fedoraproject.org), at the versions listed in `fedora-packages.txt`.
