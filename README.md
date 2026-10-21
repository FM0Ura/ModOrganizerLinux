# ModOrganizerLinux

Superproject for a native Linux port of [Mod Organizer 2](https://github.com/ModOrganizer2/modorganizer).
The goal is a Mod Organizer 2 that runs natively on Linux (Bazzite is the reference
platform) and manages mods for Steam games running through Proton, while the application
keeps building on Windows against the upstream `uibase`.

This repository holds the build scripts, CI, documentation, the application
(`deps/modorganizer`) and the upstream dependencies, all vendored under
[`deps/`](deps/README.md) with `git subtree`.

- Design specification: [docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md](docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md)
- Vendored dependencies and their pinned versions: [deps/README.md](deps/README.md)
- Updating a dependency: `scripts/update-dep.sh <name> <ref>`

## Phase 1 status

Phase 1 (native build, no virtual file system yet) is complete in code and CI; the
remaining criterion is the manual run on Bazzite
([docs/testing/phase1-manual-checklist.md](docs/testing/phase1-manual-checklist.md), in Portuguese).

Done:

- Plugins beyond the Bethesda games and the FOMOD installer: BAIN and bundle installers, previews (text, images, BSA contents), BSA extraction, the basic diagnostics, the FNIS check and the INI editor/bakery tools (Phase 4c.1).
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
- Performance baseline: [benchmarks/results/2026-10-phase1-baseline.md](benchmarks/results/2026-10-phase1-baseline.md).

Limitations of Phase 1:

- Virtual file system (Phases 2 and 3): the `mo2-vfs` helper mounts the merged view while a program runs and unmounts when it ends. Windows programs (`.exe`, `.bat`) run through `umu-run` in the Proton prefix of their Steam game (Phase 3); `umu-launcher` must be installed on the host, and a game never run from Steam has no prefix yet. When Mod Organizer runs in a distrobox, the helper and the programs run on the host (through `distrobox-host-exec`), so programs that exist only inside the container cannot be run from Mod Organizer. A game and a tool cannot run at the same time in the same prefix.
- LOOT sorts the plugins (lootcli with libloot 0.29.5, started through mo2-vfs; its data folder is the Flatpak LOOT's if there is one, else `~/.local/share/LOOT`). Creating BSA archives is unavailable, there is no auto-update.
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
- No AppImage yet (Phase 5): the binary links against the system Qt 6, so it must run in a
  Fedora 44 environment (see below), not directly on the Bazzite host.

## Rodando no Bazzite (pt-BR)

O binário usa o Qt 6 do Fedora 44; no Bazzite ele roda dentro de um distrobox Fedora 44
(AppImage só na Fase 5). Passo a passo:

1. Crie o distrobox, de um destes dois jeitos.
   - Com a mesma imagem do CI (recomendado):
     ```sh
     git clone https://github.com/FM0Ura/ModOrganizerLinux.git && cd ModOrganizerLinux
     git checkout main
     podman build -t mo2-linux-build -f build/Containerfile build/
     distrobox create --name mo2-run --image localhost/mo2-linux-build
     ```
   - Ou uma imagem Fedora limpa com os mesmos pacotes do `build/Containerfile`:
     ```sh
     distrobox create --name mo2-run --image fedora:44
     distrobox enter mo2-run -- sudo dnf -y install --setopt=install_weak_deps=False \
         qt6-qtbase-devel qt6-qtwebengine-devel qt6-qtwebsockets-devel \
         qt6-qtnetworkauth-devel qt6-qtdeclarative-devel boost-devel spdlog-devel \
         fmt-devel lz4-devel zlib-devel fuse3-devel qt6-qtwayland libsecret
     ```
   - Se o box `mo2-run` foi criado com uma imagem anterior à Fase 4a: `distrobox enter mo2-run -- sudo dnf -y install libsecret` (para guardar os logins no Secret Service).
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

Depois siga o [checklist manual](docs/testing/phase1-manual-checklist.md).

### Ferramentas (BodySlide, Nemesis, Pandora, xEdit)

Instale a ferramenta como mod (pelo MO2, a partir do arquivo baixado) e configure o executável pelo `.exe` **dentro da pasta do mod** (botão de executáveis → binário em `mods/<mod>/...`). Como no Windows, o MO2 roda um programa de dentro de `mods/` a partir do lugar correspondente no `Data` virtual (`mods/Nemesis/Nemesis_Engine/x.exe` → `<jogo>/Data/Nemesis_Engine/x.exe`), então ferramentas que precisam estar no `Data`, como o Nemesis, funcionam. Isso vale só para execuções com o sistema de arquivos virtual (o padrão do "Executar"), e o mod precisa estar ativo no perfil; desativado, o programa roda da pasta do mod (aviso no log).

- **Nemesis e BodySlide:** sem argumentos. O que eles geram vai para o `overwrite` (mova para um mod depois, como no Windows).
- **Pandora:** use o build do Nexus (no GitHub, o `..._win-x64_net.zip`), que traz o .NET; o zip que depende do .NET instalado não abre num prefixo sem o .NET 10. Na 5.0.0-beta o argumento `-o` é ignorado pelo próprio Pandora: a saída vai para o `overwrite`.
- **xEdit de outros jogos** (FO4Edit, TES5Edit, FNVEdit...): o executável roda no prefixo do jogo da instância (ou no do campo "Steam AppID" do executável). O jogo precisa ter rodado uma vez pela Steam, para o prefixo existir e o registro `Installed Path`, onde o xEdit procura o jogo, ser gravado.

### Plugins Python

O MO2 usa o Python 3.14 e o PyQt6 do Fedora do box `mo2-run`. Num box criado antes da Fase 4d.1, instale os pacotes uma vez:

    distrobox enter mo2-run -- sudo dnf install -y python3 python3-pyqt6

O Python do MO2 é isolado: não lê `PYTHONPATH` nem as variáveis `PYTHON*`, e não enxerga pacotes do `~/.local` nem de `/usr/local`; só a biblioteca padrão e os pacotes do sistema do box, mais as pastas do próprio MO2. Plugins Python incluídos: pré-visualização de DDS, verificador do formato 43, verificador de plugins do script extender, Configurator (INIs), instalador de BAIN com wizard e FNIS. O `basic_games` vem na Fase 4d.2.

## Building

The build environment is defined once, in [`build/Containerfile`](build/Containerfile)
(Fedora 44: GCC, CMake, Ninja, Qt 6 incl. WebEngine/NetworkAuth/WebSockets, Boost, spdlog,
fmt, lz4, zlib, gtest, gmock, fuse3, clang-tools-extra, Rust stable, and, for the Python
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
python3-pyqt6-devel python3-pip python3-pytest sip6` and `gmock-devel`:

```sh
distrobox enter mo2-run -- sudo dnf install -y python3-devel pybind11-devel python3-pyqt6 \
    python3-pyqt6-devel python3-pip python3-pytest sip6 gmock-devel
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
