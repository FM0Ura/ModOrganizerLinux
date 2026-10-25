<a id="readme-top"></a>

<div align="center">

<img src="https://capsule-render.vercel.app/api?type=waving&color=gradient&height=200&section=header&text=Mod%20Organizer%202%20for%20Linux&fontSize=44&fontAlignY=35&animation=fadeIn" width="100%" alt="Mod Organizer 2 for Linux"/>

<p><b>The Mod Organizer 2 mod manager as a native Linux AppImage, for Steam games running through Proton.</b></p>

[![License](https://img.shields.io/badge/license-GPL--3.0-blue?style=for-the-badge)](LICENSE)
[![Release](https://img.shields.io/github/v/release/FM0Ura/ModOrganizerLinux?style=for-the-badge&include_prereleases)](https://github.com/FM0Ura/ModOrganizerLinux/releases)
[![Linux CI](https://img.shields.io/github/actions/workflow/status/FM0Ura/ModOrganizerLinux/linux.yml?style=for-the-badge&label=linux%20ci)](https://github.com/FM0Ura/ModOrganizerLinux/actions)
[![Last commit](https://img.shields.io/github/last-commit/FM0Ura/ModOrganizerLinux?style=for-the-badge)](https://github.com/FM0Ura/ModOrganizerLinux/commits)

**English** · [Português](README.pt-BR.md)

[Releases](https://github.com/FM0Ura/ModOrganizerLinux/releases) · [User guide](docs/user/en/install.md) · [Report bug](https://github.com/FM0Ura/ModOrganizerLinux/issues/new)

</div>

<br>

<details>
<summary><b>📋 Table of Contents</b></summary>

- [📖 About](#-about)
- [✨ Features](#-features)
- [🧰 Tech Stack](#-tech-stack)
- [🚀 Getting Started](#-getting-started)
- [🔄 Updating](#-updating)
- [🎮 Working games](#-working-games)
- [🐛 Known issues](#-known-issues)
- [🤝 Contributing](#-contributing)
- [📝 License](#-license)

</details>

## 📖 About

[Mod Organizer 2](https://github.com/ModOrganizer2/modorganizer) is the mod manager that keeps
a game's folder clean by showing the game a virtual, merged view of your mods. This is a
native Linux port: it manages mods for Steam games running through Proton, and it ships as a
single AppImage, so you download it and run it, with no container or build environment.
Bazzite is the reference desktop. The Windows build keeps building from the same source
against the upstream `uibase`.

<!-- TODO: add a screenshot of the main window (docs/assets/main-window.png) -->

## ✨ Features

- 📦 **One file** — an AppImage with its own Qt, QtWebEngine, Python and libraries
- 🔀 **Virtual file system** — mods merged for the game through FUSE, nothing written to the game folder
- 🧩 **Plugins** — the Bethesda games, FOMOD, BAIN and bundle installers, previews, the INI tools, the Python plugins and about 75 community games (`basic_games`)
- 📋 **LOOT** — plugin sorting with libloot
- 🌐 **Nexus Mods** — login kept in the Secret Service (KWallet, GNOME Keyring) and `nxm://` links
- 🧳 **BSA/BA2** — archive creation with the BSA Packer
- 🔄 **Self-updating** — from the GitHub releases, or through Gear Lever

## 🧰 Tech Stack

<p align="center">
  <img src="https://skillicons.dev/icons?i=cpp,cmake,qt,rust,python,linux&theme=dark" alt="C++, CMake, Qt, Rust, Python, Linux" />
</p>

C++ and Qt 6 (the application), Rust (the `mo2-vfs` FUSE helper), Python 3.14 with PyQt6
(plugins), packed as an AppImage on Fedora 44.

## 🚀 Getting Started

### Requirements

- A 64-bit Linux desktop with **glibc 2.43 or newer**
- Steam with Proton, `umu-run` and FUSE 3, which are **not** bundled
  ([host dependencies](docs/user/en/install.md#host-dependencies))

### Tested distributions

Every release is started in a clean container of each of these (up to the main window); the
real game is tested on Bazzite.

| Distribution | glibc |
|---|---|
| Bazzite (reference desktop) | 2.43 |
| Fedora 44 | 2.43 |
| Arch Linux (rolling) | rolling |
| Ubuntu 26.04 LTS | 2.43 |

Ubuntu 24.04 is too old (glibc 2.39).

### Installation

Download `ModOrganizer-<version>-x86_64.AppImage` (about 253 MB) from the
[releases page](https://github.com/FM0Ura/ModOrganizerLinux/releases).

**With Gear Lever** (Bazzite): open the file in Gear Lever, then **Unlock** and **Move to the app menu**.

**Directly:**

```bash
chmod +x ModOrganizer-2.5.2.1-x86_64.AppImage
./ModOrganizer-2.5.2.1-x86_64.AppImage
```

> [!TIP]
> Run the game once from Steam before creating the instance, so Proton creates its prefix.
> Never run Mod Organizer as root.

More in the [installation guide](docs/user/en/install.md) and
[first steps](docs/user/en/first-steps.md).

### Host dependencies

Mod Organizer opens and manages mods without them; **Run** needs them, and Mod Organizer
tells you what is missing.

| Program | For |
|---|---|
| Steam with Proton | the game and its prefix |
| `umu-run` ([umu-launcher](https://github.com/Open-Wine-Components/umu-launcher)) | running Windows programs in the prefix |
| FUSE 3 (`fusermount3`) | the virtual folder of the mods |

Install commands per distribution: [Host dependencies](docs/user/en/install.md#host-dependencies).

## 🔄 Updating

- **Inside Mod Organizer:** with *Check for updates* on, an update button appears when a new
  release exists. It downloads, verifies the SHA-256 against the release's `SHA256SUMS`,
  replaces the file in place and restarts. It does not update while a program started by
  Mod Organizer is running: it asks you to close the programs, and you click Update again.
- **Gear Lever or AppImageUpdate:** the AppImage carries update information, so only the
  changed part is downloaded.

Details: [Updating](docs/user/en/updating.md).

## 🎮 Working games

| Game | Status |
|------|--------|
| Skyrim Special Edition (Steam + Proton) | Working: mods through the virtual file system, SKSE, LOOT and SSEEdit, checked in the manual tests on Bazzite. |
| Cyberpunk 2077 (Steam + Proton) | Working: mods through the virtual file system, checked manually on Bazzite. CET and RED4ext need `WINEDLLOVERRIDES` ([how](docs/user/en/games.md#proxy-dlls-wine-dll-overrides)). |
| NieR:Automata (Steam + Proton) | Working: mods through the virtual file system, checked manually on Bazzite. Every mod goes through the manual installer (the plugin, as upstream, does not recognize mod layouts). |

Every other game is only detected (the Bethesda plugins and the about 75 games of
`basic_games`) and has **not** been validated yet. Tools that run through the
virtual file system: SKSE, LOOT, SSEEdit, and BodySlide, Nemesis and Pandora installed as mods
(the last two have display glitches; [Games and tools](docs/user/en/games.md)).

## 🐛 Known issues

- **Game without keyboard focus on KDE Wayland:** a KWin window rule for `steam_app_.*` fixes it ([how](docs/user/en/troubleshooting.md#the-game-does-not-get-keyboard-focus-kde-wayland)).
- **Baldur's Gate 3** has been tried and has gaps: no load order on Linux yet, and a save whose name has an accent was not written ([details](docs/user/en/troubleshooting.md#baldurs-gate-3)).
- On hosts that are not Fedora-based, the built-in web view does not trust certificate authorities you added to the system store.
- Crash? [Report it with a backtrace](docs/user/en/troubleshooting.md#reporting-a-crash) (`coredumpctl` and the release's debug symbols).

All of it: [Troubleshooting](docs/user/en/troubleshooting.md).

## 🤝 Contributing

Building from source, the CI, how a release is cut and the variables for development and
tests are in [docs/dev/building.md](docs/dev/building.md). The plan of the port is in
[ROADMAP.md](ROADMAP.md) and the current state in [STATE.md](STATE.md) (both in
Portuguese). Open an issue before a large change.

## 📝 License

Distributed under the GNU General Public License v3.0, like upstream Mod Organizer 2. See
[`LICENSE`](LICENSE) for details.

---

<div align="center">

Made by **[FM0Ura](https://github.com/FM0Ura)**. Based on [Mod Organizer 2](https://github.com/ModOrganizer2/modorganizer).

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<img src="https://capsule-render.vercel.app/api?type=waving&color=gradient&height=100&section=footer" width="100%" alt=""/>

</div>
