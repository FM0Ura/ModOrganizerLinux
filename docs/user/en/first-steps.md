# First steps

[Português](../pt-BR/first-steps.md) · **English** · Previous: [Install](install.md) · Next: [Updating](updating.md)

## Before you start

1. Install the [host dependencies](install.md#host-dependencies).
2. **Run the game once from Steam.** Proton creates the prefix (the Windows folders the
   game uses: documents, settings, saves) the first time. Mod Organizer reads the game's
   "My Games" and AppData folders from that prefix
   (`<Steam library>/steamapps/compatdata/<app id>/pfx`); without one it falls back to XDG
   folders and warns in the log.

## Create an instance

1. Start Mod Organizer and choose **Create a new instance**, then the game.
2. Mod Organizer finds Steam games in `~/.local/share/Steam`, `~/.steam/steam`, Flatpak
   Steam and the extra libraries listed in Steam's `libraryfolders.vdf`. If your game is
   somewhere else, give the folder by hand.
3. Pick where the instance (mods, profiles, downloads) lives. It is plain folders: you
   can back it up by copying it.

Other stores (GOG, Epic, EA, Origin) are not detected.

## Nexus Mods and `nxm://` links

- **Settings, Nexus**: log in to Nexus Mods. The login is kept in the Secret Service
  (KWallet, GNOME Keyring); without one it lasts until Mod Organizer closes.
- Mod Organizer registers itself as the handler of `nxm://` links (a desktop file in
  `~/.local/share/applications`) when no other program handles them; **Settings, Nexus**
  can force it. After that, "Mod Manager Download" on the Nexus site opens Mod Organizer
  and downloads, including FOMOD installers.
- If you move the AppImage, start it once so the handler is rewritten.

## Run the game and tools

Choose the program (the game, SKSE, a tool) in the toolbar and press **Run**. Mod
Organizer mounts the virtual folder with your mods, runs the Windows program through
`umu-run` in the Proton prefix of the game, and unmounts when it ends.

- A game and a tool cannot run at the same time in the same prefix.
- Games that ship a "proxy" DLL (Cyberpunk 2077's CET and RED4ext, NieR:Automata's FAR,
  Baldur's Gate 3's Script Extender) need a `WINEDLLOVERRIDES` entry; see [Games](games.md).
- To run tools such as BodySlide, Nemesis, Pandora or xEdit, see [Games](games.md#tools).
- The first time a program runs, and later, Mod Organizer tells you if `umu-run` or
  `fusermount3` is missing.

## Sort plugins with LOOT

**Sort** in the plugins list runs LOOT (libloot 0.29.5). It uses the data folder of the
Flatpak LOOT if you have it, otherwise `~/.local/share/LOOT`, and downloads the masterlist
the first time.

## Install a Mod Organizer plugin

The AppImage is read-only, so Mod Organizer plugins (`.so` files, Python plugins as a
`.py` file or a folder with an `__init__.py`) go in `~/.local/share/ModOrganizer/plugins`.
The *Open MO2 Plugins folder* entry of the folder menu opens it. Restart Mod Organizer to load
a new plugin. A plugin there with the same name as one shipped with Mod Organizer replaces
it.

Next: [Updating](updating.md).
