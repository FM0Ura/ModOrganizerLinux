# ModOrganizerLinux

Superproject for a native Linux port of [Mod Organizer 2](https://github.com/ModOrganizer2/modorganizer).
The goal is a Mod Organizer 2 that runs natively on Linux (Bazzite is the reference
platform) and manages mods for Steam games running through Proton, while the application
fork (`modorganizer`) keeps building on Windows against the upstream `uibase`.

This repository holds the build scripts, CI, documentation and the upstream dependencies
(vendored under [`deps/`](deps/README.md) with `git subtree`).

- Design specification: [docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md](docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md)
- Vendored dependencies and their pinned versions: [deps/README.md](deps/README.md)
- Updating a dependency: `scripts/update-dep.sh <name> <ref>`

## Phase 1 status

Phase 1 (native build, no virtual file system yet) is complete in code and CI; the
remaining criterion is the manual run on Bazzite
([docs/testing/phase1-manual-checklist.md](docs/testing/phase1-manual-checklist.md), in Portuguese).

Done:

- `build.sh` builds the application and every dependency from a clean Fedora 44 container
  and installs `install/bin/ModOrganizer` (+ `plugins/`, `stylesheets/`, `tutorials/`,
  `translations/`, `resources/`) and `install/lib/`.
- Linux CI builds everything, runs the unit tests, the plugin-load harness and a headless
  smoke test of the application, and uploads `install/` as the artifact `mo2-linux-install`.
- Windows CI (builds the fork against upstream `uibase`) runs only for commits whose message
  contains `[windows-ci]`, or on manual dispatch. It builds the fork against the dependencies
  of upstream `master` (via `mob`), **not** against our patched copies in `deps/` (`uibase`,
  `archive`, `bsatk`, `esptk`, `cmake_common`, `game_bethesda`, installers). Those patches have
  only been checked for Windows by an `unifdef -D_WIN32` review (the Windows view of each file
  is unchanged) and have never been compiled on Windows. Next phase: a `mob` run that overrides
  the dependencies with our subtrees.
- Performance baseline: [benchmarks/results/2026-10-phase1-baseline.md](benchmarks/results/2026-10-phase1-baseline.md).

Limitations of Phase 1:

- No virtual file system (Phase 2): mods are managed but the game does not see them.
- Launching programs shows a dialog saying it arrives in Phase 3.
- LOOT is unavailable, creating BSA archives is unavailable, there is no auto-update.
- Nexus and Steam logins are not persisted on Linux yet (kept in memory only; a secret-store
  integration comes later).
- "My Games" and AppData point to XDG folders (Phase 3 maps them into the Proton prefix).
- Steam auto-detection covers `~/.local/share/Steam`, `~/.steam/steam`, Flatpak Steam and the
  extra libraries listed in `libraryfolders.vdf`; otherwise give the game path by hand.
- No Python plugins and no `nxm://` link handler until Phase 4.
- RAR extraction is verified manually only (no automated fixture).
- "Remove mod" / "remove download" send files to the trash only when the instance is on the same
  filesystem as the home trash (`~/.local/share/Trash`). On another disk the operation fails
  safely with "failed to delete" and nothing is deleted.
- No AppImage yet (Phase 5): the binary links against the system Qt 6, so it must run in a
  Fedora 44 environment (see below), not directly on the Bazzite host.

## Rodando no Bazzite (pt-BR)

O binário usa o Qt 6 do Fedora 44; no Bazzite ele roda dentro de um distrobox Fedora 44
(AppImage só na Fase 5). Passo a passo:

1. Crie o distrobox, de um destes dois jeitos.
   - Com a mesma imagem do CI (recomendado):
     ```sh
     git clone https://github.com/FM0Ura/ModOrganizerLinux.git && cd ModOrganizerLinux
     git checkout claude/serene-maxwell-1dyrbg
     podman build -t mo2-linux-build -f build/Containerfile build/
     distrobox create --name mo2-run --image localhost/mo2-linux-build
     ```
   - Ou uma imagem Fedora limpa com os mesmos pacotes do `build/Containerfile`:
     ```sh
     distrobox create --name mo2-run --image fedora:44
     distrobox enter mo2-run -- sudo dnf -y install --setopt=install_weak_deps=False \
         qt6-qtbase-devel qt6-qtwebengine-devel qt6-qtwebsockets-devel \
         qt6-qtnetworkauth-devel qt6-qtdeclarative-devel boost-devel spdlog-devel \
         fmt-devel lz4-devel zlib-devel fuse3-devel qt6-qtwayland
     ```
2. Obtenha o artefato `mo2-linux-install`. Duas rotas:
   - **Pelo navegador:** GitHub -> repositório `FM0Ura/ModOrganizerLinux` -> aba Actions ->
     workflow **Linux** -> último run verde da branch `claude/serene-maxwell-1dyrbg` ->
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

Problemas na abertura: nunca rode o MO2 como root/`sudo`; se ele travar logo depois de escolher
a instância, use `env QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu` antes do binário (e, em placa
NVIDIA, crie o distrobox com `--nvidia`); se aparecer "SHM error ... permission denied", remova
os `/tmp/qipc_*_mo*` deixados por uma execução anterior como root. Detalhes no checklist.

Alternativa: compilar você mesmo. Isso só funciona no distrobox criado com a imagem do
`build/Containerfile` (a rota `fedora:44` + `dnf` acima não tem gcc, cmake, ninja nem Rust) e
exige o fork clonado ao lado deste repositório, em `../modorganizer`:

```sh
git clone -b claude/serene-maxwell-1dyrbg https://github.com/FM0Ura/modorganizer.git ../modorganizer
distrobox enter mo2-run -- ./build.sh        # gera install/bin/ModOrganizer
```

Depois siga o [checklist manual](docs/testing/phase1-manual-checklist.md).

## Building

The build environment is defined once, in [`build/Containerfile`](build/Containerfile)
(Fedora 44: GCC, CMake, Ninja, Qt 6 incl. WebEngine/NetworkAuth/WebSockets, Boost, spdlog,
fmt, lz4, zlib, gtest, fuse3, clang-tools-extra, Rust stable). CI uses the same file. If
`fedora:44` is ever unavailable, use the latest stable Fedora tag and note it here.

`build.sh` expects the application fork checked out next to this repository
(`../modorganizer`, override with `MO2_SRC`). It builds into `build-out/` and installs into
`install/`:

```sh
./build.sh                          # all implemented targets, in dependency order
./build.sh --only uibase --tests    # one or more targets (repeatable), with ctest
./build.sh --list                   # targets in build order
```

Targets not ported yet are skipped by the default run and rejected with `--only`.

### With podman or docker

```sh
podman build -t mo2-linux-build -f build/Containerfile build/
podman run --rm -v "$PWD/..:/work:Z" -w /work/ModOrganizerLinux mo2-linux-build ./build.sh --tests
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
