# Games and tools that work

[Português](../pt-BR/games.md) · **English** · Previous: [Updating](updating.md) · Next: [Troubleshooting](troubleshooting.md)

## Working games

| Game | Status |
|------|--------|
| Skyrim Special Edition (Steam + Proton) | Working: mods through the virtual file system, SKSE, LOOT and SSEEdit, checked in the manual tests on Bazzite. |
| Cyberpunk 2077 (Steam + Proton) | Working: mods through the virtual file system, checked manually on Bazzite. CET and RED4ext need `WINEDLLOVERRIDES` (see [Proxy DLLs](#proxy-dlls-wine-dll-overrides)). |
| NieR:Automata (Steam + Proton) | Working: mods through the virtual file system, checked manually on Bazzite. Every mod goes through the manual installer (the plugin, as upstream, does not recognize mod layouts). |

Every other game is only detected (the Bethesda plugins and the about 75 games of
`basic_games`) and has **not** been validated yet. Baldur's Gate 3 has been tried and has
known gaps (see [Troubleshooting](troubleshooting.md#baldurs-gate-3)).

## Tools

Install the tool as a mod (from the downloaded archive, in Mod Organizer) and set up the
executable from the `.exe` **inside the mod's folder** (executables button, binary in
`mods/<mod>/...`). As on Windows, Mod Organizer runs a program from inside `mods/` from
the matching place in the virtual `Data` folder (`mods/Nemesis/Nemesis_Engine/x.exe`
runs as `<game>/Data/Nemesis_Engine/x.exe`), so tools that must be in `Data`, like Nemesis,
work. This holds only for runs with the virtual file system (the default of **Run**), and
the mod must be active in the profile; if it is disabled the program runs from the mod
folder (a warning in the log).

- **Nemesis and BodySlide:** no arguments. What they generate goes to `overwrite` (move it
  to a mod afterwards, as on Windows).
- **Pandora:** use the Nexus build (on GitHub, the `..._win-x64_net.zip`), which ships .NET;
  the zip that depends on an installed .NET does not open in a prefix without .NET 10. In
  5.0.0-beta Pandora itself ignores the `-o` argument: the output goes to `overwrite`.
- **xEdit for other games** (FO4Edit, TES5Edit, FNVEdit...): the executable runs in the
  prefix of the instance's game (or the one in the executable's "Steam AppID" field). The
  game must have run once from Steam, so that the prefix exists and the `Installed Path`
  registry entry, where xEdit looks for the game, is written.

## Python plugins

Mod Organizer uses the Python 3.14 and PyQt6 **inside the AppImage**; nothing to install,
and it never reads your system Python, `PYTHONPATH` or `~/.local` packages. Bundled
Python plugins: DDS preview, format 43 checker, script extender plugin checker, Configurator
(INIs), BAIN installer with wizard, FNIS and `basic_games`.

`basic_games` brings community support for about 75 non-Bethesda game modules. On Linux it
sees only **Steam** games: Mod Organizer finds the installed game by app ID in the Steam
libraries of the user; GOG, Epic, EA and Origin are not detected (logged once). The game's
documents, settings and saves live in its Proton prefix
(`<library>/steamapps/compatdata/<app ID>/pfx/drive_c/users/steamuser/...`), so **run the
game once from Steam** before creating the instance; without a prefix Mod Organizer uses the
XDG folders and says so in the log. Of the `basic_games` games, Cyberpunk 2077 and
NieR:Automata are validated (see the table above); Baldur's Gate 3 was tried and has known
gaps, and the rest are detected but untested.

### Proxy DLLs (Wine DLL overrides)

Mods that ship a "proxy" DLL (Cyberpunk 2077: CET and RED4ext, with `version.dll` and
`winmm.dll`; Baldur's Gate 3: Script Extender, `DWrite.dll`; NieR:Automata: FAR,
`dinput8.dll`) **do not load by themselves on Linux**: forced libraries
(`executableForcedLoads`, and the BG3 plugin's `force_load_dlls` option) are ignored, and
Wine prefers its own DLL to the one in the game folder. Since Mod Organizer starts the game
through Proton, Steam launch options do not apply either. In **Tools, Modify Executables...**,
select the game executable and, in the **Proton** group, on the **Environment** line, write
`WINEDLLOVERRIDES=<dll>=n,b` (without `.dll`; several DLLs separated by commas):

- `WINEDLLOVERRIDES=version,winmm=n,b` (Cyberpunk 2077)
- `WINEDLLOVERRIDES=DWrite=n,b` (Baldur's Gate 3)
- `WINEDLLOVERRIDES=dinput8=n,b` (NieR:Automata)

## Creating BSA/BA2 archives

**Tools, BSA Packer** packs the loose files of a mod into a `.bsa` (Morrowind, Oblivion,
Fallout 3, Fallout: New Vegas, Skyrim, Enderal, Skyrim SE and Enderal SE) or `.ba2`
(Fallout 4 and Starfield, with textures in a separate `... - Textures.ba2`) in the mod's
folder. For the other games (the VR editions, the `basic_games` games) the tool writes no
archive and no plugin, but with `hide_loose_assets` on, upstream still hides the folders.
In **Settings, Plugins, BSA Packer**:

- `create_plugins` creates the empty plugin (`.esp`/`.esl`) that makes the game load the
  archive (none for Morrowind: upstream creates no plugin for it);
- `hide_loose_assets` hides the loose files (the folders become `....mohidden`);
- `blacklisted_files` should list the extensions to leave out, but with the default value it
  is not applied (an upstream defect on every platform: `ArchiveBuilderHelper.cpp` splits the
  list on spaces while the default and the description use `;`), so those files also go into
  the archive (and, with `hide_loose_assets`, still come back loose to their place);
- `compress_archives` compresses.

It is upstream's `bsapacker` plugin: on Linux the archive is written by `libbsarchpp` (on
Windows by `libbsarch`, which exists only there). The format is the same, but LZ4 archives
(Skyrim SE, Enderal SE and Starfield textures; Starfield's main archives use zlib) are not
byte-identical to xEdit's. In a `.bsa`, names are written in lower case, as the game looks
them up; outside Morrowind a file without an extension (for example `docs/LICENSE`) is
left out, and of two files whose names differ only in case only the first goes in (both
cases with a warning in the log). The validation target is Skyrim SE (manual test
pending); the other games are supported, not validated (automated tests only). A single
file above 4 GiB is not created (the reason goes to the log:
`failed to write archive <path>: <message>`).

Next: [Troubleshooting](troubleshooting.md).
