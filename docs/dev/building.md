# Building and developing

This is the developer guide: how to build Mod Organizer 2 for Linux from source, how the
CI and the releases work, and the variables that exist for development and tests. Users
who only want to run it should read the [README](../../README.md) and the
[user guide](../user/en/install.md).

Superproject for a native Linux port of [Mod Organizer 2](https://github.com/ModOrganizer2/modorganizer).
The goal is a Mod Organizer 2 that runs natively on Linux (Bazzite is the reference
platform) and manages mods for Steam games running through Proton, while the application
keeps building on Windows against the upstream `uibase`.

This repository holds the build scripts, CI, documentation, the application
(`deps/modorganizer`) and the upstream dependencies, all vendored under
[`deps/`](../../deps/README.md) with `git subtree`.

- Design specification: [docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md](../superpowers/specs/2026-10-14-mo2-linux-port-design.md)
- Vendored dependencies and their pinned versions: [deps/README.md](../../deps/README.md)
- Updating a dependency: `scripts/update-dep.sh <name> <ref>`

Contents: [Building](#building) · [CI](#ci) · [Releases](#releases) ·
[Variables for development and tests](#variables-for-development-and-tests) ·
[Project status and limitations](#project-status-and-limitations-phases-1-to-4) ·
[Running a CI build in a distrobox (pt-BR)](#rodando-no-bazzite-pt-br)

## Building

The build environment is defined once, in [`build/Containerfile`](../../build/Containerfile)
(Fedora 44: GCC, CMake, Ninja, Qt 6 incl. WebEngine/NetworkAuth/WebSockets, Boost, spdlog,
fmt, lz4, zlib, gtest, gmock, gsl-lite, tbb, openssl, fuse3, clang-tools-extra, Rust stable, and, for the Python
plugins, `python3-devel pybind11-devel python3-pyqt6 python3-pyqt6-devel python3-pip
python3-pytest sip6`). CI uses the same file. If
`fedora:44` is ever unavailable, use the latest stable Fedora tag and note it here.

`build.sh` builds the application from `deps/modorganizer` (override with `MO2_SRC`).
It builds into `build-out/` and installs into
`install/`:

```sh
./build.sh                          # all implemented targets, in dependency order
./build.sh --only uibase --tests    # one or more targets (repeatable), with ctest
./build.sh --list                   # targets in build order
```

The `mo2-vfs` target builds the Rust FUSE virtual file system (`mo2-vfs/`) into
`install/bin/mo2-vfs`; its tests need `/dev/fuse` and `fusermount3` (they are skipped
without them, and fail with `MO2_REQUIRE_FUSE=1`, as in CI).

Targets not ported yet are skipped by the default run and rejected with `--only`.

### With podman or docker

```sh
podman build -t mo2-linux-build -f build/Containerfile build/
podman run --rm -v "$PWD/..:/work:Z" -w /work/ModOrganizerLinux mo2-linux-build ./build.sh --tests
```

Building in a Fedora 44 box that was not created from that image needs, besides the other
dependencies, the Python packages `python3-devel pybind11-devel python3-pyqt6
python3-pyqt6-devel python3-pip python3-pytest sip6`, `gmock-devel`, `gsl-lite-devel`, `tbb-devel` and `openssl-devel`
(the `libbsarchpp` tests need OpenSSL):

```sh
distrobox enter mo2-run -- sudo dnf install -y python3-devel pybind11-devel python3-pyqt6 \
    python3-pyqt6-devel python3-pip python3-pytest sip6 gmock-devel gsl-lite-devel tbb-devel \
    openssl-devel
```

### With distrobox (Bazzite)

```sh
podman build -t mo2-linux-build -f build/Containerfile build/
distrobox create --name mo2-build --image localhost/mo2-linux-build
distrobox enter mo2-build -- ./build.sh --tests
```

### Local environment

No container is required: any environment that provides the dependencies listed in the
Containerfile works. `build.sh` respects `CC`, `CXX` and `CMAKE_PREFIX_PATH` from the
environment (it prepends `install/` to `CMAKE_PREFIX_PATH`).

## CI

| Workflow | When it runs | What it does |
|---|---|---|
| `.github/workflows/linux.yml` | every push and pull request | builds everything in the container of `build/Containerfile` (cached by the hash of that file), runs the unit tests, the plugin-load harness, the headless smoke test and the self-update end-to-end test (`tests/selfupdate-e2e`); uploads `install/` as the artifact `mo2-linux-install` |
| `.github/workflows/windows.yml` | only if the head commit message contains `[windows-ci]`, or on manual dispatch | builds the application against the dependencies of upstream `master` (via `mob`); see the status section below for what that does and does not prove |
| `.github/workflows/release-linux.yml` | push of a `v*` tag, manual dispatch, or a push whose head commit message contains `[appimage-ci]` | builds the AppImage and tests it in clean containers; see [Releases](#releases) |

Put `[windows-ci]` and/or `[appimage-ci]` in the message of the head commit of a push to
run those workflows; both are slow and are not run on every push. The checkpoint commit
of a phase carries both: `ci: Phase N checkpoint [windows-ci] [appimage-ci]`.

## Releases

A release is built by `release-linux.yml` and stays a **draft** until you publish it by
hand; the workflow never publishes (`gh release create --draft`).

The files are named after the version of the port, `2.5.2.N` (the upstream version plus a
fourth number; builds made by hand keep `2.5.2`):

- `ModOrganizer-<version>-x86_64.AppImage`
- `ModOrganizer-<version>-x86_64.AppImage.zsync` (delta update information)
- `ModOrganizer-<version>-x86_64.debug.tar.zst` (debug symbols, `.build-id/xx/yyyy.debug`)
- `SHA256SUMS`

To cut a release:

1. Make sure the branch to release is green on `linux.yml`.
2. Tag it with a four-number version and push the tag:

   ```sh
   git tag v2.5.2.1
   git push origin v2.5.2.1
   ```

   The workflow takes the version from the tag (`v2.5.2.1` becomes `MO2_VERSION=2.5.2.1`),
   builds the application with its tests in the container, makes the AppImage
   (`build/appimage/build-appimage.sh`, which runs `scripts/check-appdir.sh`), runs the
   smoke test of the AppImage in a clean container of every distribution of
   `tests/appimage/distros.txt` and creates the draft release `v2.5.2.1`.
3. Without a tag, run the workflow by hand (Actions, "Linux release", Run workflow) with the
   `version` input; tick `create_draft` to get a draft release, otherwise only the files of
   the run (an artifact) are produced. A push whose head commit message contains
   `[appimage-ci]` builds as `2.5.2.0` and uploads the files as a run artifact, without a release.
4. Test the draft by the checklist (`docs/testing/phase5-manual-checklist.md`, in
   Portuguese) and then publish it in the GitHub UI (or `gh release edit v2.5.2.1 --draft=false`).
   Mod Organizer ignores drafts: the self-update and Gear Lever see only published
   releases of a public repository.

To build the AppImage on your machine, in the build container (Fedora 44), from an
`install/` made by `./build.sh`:

```sh
MO2_VERSION=2.5.2.1 build/appimage/build-appimage.sh install appimage-out
```

To test one in clean containers: `tests/appimage/run-distro-smoke.sh <AppImage> --all`
(podman or docker; `MO2_CONTAINER_ENGINE` picks one). The tested distributions and the
host packages each one gets are in `tests/appimage/distros.txt`.

## Variables for development and tests

These are read by the code; none is needed to use Mod Organizer. The first three exist
for tests and development only.

| Variable | Where | What it does |
|---|---|---|
| `MO2_UPDATE_API_URL` | `deps/modorganizer/src/github.cpp` (Linux only) | Replaces `https://api.github.com` as the base of the GitHub API requests, so the self-update of the AppImage can be tested against a local HTTP server (`tests/selfupdate-e2e/run.sh`). Empty or unset: the real GitHub. |
| `MO2_SMOKE_ACCEPT_UPDATE` | `deps/modorganizer/src/selfupdater.cpp` (Linux only) | If set (any value), the self-updater installs an available update without anyone clicking: it starts the update as soon as one is found and accepts the "Update" dialog without showing it. Used by `tests/selfupdate-e2e/run.sh`. |
| `MO2_SMOKE_RESTART_ONCE` | `deps/modorganizer/src/moapplication.cpp` (Linux only) | If set, Mod Organizer restarts once in the same process, 1.5 s after the main window is first shown (the path of Change Game), keeping the current instance. `tests/smoke/modorganizer-smoke.sh` uses it to check that restarting does not crash. |
| `MO2_VERSION` | `build.sh`, `build/appimage/build-appimage.sh`, the release workflow | Version of the port, `X.Y.Z` or `X.Y.Z.N` (e.g. `2.5.2.1`). `build.sh` passes it to CMake as `MO2_VERSION_OVERRIDE`, which sets the version compiled into the application (shown in Help, About, and compared by the self-update); `build-appimage.sh` requires it for the file names. Not set: the version of the upstream source, `2.5.2`. The release workflow takes it from the tag. |
| `MO2_VFS_BRIDGE` | `deps/modorganizer/src/linux/vfsbridge_linux.cpp` | How `mo2-vfs` and the Windows programs are started: `direct` (in the environment of Mod Organizer itself, the normal case for an AppImage or a native install), `host` (through `distrobox-host-exec`, for Mod Organizer running in a distrobox) or `auto`/empty (detects a container by `/run/.containerenv` or `/.dockerenv`). Any other value logs a warning and detects. The unit tests set it to `direct`. |
| `MO2_NEXUS_CLIENT_ID` | `deps/modorganizer/src/nexusoauthconfig.cpp` | Client id of the Nexus Mods OAuth login. Default `modorganizer2`. Set it to use an application of your own. |

## Project status and limitations (Phases 1 to 4)

The status and limitations section of the former README, kept as it was. Phase 5 (the
AppImage) supersedes the lines about running only in a Fedora 44 environment and about
the Python plugins; see the [user guide](../user/en/install.md).

### Phase 1 status

Phase 1 (native build, no virtual file system yet) is complete in code and CI; the
remaining criterion is the manual run on Bazzite
([docs/testing/phase1-manual-checklist.md](../testing/phase1-manual-checklist.md), in Portuguese).

Done:

- Plugins beyond the Bethesda games and the FOMOD installer: BAIN and bundle installers, previews (text, images, BSA contents), BSA extraction, the basic diagnostics, the FNIS check and the INI editor/bakery tools (Phase 4c.1), and the BSA Packer (BSA/BA2 creation, Phase 4e).
- `build.sh` builds the application and every dependency from a clean Fedora 44 container
  and installs `install/bin/ModOrganizer` (+ `plugins/`, `stylesheets/`, `tutorials/`,
  `translations/`, `resources/`) and `install/lib/`.
- Linux CI builds everything, runs the unit tests, the plugin-load harness and a headless
  smoke test of the application, and uploads `install/` as the artifact `mo2-linux-install`.
- Windows CI (builds the application against upstream `uibase`) runs only for commits whose message
  contains `[windows-ci]`, or on manual dispatch. It builds `deps/modorganizer` against the dependencies
  of upstream `master` (via `mob`), **not** against our patched copies in `deps/` (`uibase`,
  `archive`, `bsatk`, `esptk`, `cmake_common`, `game_bethesda`, installers). Those patches have
  only been checked for Windows by an `unifdef -D_WIN32` review (the Windows view of each file
  is unchanged) and have never been compiled on Windows. Next phase: a `mob` run that overrides
  the dependencies with our subtrees.
- Performance baseline: [benchmarks/results/2026-10-phase1-baseline.md](../../benchmarks/results/2026-10-phase1-baseline.md).

### Limitations of Phase 1

- Virtual file system (Phases 2 and 3): the `mo2-vfs` helper mounts the merged view while a program runs and unmounts when it ends. Windows programs (`.exe`, `.bat`) run through `umu-run` in the Proton prefix of their Steam game (Phase 3); `umu-launcher` must be installed on the host, and a game never run from Steam has no prefix yet. When Mod Organizer runs in a distrobox, the helper and the programs run on the host (through `distrobox-host-exec`), so programs that exist only inside the container cannot be run from Mod Organizer. A game and a tool cannot run at the same time in the same prefix.
- LOOT sorts the plugins (lootcli with libloot 0.29.5, started through mo2-vfs; its data folder is the Flatpak LOOT's if there is one, else `~/.local/share/LOOT`). BSA/BA2 creation is the BSA Packer (Phase 4e, see [the games guide](../user/pt-BR/games.md#criação-de-bsaba2), in Portuguese); there is no auto-update (Phase 5 supersedes this: Mod Organizer updates the AppImage from the GitHub releases, see the [user guide](../user/en/updating.md)).
- `diagnose_basic` has no NTFS file-attribute check on Linux. DDS previews are unavailable until the DDS preview plugin (Python, Phase 4d): `preview_base` leaves `.dds` to it. The FNIS check (`check_fnis`) works for Skyrim (LE) only, as upstream; it is inactive for Skyrim SE.
- Nexus and Steam logins are kept in the Secret Service (KWallet, GNOME Keyring); without one, only until Mod Organizer closes.
- "My Games" and AppData point into the Proton prefix of the Steam game (`compatdata/<appid>/pfx`); a game without a prefix falls back to XDG folders.
- Steam auto-detection covers `~/.local/share/Steam`, `~/.steam/steam`, Flatpak Steam and the
  extra libraries listed in `libraryfolders.vdf`; otherwise give the game path by hand.
- No Python plugins until Phase 4d. `nxm://` links: Mod Organizer registers itself as the handler (a desktop file in `~/.local/share/applications`) when no other program handles them; Settings → Nexus forces it.
- RAR extraction is verified manually only (no automated fixture).
- "Remove mod" / "remove download" send files to the trash only when the instance is on the same
  filesystem as the home trash (`~/.local/share/Trash`). On another disk the operation fails
  safely with "failed to delete" and nothing is deleted.
- (Phase 1 only. Phase 5 ships the AppImage, which carries its own Qt, Python and libraries. A plain `install/` tree still links against the system Qt 6, so it must run in a
  Fedora 44 environment, see "Rodando no Bazzite" below, not directly on the Bazzite host.)

## Rodando no Bazzite (pt-BR)

O binário usa o Qt 6 do Fedora 44; no Bazzite ele roda dentro de um distrobox Fedora 44
(o AppImage é o caminho de usuário desde a Fase 5, veja [o guia de instalação](../user/pt-BR/install.md)). Passo a passo:

1. Crie o distrobox, de um destes dois jeitos.
   - Com a mesma imagem do CI (recomendado):
     ```sh
     git clone https://github.com/FM0Ura/ModOrganizerLinux.git && cd ModOrganizerLinux
     git checkout main
     podman build -t mo2-linux-build -f build/Containerfile build/
     distrobox create --name mo2-run --image localhost/mo2-linux-build
     ```
   - Ou uma imagem Fedora limpa, com os pacotes de execução do `build/Containerfile` e os do Python (para compilar, use a imagem do `build/Containerfile`):
     ```sh
     distrobox create --name mo2-run --image fedora:44
     distrobox enter mo2-run -- sudo dnf -y install --setopt=install_weak_deps=False \
         qt6-qtbase-devel qt6-qtwebengine-devel qt6-qtwebsockets-devel \
         qt6-qtnetworkauth-devel qt6-qtdeclarative-devel boost-devel spdlog-devel \
         fmt-devel lz4-devel zlib-devel fuse3-devel qt6-qtwayland libsecret \
         python3 python3-pyqt6 tbb
     ```
   - Se o box `mo2-run` foi criado com uma imagem anterior à Fase 4a: `distrobox enter mo2-run -- sudo dnf -y install libsecret` (para guardar os logins no Secret Service).
   - Se foi criado antes da Fase 4e: `distrobox enter mo2-run -- sudo dnf -y install tbb` (o BSA Packer usa o `libtbb`; com a imagem do `build/Containerfile`, o `tbb-devel` já o instala).
2. Obtenha o artefato `mo2-linux-install`. Duas rotas:
   - **Pelo navegador:** GitHub -> repositório `FM0Ura/ModOrganizerLinux` -> aba Actions ->
     workflow **Linux** -> último run verde do branch `main` ->
     seção Artifacts -> `mo2-linux-install` (é preciso estar logado no GitHub). O download é
     um zip; extraia (só nesta rota):
     ```sh
     mkdir -p ~/mo2-phase1 && unzip ~/Downloads/mo2-linux-install.zip -d ~/mo2-phase1
     ```
   - **Pelo terminal**, com o `gh` autenticado (ele já extrai os arquivos):
     ```sh
     gh run download <id do run> -R FM0Ura/ModOrganizerLinux -n mo2-linux-install -D ~/mo2-phase1
     ```
3. Nas duas rotas, o artefato perde a permissão de execução; restaure:
   ```sh
   chmod +x ~/mo2-phase1/bin/ModOrganizer
   ```
4. Rode dentro do distrobox:
   ```sh
   distrobox enter mo2-run -- ~/mo2-phase1/bin/ModOrganizer
   ```
   `QT_QPA_PLATFORM` não é necessário em um desktop (só para execução sem tela). Se a janela
   não aparecer no Wayland, tente
   `distrobox enter mo2-run -- env QT_QPA_PLATFORM=xcb ~/mo2-phase1/bin/ModOrganizer`.
   Por SSH, exporte antes as variáveis da sessão da área de trabalho (`WAYLAND_DISPLAY`,
   `XDG_SESSION_TYPE`, `XDG_RUNTIME_DIR`, `DISPLAY`, `XAUTHORITY`); sem tela, o MO2 avisa
   ("needs a graphical session") e sai.

Problemas na abertura: nunca rode o MO2 como root/`sudo`; se ele travar logo depois de escolher
a instância, use `env QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu` antes do binário (e, em placa
NVIDIA, crie o distrobox com `--nvidia`); se aparecer "SHM error ... permission denied", remova
os `/tmp/qipc_*_mo*` deixados por uma execução anterior como root. Detalhes no checklist.

Alternativa: compilar você mesmo. Isso só funciona no distrobox criado com a imagem do
`build/Containerfile` (a rota `fedora:44` + `dnf` acima não tem gcc, cmake, ninja nem Rust).
O código do app já vem neste repositório, em `deps/modorganizer`:

```sh
distrobox enter mo2-run -- ./build.sh        # gera install/bin/ModOrganizer
```

Depois siga o [checklist manual](../testing/phase1-manual-checklist.md).

### Python no box

O MO2 usa o Python 3.14 e o PyQt6 do Fedora do box `mo2-run`. Num box criado antes da Fase 4d.1, instale os pacotes uma vez:

    distrobox enter mo2-run -- sudo dnf install -y python3 python3-pyqt6

Sem esses pacotes (ou com outra versão do Python no box), o MO2 abre sem os plugins Python e o ícone de notificações mostra o aviso do "Python Proxy" com esse comando.
