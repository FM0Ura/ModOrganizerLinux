# Troubleshooting and known issues

[Português](../pt-BR/troubleshooting.md) · **English** · Previous: [Games](games.md)

## It does not start

- **No window on Wayland:** try `QT_QPA_PLATFORM=xcb ~/Applications/ModOrganizer.AppImage`.
- **Freezes right after choosing the instance:** start it with
  `QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu`.
- **"SHM error ... permission denied":** delete the `/tmp/qipc_*_mo*` files left by an
  earlier run as root. Never run Mod Organizer as root.
- **Over SSH:** export the variables of the desktop session first (`WAYLAND_DISPLAY`,
  `XDG_SESSION_TYPE`, `XDG_RUNTIME_DIR`, `DISPLAY`, `XAUTHORITY`). With no display Mod
  Organizer says "needs a graphical session" and exits.
- **The AppImage says it cannot run:** the glibc must be 2.43 or newer
  ([Install](install.md#what-you-need)), and FUSE 3 or `--appimage-extract-and-run` is needed.

## "umu-run" or "fusermount3" is missing

Mod Organizer checks the [host dependencies](install.md#host-dependencies) before it runs
a program and lists what is missing. It opens and manages mods without them; only **Run**
needs them.

## The game does not get keyboard focus (KDE Wayland)

A game started by Mod Organizer is an X11 window (through XWayland) without a startup id,
opened from a native Wayland window, so KWin's focus stealing prevention marks it as "wants
attention" and the keyboard stays with the previous window. Steam does not hit this.
Workaround, a KWin window rule:

1. System Settings, Window Management, Window Rules, **Add New...**
2. **Window class (application)**, match **Regular expression**: `steam_app_.*`
3. Add the property **Focus stealing prevention**, set to **Force**, **None**. Apply.

A fix inside Mod Organizer is still open.

## Baldur's Gate 3

- The native Linux version (`bin/bg3`) that Steam installs when Proton is not forced is
  not recognized; use Proton.
- There is no load order on Linux yet: `modsettings.lsx` is not generated, because LSLib
  (`Divine.dll`) rejects Unix paths. Mod Organizer warns once per session. Generate the
  file elsewhere and put it in the profile; Mod Organizer maps it to the game.
- With Proton the game opens through Mod Organizer, but a save whose name has an accent
  was not written ("Failed to create dir..."). Not yet compared with the game opened from
  Steam without Mod Organizer.
- The Script Extender needs the DLL override in [Games](games.md#proxy-dlls-wine-dll-overrides).

## The built-in web view and your own certificate authorities

The browser window inside Mod Organizer (Nexus login, for example) uses Chromium's
built-in root certificates. On hosts that are not Fedora-based, certificate authorities you
added to the system store are not trusted there. Downloads and the LOOT masterlist use the
host's certificate bundle.

## Other known limitations

- If a program started through Mod Organizer is stuck, the interface of Mod Organizer can
  freeze while the virtual folder is mounted (the "helper" case, still being investigated).
  Close the program, or `fusermount3 -u <mount>` if it is left mounted.
- Pandora (window with two title bars, mouse offset) and BodySlide (black preview) have
  glitches on a 4K screen with scaling 2.7 on KDE; not diagnosed.
- NieR:Automata mods always go through the manual installer.
- "Remove mod" and "remove download" send files to the trash only when the instance is on
  the same filesystem as `~/.local/share/Trash`; otherwise the operation fails safely with
  "failed to delete" and nothing is deleted.
- RAR extraction was verified by hand only.
- The FNIS check works for Skyrim (LE) only, as upstream; it is inactive for Skyrim SE. The
  basic diagnostics have no NTFS attribute check on Linux.
- Restarting (Change Game, opening another instance) used to crash the second time in the
  same process. It is fixed in the AppImage (Phase 5; still to be confirmed on a real
  desktop); if Mod Organizer still crashes when you switch instances, report it.

## Reporting a crash

Open an [issue](https://github.com/FM0Ura/ModOrganizerLinux/issues/new) with the version
(Help, About), your distribution, what you did, and the backtrace. To get one:

1. Download `ModOrganizer-<version>-x86_64.debug.tar.zst` of **the same release** and
   extract it; it is a `.build-id/xx/yyyy.debug` tree:

   ```sh
   mkdir -p ~/mo2-debug
   tar --zstd -xf ModOrganizer-2.5.2.1-x86_64.debug.tar.zst -C ~/mo2-debug
   ```

2. Open the last core dump (systemd) in gdb:

   ```sh
   coredumpctl list ModOrganizer
   coredumpctl gdb ModOrganizer
   ```

3. In gdb:

   ```
   (gdb) set debug-file-directory /home/you/mo2-debug
   (gdb) bt full
   ```

   If gdb cannot find the AppImage's libraries (the mount of the AppImage is gone after the
   crash), extract it with `ModOrganizer.AppImage --appimage-extract` and
   `set solib-search-path <squashfs-root>/usr/lib` before `bt full`.

Also attach `logs/crash.log` of the instance if it exists.
