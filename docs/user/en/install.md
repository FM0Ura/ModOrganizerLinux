# Installing Mod Organizer 2 for Linux

[Português](../pt-BR/install.md) · **English** · Next: [First steps](first-steps.md)

Mod Organizer 2 for Linux is one file, an AppImage. Download it, make it executable and
run it, or let Gear Lever manage it. It carries its own Qt, QtWebEngine, Python and
libraries, so you do not need a development environment or a distrobox.

## What you need

- A 64-bit (x86_64) Linux desktop with a graphical session (Wayland or X11) and **glibc
  2.43 or newer**. Check with `ldd --version`. The AppImage is built on Fedora 44, so
  older distributions are not supported (Ubuntu 24.04 has glibc 2.39 and is too old).
- Steam with the Proton of the game you want to mod. Mod Organizer manages mods of Steam
  games that run through Proton; other stores are not supported.
- The programs listed under [Host dependencies](#host-dependencies).

### Tested distributions

Every release is started in a clean container of each of these before it is published
(up to the main window, headless; the real game is only tested on Bazzite):

| Distribution | glibc | Notes |
|---|---|---|
| Bazzite (Fedora 44 based) | 2.43 | The reference desktop; games are tested here |
| `fedora:44` | 2.43 | Container smoke test |
| `archlinux:latest` | rolling | Container smoke test |
| `ubuntu:26.04` (LTS) | 2.43 | Container smoke test; 24.04 is too old |

Others with glibc 2.43 or newer should work but are not tested.

## Download

Get the latest `ModOrganizer-<version>-x86_64.AppImage` (about 253 MB) from the
[releases page](https://github.com/FM0Ura/ModOrganizerLinux/releases). The version has
four numbers, such as `2.5.2.1`: the upstream Mod Organizer version plus the number of
the port. The release also has `SHA256SUMS`; to check the download:

```sh
sha256sum -c SHA256SUMS --ignore-missing
```

## Run it

### With Gear Lever (Bazzite)

Open the downloaded file with [Gear Lever](https://github.com/mijorus/gearlever)
(Bazzite has it installed) click **Unlock**, then **Move to the app menu**: it moves the AppImage to your
applications folder and adds a menu entry. Gear Lever can also update it later (see
[Updating](updating.md)).

### Directly

```sh
mkdir -p ~/Applications
mv ~/Downloads/ModOrganizer-2.5.2.1-x86_64.AppImage ~/Applications/ModOrganizer.AppImage
chmod +x ~/Applications/ModOrganizer.AppImage
~/Applications/ModOrganizer.AppImage
```

Keep the file in a folder you can write to (your home): the built-in updater replaces it
in place. Never run Mod Organizer as root or with `sudo`.

If the AppImage does not start because FUSE is missing, install FUSE 3 (below) or run it
extracted: `./ModOrganizer.AppImage --appimage-extract-and-run`.

## Host dependencies

These programs are **not** in the AppImage. Mod Organizer starts and manages mods
without them; only **Run** needs them, and Mod Organizer checks for them before it runs
a program and tells you what is missing and links here.

| Program | What it is for |
|---|---|
| Steam, with Proton | The game and the Proton prefix it runs in. Run the game once from Steam so the prefix exists |
| `umu-run` ([umu-launcher](https://github.com/Open-Wine-Components/umu-launcher)) | Runs the Windows programs (game, SKSE, tools) in the Proton prefix |
| FUSE 3 (`fusermount3`) | Mounts the virtual folder that shows the game the merged mods |

Check what you have:

```sh
command -v umu-run fusermount3 steam
```

Install them with your distribution's package manager. Bazzite already has Steam and
`umu-run`.

| Distribution | FUSE 3 | umu-launcher |
|---|---|---|
| Fedora | `sudo dnf install fuse3` | `sudo dnf install umu-launcher` |
| Arch | `sudo pacman -S fuse3` | `umu-launcher` (official repository or AUR) |
| Ubuntu | `sudo apt install fuse3` | no package in the Ubuntu archive: use the `.deb`/zipapp from the [umu-launcher releases](https://github.com/Open-Wine-Components/umu-launcher/releases) |

### Graphics and system libraries

The AppImage takes the graphics stack from your system, so the drivers match your GPU. A
normal desktop installation already has all of it. If a minimal installation fails to
start, these are the packages the container tests install (everything else is inside the
AppImage):

- **Fedora:** `mesa-libGL mesa-libEGL libglvnd-opengl libglvnd-glx fontconfig freetype libxkbcommon libxkbcommon-x11 libX11 libX11-xcb libxcb dbus-libs libwayland-client libwayland-cursor libwayland-egl mesa-libgbm libdrm vulkan-loader dejavu-sans-fonts ca-certificates findutils`
- **Arch:** `mesa libglvnd fontconfig freetype2 libxkbcommon libxkbcommon-x11 libx11 libxcb dbus wayland libdrm vulkan-icd-loader ttf-dejavu ca-certificates findutils`
- **Ubuntu:** `ca-certificates libgl1 libegl1 libopengl0 libglx0 libfontconfig1 libfreetype6 libx11-6 libx11-xcb1 libxcb1 libxkbcommon0 libxkbcommon-x11-0 libdbus-1-3 libwayland-client0 libwayland-cursor0 libwayland-egl1 libgbm1 libdrm2 libvulkan1 fonts-dejavu-core`

Nexus and Steam logins are kept in the Secret Service (KWallet or GNOME Keyring). Without
one, they are remembered only until Mod Organizer closes.

Next: [First steps](first-steps.md).
