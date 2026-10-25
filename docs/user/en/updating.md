# Updating

[Português](../pt-BR/updating.md) · **English** · Previous: [First steps](first-steps.md) · Next: [Games](games.md)

Releases are published on the
[releases page](https://github.com/FM0Ura/ModOrganizerLinux/releases). There are three ways
to update; use whichever suits you.

## Inside Mod Organizer

With **Settings, Check for updates** on, Mod Organizer looks at the releases of
`FM0Ura/ModOrganizerLinux` when it starts. When there is a newer version, an update button
appears in the toolbar. Click it to see the version and the release notes, then accept:

1. it downloads the new AppImage next to the old one and checks its SHA-256 against the
   `SHA256SUMS` of the release;
2. it replaces your file **in place** (same name, same permissions), so your menu entry and
   the `nxm://` handler keep working;
3. it closes, and starts again with the same arguments, now in the new version
   (Help, About shows it).

It does not update when:

- a program started by Mod Organizer is still running (its virtual folder is mounted). Mod
  Organizer warns you and installs nothing (a file it had already downloaded is discarded).
  Nothing waits or resumes: close the programs, then click the update button again;
- the folder of the AppImage is not writable. Mod Organizer still checks the releases and
  shows the usual update button, but clicking it only shows a notice with a link to the
  page of the release; nothing is downloaded or replaced. Replace the file yourself, or
  update through Gear Lever;
- Mod Organizer is not running from an AppImage;
- the checksum does not match or the release has no `SHA256SUMS`: it tells you, links the
  release, and leaves the old file as it was.

Offline mode and the prerelease option are respected like on Windows.

## With Gear Lever or AppImageUpdate

The AppImage carries update information (`gh-releases-zsync`), so Gear Lever shows "update
available" and downloads only the part of the file that changed. From a terminal, the
AppImageUpdate tool does the same:

```sh
appimageupdatetool ~/Applications/ModOrganizer.AppImage
```

## By hand

Download the new `ModOrganizer-<version>-x86_64.AppImage` from the releases page, check it
with `sha256sum -c SHA256SUMS --ignore-missing`, and replace the old file.

Your instances, mods and settings are not in the AppImage, so updating never touches them.

A new release is first a draft that the maintainer tests and publishes; the update
mechanisms see only published releases.

Next: [Games](games.md).
