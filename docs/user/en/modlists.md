# Modlists: share and import a mod setup

[Português](../pt-BR/modlists.md) · **English** · Previous: [Games](games.md) · Next: [Troubleshooting](troubleshooting.md)

A modlist is a `.mo2list` file that describes a profile: which mods, in which order, the
plugin order, the game's INI files, the FOMOD choices and the executables. Someone else
imports it and Mod Organizer downloads and installs the mods. Status: implemented, manual
test pending. Collections from Nexus, Wabbajack and an assistant come later.

What goes into the file is a **description**, not the mods: Nexus mods are listed by id,
so everyone downloads them from Nexus. Only mods you mark as made by you carry their files.

## Export

**File → Export modlist…** (next to Install Mod) exports the current profile.

1. Fill in the name, author, version and an optional README for the people who import it.
2. By default only the **active** mods go in. **Include inactive mods as optional** adds
   the others; they arrive unchecked in the importer's review.
3. Each mod that does not come from Nexus has a choice:
   - **Manual source** (default): the list says where to get it (you give a link) and
     carries its checksum when the downloaded file is still around.
   - **Made by me — include files**: the files go into the package. Use it only for content
     you made or may redistribute. It is disabled for Nexus mods, which are never bundled.
   - **Leave out**.
4. The `overwrite` folder is listed file by file, **all unchecked**. What you check goes
   into the package.
5. The window shows the warnings (for example a mod with several files, a mod without a
   checksum) and the executables with the **full command line and environment**.
6. Before saving, the export looks for your home folder path and user name in the INI
   files, executables, bundled text files and the manifest. If it finds any, it shows where
   and asks for confirmation (**Save anyway**). Absolute paths of the instance, game,
   Proton prefix and home are replaced by markers (`%INSTANCE%`, `%GAME%`,
   `%PROTON_PREFIX%`, `%HOME%`) and restored on import.
7. While saving, Mod Organizer computes the MD5 checksum of the downloaded archives,
   with a progress window you can cancel.

Never included: saves, `ModOrganizer.ini`, tokens, API keys and logs. Environment
variables of an executable whose name contains `KEY`, `TOKEN`, `SECRET`, `PASS`, `AUTH`,
`COOKIE` or `SESSION` are dropped, with a warning.

### Limits

- **Keep your edits in a separate mod or in `overwrite`.** An edit made *inside* the
  folder of a Nexus mod (for example a hand-edited `.ini` of the mod) is **not** carried:
  whoever imports gets the original file. Export does not detect this.
- FOMOD choices are only recorded for mods installed with this version or later. For older
  ones the export warns, and the importer picks the options by hand.
- Computing the checksums of big archives takes time.

## Import

Double-click a `.mo2list` in the file manager (it opens in Mod Organizer, running or not)
or use **File → Import modlist…**. Mod Organizer never runs anything from the list.
Double-click works once Mod Organizer has been started at least once, because it registers
the file type at startup. If another application is already the default for `.mo2list`
files, Mod Organizer does not replace it; use **File → Import modlist…** or choose Mod
Organizer in the file manager's "Open with".

1. **Destination:** a new instance, a new profile or an existing profile. Nothing is
   pre-selected. A list for another game only goes into a new instance of that game. A list
   from a newer Mod Organizer is refused with a message to update.
2. **Review:** the mods, with optional ones **unchecked**. If you already have a mod in a
   different version, choose **Download the list's version** or **Use mine**. The footer says whether
   you have Nexus Premium or "No Premium: you will click N downloads on Nexus".
   If the list carries executables, native files (`.dll`, `.exe`, scripts...) or INIs that
   load DLLs, an **Executable content** section lists them (executables with binary,
   arguments, working directory and environment) and you must confirm it to continue.
3. **Downloads:** with Premium they are automatic. Without Premium a window opens the page
   of each Nexus file; click **Mod Manager Download** there and the file comes back to
   Mod Organizer. **Skip**, **Open again** and **Stop waiting / Cancel** are in the window. Mods with
   a manual source have **Open page** and **Choose downloaded file…**. A file you already
   have (same checksum) is reused.
4. **Install:** all mods install in a batch, **with no dialogs**. What would need a
   decision goes to the summary. When you import into an existing profile and some reused
   mods are out of place, one question appears first: "Move N reused mods to the list's
   order?" (Yes moves them, No leaves your order).
5. **Summary:** Installed, Reused, Failed and Skipped, with **Install manually**
   (opens the usual installer for that mod), **Retry failed**, **Sort with LOOT** and
   **Copy** (the summary as text).

A **new instance** restarts Mod Organizer and continues by itself. If you close Mod
Organizer in the middle of an import, the next start offers **Continue**, **Later** or
**Discard**; Continue does not repeat the mods that are already in.

### What the import never does to your setup

- Nothing of yours is deleted or reinstalled over. A mod in a different version installs
  **next to yours** as `<name> [<list>]`, and your old one is disabled **only in the
  destination profile**.
- Imported executables never replace one of yours: if the title is taken (case does not
  matter), the new one is added as `<title> [<list>]`. A binary must be inside the game,
  instance, `mods` or `overwrite` folder. Of the environment variables, only an allowlist of
  Proton, DXVK and VKD3D tuning ones survive (for example `PROTON_LOG`, `DXVK_*`,
  `VKD3D_*`, `WINEDLLOVERRIDES`, `MANGOHUD`); the others are listed in the summary.
- Only the game's INI files are applied. In an existing profile, Mod Organizer asks first.
  If the profile does not use local INI files, the list's INIs are **not** applied (turning
  local INIs on is your call) and they are listed in the summary under "INI files not
  applied".
- A new instance and a new profile get local INI files and local saves.

### Known limits

- A single huge malicious archive entry inside a `.mo2list` can still exhaust memory on
  import (it only crashes Mod Organizer; open lists from people you trust).
- Nexus files that were replaced or removed since the list was made go to the summary as
  failed.

Next: [Troubleshooting](troubleshooting.md).
