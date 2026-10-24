# Estado do projeto

> Atualize este arquivo ao fim de cada tarefa, checkpoint ou decisão. O plano de cada fase continua sendo a fonte dos detalhes; aqui fica só o "onde estamos".

**Última atualização:** 2026-10-24
**Fase atual:** 4d.2 — `basic_games` · **Status:** 🟡 revisão final concluída; PR #8 aberto; teste manual pendente · **Anterior:** 4d.1 (🟡 PR #7, CI verde; teste manual pendente)
**Branch principal:** `main` (Fases 1 a 4a; `fase-2-vfs-fuse`, `fase-3-steam-proton` e `fase-4a-nexus` foram integrados; `claude/serene-maxwell-1dyrbg` é o antigo branch da Fase 1). **Branch da Fase 4b:** `fase-4b-loot` (a partir de `main`; PR #3). **Branch da Fase 4c.1:** `fase-4c1-plugins` (a partir de `fase-4b-loot`; worktree `../ModOrganizerLinux-4c1` enquanto o MO2 do usuário roda pelo checkout principal). **Branch da Fase 4c.2:** `fase-4c2-pendencias` (a partir de `fase-4c1-plugins`; worktree `../ModOrganizerLinux-4c2`). **Branch da Fase 4c.3:** `fase-4c3-ferramentas` (a partir de `fase-4c2-pendencias`; worktree `../ModOrganizerLinux-4c3`). **Branch da Fase 4d.1:** `fase-4d1-python` (a partir de `fase-4c3-ferramentas`; worktree `../ModOrganizerLinux-4d1`). **Branch da Fase 4d.2:** `fase-4d2-basic-games` (a partir de `fase-4d1-python`; worktree `../ModOrganizerLinux-4d2`)

## Repositório

Tudo fica em [`FM0Ura/ModOrganizerLinux`](https://github.com/FM0Ura/ModOrganizerLinux): build, CI, testes, docs, o app em `deps/modorganizer` e as demais dependências em `deps/`. O fork [`FM0Ura/modorganizer`](https://github.com/FM0Ura/modorganizer) (`bb46b18`) foi incorporado em 2026-10-15 e não recebe mais mudanças.

## Fase 4d.2 — progresso

Spec: [`docs/superpowers/specs/2026-10-21-phase4d2-basic-games-design.md`](docs/superpowers/specs/2026-10-21-phase4d2-basic-games-design.md) · Plano: [`docs/superpowers/plans/2026-10-21-phase4d2-basic-games.md`](docs/superpowers/plans/2026-10-21-phase4d2-basic-games.md) · Checklist manual: [`docs/testing/phase4d2-manual-checklist.md`](docs/testing/phase4d2-manual-checklist.md)

| # | Tarefa | Status | Commits principais |
|---|---|---|---|
| 1 | Spike do LSLib no Linux (sem commits de código): `dotnet Divine.dll` falha no Linux, o `TryToValidatePath` do LSLib usa `System.Uri` e um caminho Unix `/…` vira URI relativa, então toda ação sai com código 134. Decisão do usuário: a ordem de carga do BG3 fica para depois; spec e plano atualizados | ✅ | `1eda1e8` (spec e plano) |
| 2 | `mobase.utils.steamApp(app_id)` (só no Linux): o `steam::findApp()` do `uibase` exposto ao Python | ✅ | `d088285` |
| 3 | `basic_games`: subtree (hash em `deps/README.md`), alvo no `build.sh`, `find_games()` da Steam preguiçoso por app ID e as outras lojas vazias, teste com Steam falsa; a revisão pediu registrar o app ID da Steam que casa com o caminho do jogo no Linux, pular no Linux o módulo de jogo que falhou ao importar e nenhum bytecode na árvore | ✅ | subtree (split `f1207d6`), `899cab0`, `910f763`, `7de498f`, `e71dca9`, `397e408`, `5a800c3`, `5daa851`, `b886fec` |
| 4 | Pastas do usuário (`%USERPROFILE%`, `%DOCUMENTS%` e a pasta de documentos padrão) no prefixo do Proton do app ID do jogo, com o fallback XDG e um aviso por jogo | ✅ | `11b3e5e`, `01b7b3b`, `bf250eb` |
| 5 | Baldur's Gate 3 sem o LSLib (nada é baixado nem executado no Linux, com um aviso uma vez por sessão) e o `-modlist` do `redMod.exe` do Cyberpunk 2077 como caminho `Z:` | ✅ | `3bafae0`, `e08fd29`, `e438ed2`, `2d226ba` |
| 6 | Documentação, checklist e ponto de controle de CI (inclui o merge de `fase-4d1-python` com a correção do CI da 4d.1 `742af0c`: plugin Python de arquivo único importa módulos vizinhos, a ordem de carga é a do sistema de arquivos; o CI do PR #7 falhara no `FNISPatches`, que importa o `FNISTool`) | ✅ README (seção "Plugins Python"), checklist, ROADMAP, STATE e as notas de `deps/README.md`. Revisão final do branch: 0 críticos, 1 importante (mods com DLL proxy precisam de `WINEDLLOVERRIDES`, agora documentado) e 10 menores; correções revisadas sem pendências; `./build.sh --tests` completo verde em `5d31789`; PR #8 contra `fase-4d1-python` (2026-10-24) | `8e888d6` (merge), `a7db81a`, correções `a7db81a..5d31789` (`5d31789` é o `[windows-ci]`) |
| 7 | Teste manual no Bazzite ([`docs/testing/phase4d2-manual-checklist.md`](docs/testing/phase4d2-manual-checklist.md)) | ⚪ pendente | — |

## Fase 4d.1 — progresso

Spec: [`docs/superpowers/specs/2026-10-21-phase4d1-python-design.md`](docs/superpowers/specs/2026-10-21-phase4d1-python-design.md) · Plano: [`docs/superpowers/plans/2026-10-21-phase4d1-python.md`](docs/superpowers/plans/2026-10-21-phase4d1-python.md) · Checklist manual: [`docs/testing/phase4d1-manual-checklist.md`](docs/testing/phase4d1-manual-checklist.md)

| # | Tarefa | Status | Commits principais |
|---|---|---|---|
| 1 | Pacotes Python/pybind11/PyQt6 na imagem do container e `cmake_common` no Linux (versão do Python, PyQt6 e `pyuic6`/`pylupdate6` do sistema), com teste no `cmake-smoke` | ✅ | `03c99be`, `f74612c`, `1e17493` |
| 2 | `plugin_python`: subtree, proxy (`dladdr`, `libpython` com `RTLD_GLOBAL`), runner e `mobase` no Linux, alvo no `build.sh` e teste de importação do `mobase`; o pybind11 3 exigiu uma correção do caster de holder em `shared_cpp_owner.h` | ✅ | subtree `plugin_python` (split `baf0920`), `87ca46e`, `bba44ba`, `33f4c0a` |
| 3 | Runner isolado (`PyConfig` isolada e `sys.path` filtrado), testes de integração (inclusive o do isolamento, que prova `~/.local` e `PYTHONPATH` invisíveis) e o aviso no painel de diagnóstico quando o Python inicia mas não importa `mobase`/PyQt6 (pedido da revisão) | ✅ | `a2f64f0`, `c332833`, `41b5930`, `0e2ccb8` |
| 4 | Os seis plugins Python (`preview_dds`, `form43_checker`, `script_extender_plugin_checker`, `tool_configurator`, `installer_wizard`, `fnistool`): subtrees (hashes em `deps/README.md`), alvos no `build.sh`, patches Linux e teste de carga | ✅ | seis subtrees, `cb2f180`, `1acfc27`, `d286b7f`, `d23e755`, `37fc47d`, `5681d61`, `25bb2d0`, `fd32632`, `99eacdd` |
| 5 | Smoke: os plugins Python são carregados pelo aplicativo; fixture do BAIN com wizard | ✅ | `e0f150b` |
| 6 | Documentação, checklist e ponto de controle de CI | ✅ README (seção "Plugins Python" e pacotes de build), checklist, ROADMAP e STATE; o push e o PR dependem da revisão final e da autorização do usuário | este commit |
| 7 | Teste manual no Bazzite ([`docs/testing/phase4d1-manual-checklist.md`](docs/testing/phase4d1-manual-checklist.md)) | ⚪ pendente | — |
| 8 | Correções da revisão final: no Linux o proxy carrega o `libpython` e o runner em tempo de execução (`dlopen`; o runner exporta `mo2_create_python_runner`), então um Python ausente vira o aviso do painel em vez de impedir a carga do proxy; o `build.sh --tests` confere que o `plugin_python.so` não depende de nenhum dos dois; `pyuic6`, `pylupdate6` e `sip-module` do sistema antes dos do `~/.local/bin`; checklist (fechar o MO2, box sem PyQt6, `sys.path` sob o `$HOME`) | ✅ | `a77d2b5`, `7ea9b1d`, `70ca1c2`, este commit |

## Fase 4c.3 — progresso

Spec: [`docs/superpowers/specs/2026-10-21-phase4c3-ferramentas-design.md`](docs/superpowers/specs/2026-10-21-phase4c3-ferramentas-design.md) · Plano: [`docs/superpowers/plans/2026-10-21-phase4c3-ferramentas.md`](docs/superpowers/plans/2026-10-21-phase4c3-ferramentas.md) · Checklist manual: [`docs/testing/phase4c3-manual-checklist.md`](docs/testing/phase4c3-manual-checklist.md)

| # | Tarefa | Status | Commits principais |
|---|---|---|---|
| 1 | Regra dos caminhos comum (`virtualizedPaths`, em `virtualizedpaths.h`), com o Windows usando-a | ✅ | `fbd20ae` |
| 2 | O Linux roda programas de `mods/` pelo `Data` virtual (só com VFS e com o mod do binário ativo); vfs-e2e `tool-exe` | ✅ | `8b61d10`, `6133e77`, `1902980` (a revisão pediu que o perfil aberto, em memória, decida se o mod está ativo, por causa da gravação adiada da `modlist.txt`) |
| 3 | xEdit de outros jogos: fixtures `SteamGames` e correções de detecção no `game_bethesda` | ✅ | `4269a8e`, `b2f2635`, `035164d`, `7ca99ea`, `b62fb03`, `73d2486`, `7151e0a`, `926ae89`, `1b45f04` (os testes falharam primeiro, 7 de 30: no Linux o `GameGamebryo::parseSteamLocation()` não olhava nas bibliotecas da Steam e agora usa o módulo de descoberta (`steam::findApp`), o que corrigiu o Starfield e tornou detectáveis o Fallout 3 GOTY e o Fallout 76; Skyrim VR, Fallout 4 VR, Enderal, Fallout New Vegas (inclusive a edição `enplczru`) e Enderal SE tinham um `identifyGamePath()` que só lia o registro do Windows e agora também olham nas bibliotecas da Steam no Linux; no Enderal SE o `initializeProfile()` junta as pastas do AppData e do jogo com "/" no Linux, onde o upstream as concatenava sem separador; a tabela dos testes não precisou de mudança, o My Games já estava no prefixo certo nos dez jogos) |
| 4 | Documentação, checklist e ponto de controle de CI | ✅ README (seção "Ferramentas"), checklist, ROADMAP e STATE; o push e o PR dependem da revisão final e da autorização do usuário | este commit |
| 5 | Teste manual no Bazzite ([`docs/testing/phase4c3-manual-checklist.md`](docs/testing/phase4c3-manual-checklist.md)) | ⚪ pendente | — |

## Fase 4c.2 — progresso

Spec: [`docs/superpowers/specs/2026-10-20-phase4c2-pendencias-design.md`](docs/superpowers/specs/2026-10-20-phase4c2-pendencias-design.md) · Plano: [`docs/superpowers/plans/2026-10-20-phase4c2-pendencias.md`](docs/superpowers/plans/2026-10-20-phase4c2-pendencias.md) · Checklist manual: [`docs/testing/phase4c2-manual-checklist.md`](docs/testing/phase4c2-manual-checklist.md)

| # | Tarefa | Status | Commits principais |
|---|---|---|---|
| 1 | Timeouts do VFS testáveis: `MO2_VFS_TIMEOUT_MS` só encurta o timeout do mount (R2) | ✅ | `0060c51` |
| 2 | Reconferência assíncrona das raízes deixadas montadas (R1) | ✅ | `5090625` |
| 3 | Helper travado com programa observado: a raiz fica como montada e o helper só é morto quando o mount some (P7) | ✅ | `9460c09` |
| 4 | Sem tela, mensagem clara e código de saída 1 antes da `QApplication` (P1) | ✅ | `54aa961`, `d129e7c` (smoke "without a display") |
| 5 | INIs de arquivos e da invalidação de BSA ignorando maiúsculas (I1, I2) | ✅ | `d385b5a` |
| 6 | Documentação, checklist e ponto de controle de CI | ✅ docs e checklist (pendências P2 a P5 da Fase 2, nota de SSH no README); o ponto de controle de CI, o push e o PR dependem da revisão final e da autorização do usuário | este commit |
| 7 | Teste manual no Bazzite ([`docs/testing/phase4c2-manual-checklist.md`](docs/testing/phase4c2-manual-checklist.md)) | ⚪ pendente | — |

## Fase 4c.1 — progresso

Spec: [`docs/superpowers/specs/2026-10-20-phase4c1-cpp-plugins-design.md`](docs/superpowers/specs/2026-10-20-phase4c1-cpp-plugins-design.md) · Plano: [`docs/superpowers/plans/2026-10-20-phase4c1-cpp-plugins.md`](docs/superpowers/plans/2026-10-20-phase4c1-cpp-plugins.md) · Checklist manual: [`docs/testing/phase4c1-manual-checklist.md`](docs/testing/phase4c1-manual-checklist.md)

| # | Tarefa | Status | Commits principais |
|---|---|---|---|
| 1 | `tests/plugin-load`: tipos de plugin `preview`, `tool`, `diagnose` e `plugin`; configurações | ✅ | `3ce2c41` |
| 2 | `installer_bain` e `installer_bundle` (subtrees + alvos no `build.sh`) | ✅ | `3bdecfa` |
| 3 | `preview_base` e `preview_bsa` | ✅ | `9f2d3d4`, `b4c7567` (`bsatk` com código independente de posição, necessário para ligá-lo aos plugins), `778402f` |
| 4 | `bsa_extractor` | ✅ | `e992baa`, `32fcbe9` |
| 5 | `diagnose_basic` (sem a checagem de atributos NTFS no Linux) | ✅ | `819f84e`, `64fd32a` |
| 6 | `check_fnis` | ✅ | `2241fd5`, `6ca8659`, `9ec0e72` (correção de um bug do upstream: `waitForApplication` recebia o ponteiro do código de saída como `refresh`) |
| 7 | `tool_inieditor` e `tool_inibakery` | ✅ | `ce3c4b4`, `fb7b1a2` (o `tool_inibakery` não precisou de patch) |
| 8 | Smoke: todo plugin instalado é carregado pelo aplicativo | ✅ | `136e471` |
| 9 | Documentação e checklist | ✅ docs e checklist; o push e o PR dependem da revisão final e da autorização do usuário | este commit |
| 10 | Correções da revisão final: INIs do perfil ignorando maiúsculas e reparo dos INIs vazios (`game_bethesda`), pasta `__MO_Saves` dos saves locais, `Profile::absoluteIniFilePath()` ignorando maiúsculas (app), separadores nos padrões do `check_fnis` e na árvore do `preview_bsa`, docs e checklist (itens 8 a 10) | ✅ | `62fe2cf`, `9d6b312`, `36c9bc5`, `0ecbf34`, `0d45c7e` |
| 11 | Teste manual no Bazzite ([`docs/testing/phase4c1-manual-checklist.md`](docs/testing/phase4c1-manual-checklist.md)) | ⚪ pendente | — |

## Fase 4b — progresso

Spec: [`docs/superpowers/specs/2026-10-18-phase4b-loot-design.md`](docs/superpowers/specs/2026-10-18-phase4b-loot-design.md) · Plano: [`docs/superpowers/plans/2026-10-18-phase4b-loot.md`](docs/superpowers/plans/2026-10-18-phase4b-loot.md) · Checklist manual: [`docs/testing/phase4b-manual-checklist.md`](docs/testing/phase4b-manual-checklist.md)

| # | Tarefa | Status | Commits principais |
|---|---|---|---|
| 1 | `mo2-vfs`: pedido `kill` | ✅ | `9b6e8c3` |
| 2 | Build do libloot 0.29.5 | ✅ | `a91371c`, `8a14c1d` |
| 3 | `lootcli`: pasta de dados e download com libcurl | ✅ | `d371e8e`, `3a7a7fe` |
| 4 | `lootcli`: executável no Linux | ✅ | `2c44af7`, `81a5292` |
| 5 | `tests/loot-e2e` | ✅ | `cce9fb2`, `8b09add` |
| 6 | `VfsConnector::kill` | ✅ | `31c0d76` |
| 7 | Helpers `loot_linux` | ✅ | `310d9b0` |
| 8 | LOOT roda o `lootcli` pelo `mo2-vfs` | ✅ | `928be58`, `37804a3` (o segundo corrige um bug do upstream em `Loot::processStdout`: uma linha não reconhecida repetia para sempre) |
| 9 | Documentação, checklist e ponto de controle de CI | ✅ docs e checklist; o push e o CI dependem da autorização do usuário | `6aade61` |
| 10 | Teste manual no Bazzite ([`docs/testing/phase4b-manual-checklist.md`](docs/testing/phase4b-manual-checklist.md)) | ⚪ pendente | — |

## Fase 4a — progresso

Spec: [`docs/superpowers/specs/2026-10-18-phase4a-nexus-nxm-keyring-design.md`](docs/superpowers/specs/2026-10-18-phase4a-nexus-nxm-keyring-design.md) · Plano: [`docs/superpowers/plans/2026-10-18-phase4a-nexus-nxm-keyring.md`](docs/superpowers/plans/2026-10-18-phase4a-nexus-nxm-keyring.md) · Checklist manual: [`docs/testing/phase4a-manual-checklist.md`](docs/testing/phase4a-manual-checklist.md)

| # | Tarefa | Status | Commits principais |
|---|---|---|---|
| 1 | Build: `libsecret`, `gnome-keyring` e `dbus-daemon` no container; wrapper do Secret Service | ✅ | `1504f94`, `7eb0f49` |
| 2 | MO2: credenciais (Nexus e Steam) guardadas no Secret Service | ✅ | `ab639fb` |
| 3 | MO2: arquivo `.desktop` e decisão do handler `nxm` | ✅ | `16ccd1c` |
| 4 | MO2: registro do handler `nxm` com `xdg-mime` | ✅ | `439f936` |
| 5 | MO2: o Mod Organizer se registra para links `nxm://` | ✅ | `f67c374` |
| 6 | Smoke: um link `nxm://` de uma segunda invocação chega ao aplicativo em execução | ✅ | `a5d0495` |
| 7 | Documentação, checklist manual e ponto de controle de CI | ✅ docs, checklist e ponto de controle de CI; o push e o CI dependem da autorização do usuário | `7fb33a5` |
| — | Revisão final do branch + rodada de correções (id próprio do `.desktop`, `org.modorganizer.ModOrganizer2.nxm.desktop`, para não colidir com o instalador Wine do MO2; smoke e e2e sem o keyring real; aviso de login sempre enfileirado e `errno` nas falhas; `secret-service.sh` exige `PASSED`; checklist e docs) | ✅ | `08371e2`, `ea5d0b7`, `d0ba64f`, `2a0b8c4` |
| 8 | Teste manual no Bazzite ([`docs/testing/phase4a-manual-checklist.md`](docs/testing/phase4a-manual-checklist.md)) | ✅ aprovado pelo usuário em 2026-10-18 (itens 1–7, sem correções) | — |

## Fase 3 — progresso

✅ Concluída: teste manual aprovado e merge no `main` em 2026-10-18 (PR #1).

Spec: [`docs/superpowers/specs/2026-10-17-phase3-steam-proton-design.md`](docs/superpowers/specs/2026-10-17-phase3-steam-proton-design.md) · Plano: [`docs/superpowers/plans/2026-10-17-phase3-steam-proton.md`](docs/superpowers/plans/2026-10-17-phase3-steam-proton.md) · Checklist manual: [`docs/testing/phase3-manual-checklist.md`](docs/testing/phase3-manual-checklist.md)

| # | Tarefa | Status | Commits principais |
|---|---|---|---|
| 1 | `mo2-vfs`: pedido `spawn`, evento `exited`, sessão própria, stdout fechado | ✅ | `b1bdb44`, `330636d` |
| 2 | `mo2-vfs`: espera do `wineserver`, `status.processes`, `--list-mounts` | ✅ | `0897e00` |
| 3 | Ponto de controle: SKSE real no Bazzite (manual, com o usuário) | ✅ 2026-10-18: Proton Experimental via `umu-run`, `mo2-vfs` do branch rodando direto no host; `skse64_loader.exe` iniciou o `SkyrimSE.exe` (1.7.104, SKSE 2.3.1) e saiu; SKSE carregou pelo `Data` montado e respondeu a `getskseversion`; `exited` do `umu-run` (0) e `unmounted` 0,09 s depois, jogo já fechado; o grupo do `umu-run` cobriu o relançamento (a espera do `wineserver` foi só rede de segurança); a 1ª execução baixou o runtime `steamrt4` (~18 s). Spec §1.2 | — |
| 4 | `uibase`: parser de VDF público, `libraryfolders.vdf` refeito sobre ele | ✅ | `fa72f90` |
| 5 | `uibase`: descoberta da Steam (raízes, jogos, prefixos, Protons) | ✅ | `0251d4e`, `4fd8ec4` |
| 6 | `game_bethesda`: "My Games" e AppData dentro do prefixo | ✅ | `f00e082`, `6f08cd3` |
| 7 | MO2: ponte para o host (`vfsbridge_linux`, `MO2_VFS_BRIDGE`) | ✅ | `fbfc624`, `13e61cb` |
| 8 | MO2: `VfsConnector::spawn()`, `runningPrograms()`, códigos de saída, um `update` por execução | ✅ | `b710b90`, `898d9af` |
| 9 | MO2: `ProtonLauncher` (argv/env/cwd do lançamento) | ✅ | `674d40e`, `8ff9efa` |
| 10 | MO2: Proton e variáveis por executável (`ExecutablesList`, diálogo) | ✅ | `cee5347` |
| 11 | MO2: "Executar" pelo helper (`processrunner_linux.cpp`) | ✅ | `af36b65`, `c204628` |
| 12 | e2e: "Executar" de um `.exe` com um `umu-run` falso | ✅ | `8bcb72c`, `a7ce0c4` |
| 13 | Documentação, checklist manual e fechamento | ✅ docs (`docs: Phase 3 manual checklist, state and roadmap [windows-ci]`) | `6d9d166`, `10869e8` |
| — | Revisão final do branch + rodada de correções (mount deixado nunca é montado por cima, mesmo com helper novo do `spawn()`; tabela de mounts ilegível conta como montado; programa de uma ativação não roda sem o VFS se o helper se perdeu; docs do resultado da Task 3) | ✅ (CI verde e teste manual aprovado depois; ver o topo desta seção) | `79f9233`, `99d405b`, este commit |
| — | Teste manual (2026-10-18): SSEEdit sob Proton abortava com "Access denied" (código 5, plugins nunca carregavam) ao criar `Plugins.sseviewsettings` em `AppData/Local/Skyrim Special Edition`, raiz que só tem os mappings de arquivo do perfil e nenhuma camada de criação (`EROFS`). Corrigido: raiz só com mappings de arquivo grava na pasta real, como o USVFS (decisão de 2026-10-18); renomear sobre um destino de mapping de arquivo dá `EXDEV`. O pop-up "Not implemented" do SSEEdit é do Wine, não do MO2 (limitações conhecidas da Fase 3) | ✅ | `99ace0c`, `504bc0f` |
| — | Teste manual (2026-10-18): SSEEdit sob Proton abortava com "Error loading plugin list: EDirectoryNotFoundException" em `S:\steamapps\common\Skyrim Special Edition\Data\` (caminho que a Steam grava no registro do prefixo). Causa: o `S:` é o "game drive" do Proton, criado só com `STEAM_COMPAT_INSTALL_PATH` e `STEAM_COMPAT_LIBRARY_PATHS`, que o MO2 não passava (a Steam passa). Correção: o `ProtonLauncher` passa as duas, com caminhos canônicos como o host os vê (a pasta do jogo e o `steamapps` de cada biblioteca, a do jogo primeiro); verificado no sistema real, o `umu-run` cria `s:` → raiz da Steam e o jogo aparece em `S:\steamapps\common\`. Testes: 3 unitários (`WindowsExeArgv`, jogo em outra biblioteca, Steam por link simbólico) e o `probe-exe` do e2e | `deps/modorganizer` `ProtonLauncher`, `tests/vfs-e2e` |

### Critérios de pronto (spec da Fase 3, §10)

| Critério | Status |
|---|---|
| 1. Testes do §8 passam no CI Linux; CI Windows compilando | ✅ CI verde no PR #1 (merge em 2026-10-18); local: `./build.sh --tests` na distrobox com `MO2_REQUIRE_FUSE=1`, `cargo fmt --check` e `cargo clippy` verdes; Windows: o último commit do branch leva `[windows-ci]`. Desvio do §8: o teste unitário de `proton`/`environment` no `ExecutablesList` não existe (código do app fora do `organizer_tests`); coberto pelo `probe-exe` do e2e e por uma conferência da ida e volta pelo `QSettings` de valores com vírgula (registrado na spec) |
| 2. Checklist manual no Bazzite | ✅ [`docs/testing/phase3-manual-checklist.md`](docs/testing/phase3-manual-checklist.md) aprovado em 2026-10-18 (o ponto de controle do SKSE, Task 3, passou antes) |
| 3. `ROADMAP.md`, `STATE.md` e spec geral (§3.2) atualizados | ✅ (Task 13 e rodada de correções; §1.2 da spec com o resultado da Task 3) |

## Fase 2 — progresso

Spec: [`docs/superpowers/specs/2026-10-15-phase2-vfs-fuse-design.md`](docs/superpowers/specs/2026-10-15-phase2-vfs-fuse-design.md) · Plano: [`docs/superpowers/plans/2026-10-15-phase2-vfs-fuse.md`](docs/superpowers/plans/2026-10-15-phase2-vfs-fuse.md) · Checklist manual: [`docs/testing/phase2-manual-checklist.md`](docs/testing/phase2-manual-checklist.md)

| # | Tarefa | Status | Commits principais |
|---|---|---|---|
| 1 | Esqueleto do crate `mo2-vfs`, alvo de build, sonda FUSE no CI | ✅ | `34991ed` |
| 2 | Normalização de nomes e raízes de mapeamento | ✅ | `8b6d1eb`, `7485bc2`..`370b997` |
| 3 | Camadas e árvore mesclada | ✅ | `04c3472`..`c1d91df` |
| 4 | Operações de arquivo na árvore (whiteouts, rename) | ✅ | `209b234`..`ad670de` |
| 5 | FUSE: caminho de leitura | ✅ | `0917d57` |
| 6 | FUSE: caminho de escrita | ✅ | `4b5c5b2`..`4307085` |
| 7 | Sessão de mount, `update` a quente, estado de limpeza | ✅ | `ea5d96f`..`eddb032` |
| 8 | Protocolo de linhas, observação de grupos de processos, modo de limpeza | ✅ | `a33f468`, `ef742b3` |
| 9 | Benchmarks do VFS | ✅ | `7b51395`..`43d6640` |
| 9b | Desempenho (varredura paralela, readdir linear, cache do kernel, passthrough) | ✅ | `906077c`..`b902ef3` |
| 10 | `LinuxVfsConnector` (cliente do `mo2-vfs`) | ✅ | `c3e1cf9`, `338d9cd`..`696598d` |
| 11 | "Executar" de programa nativo com o VFS | ✅ | `e72723c`..`d3bc372` |
| 12 | `fileMapping()` extraído e testado (pendência da Fase 1) | ✅ | `94a5495`..`a29d5f0` |
| 13 | Teste ponta a ponta com programa nativo | ✅ | `b9b4f89` |
| 14 | Varredura de pastas: `EINTR`, links simbólicos (pendência) | ✅ | `56ee791` |
| 15 | FILETIME e INI consolidados (pendência) | ✅ | `db9fc72`..`6fcaa7b` |
| 16 | Guard de CI contra leituras de tamanho da plataforma nos saves (pendência) | ✅ | `332d016` (+ `bad2cf1`) |
| 17 | Smoke test até a janela principal (pendência) | ✅ | `75497c2`..`822d68c` |
| 18 | Documentação, checklist manual e fechamento | ✅ docs; revisão final do branch + rodada de correções (`7ded04e`..`1fee876`); checkpoint de CI | `3248e6f`..`1fee876` |

### Critérios de pronto (spec da Fase 2, §10)

| Critério | Status |
|---|---|
| 1. Testes da §8 passam no CI Linux; CI Windows continua verde | 🚧 Linux verde nos checkpoints: [#38828968997](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/38828968997) (sonda FUSE), [#39026042925](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/39026042925) (suíte Rust como root, protocolo, 53/53), [#39049120299](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/39049120299) (organizer 66/66, e2e); final do branch: Linux [#39067367446](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/39067367446) (tudo como root: Rust, organizer, smoke até a janela principal, e2e); Windows: commit `[windows-ci]` deste fechamento |
| 2. Checklist manual no Bazzite | ✅ aprovado pelo usuário em 2026-10-17 (instância `fase2-teste`, Skyrim SE real, terminal de teste `xterm`); itens 0–8 ok, com as observações em "Pendências por fase → Antes do merge / Fase 2" |
| 3. Metas de desempenho registradas | ✅ [`benchmarks/results/2026-10-phase2-vfs.md`](benchmarks/results/2026-10-phase2-vfs.md): mount 916 ms (primeira execução a frio: 2972 ms), update 906 ms, `readdir` 0,84x do nativo, `open`+`read` 3,36x (meta de 2x **não atingida**: um round trip de OPEN por arquivo; passthrough recusado sem `CAP_SYS_ADMIN`) |
| 4. Pendências da §9 resolvidas | ✅ (Tasks 12 a 17) |
| 5. `ROADMAP.md`, `STATE.md` e spec geral atualizados | ✅ (esta tarefa) |

## Fase 1 — progresso

| # | Tarefa | Status |
|---|---|---|
| 1 | Dependências em `deps/` via subtree | ✅ |
| 2 | Container, `build.sh`, CI Linux | ✅ |
| 3 | `cmake_common` com GCC | ✅ |
| 4 | 7-Zip 26.01 como `7z.so` | ✅ |
| 5 | `archive` no Linux | ✅ |
| 6 | `uibase` no Linux | ✅ |
| 7 | `bsatk`, `esptk`, `lootcli`, `dds-header` | ✅ |
| 7b | Dependências refixadas no `master` do upstream | ✅ |
| 8 | CMake Linux do app + CI Windows | ✅ |
| 9 | Varredura de pastas nativa + árvore de dados | ✅ |
| 10 | Processos, shell, atalhos `.desktop`, métricas | ✅ |
| 11 | `VfsConnector` + execução desativada | ✅ |
| 12 | Compilação completa + smoke test | ✅ |
| 13 | Plugins `game_bethesda` + instaladores | ✅ |
| 14 | Benchmark de refresh (linha de base) | ✅ |
| 15 | Checklist manual + README | ✅ |
| — | Revisão final do branch + correções | ✅ |

### Critérios de pronto (spec §4.8)

| Critério | Status |
|---|---|
| 1. `build.sh` gera o app a partir de container limpo | ✅ (CI Linux) |
| 2. Checklist manual aprovado no Bazzite | ✅ aprovado pelo usuário em 2026-10-15 |
| 3. CI Linux e Windows verdes | ✅ Linux [#27](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/38806338290)/[#28](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/38807459345); Windows [#15](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/38810512689) (fork `bb46b18` contra deps do upstream — ver pendência dos `deps/`) |
| 4. Linha de base de desempenho registrada | ✅ [`benchmarks/results/2026-10-phase1-baseline.md`](benchmarks/results/2026-10-phase1-baseline.md) |
| 5. Nenhum `windows.h` fora de código só-Windows | ✅ |

## Pacote para o teste manual

Fases 4b a 4d.2 (PRs #3 a #8, empilhados): um só build cobre todas, porque cada branch contém os anteriores. O pacote é a compilação local do worktree `../ModOrganizerLinux-4d2` (`./build.sh` na distrobox `mo2-run`, resultado em `install/` do worktree, com o `mo2-vfs` em `install/bin/` e o `lootcli` em `install/bin/loot/`), iniciada pelo box (com `python3 python3-pyqt6`); ou o artefato `mo2-linux-install` de um run Linux verde do PR #8 (restaure a permissão de execução de `bin/ModOrganizer` e `bin/mo2-vfs`). Ordem dos checklists: `docs/testing/phase4b-manual-checklist.md`, `phase4c1-…`, `phase4c2-…`, `phase4c3-…`, `phase4d1-…` e `phase4d2-…` (NieR:Automata, Cyberpunk 2077 e Baldur's Gate 3 rodados ao menos uma vez pela Steam, para o prefixo do Proton existir).

(Fase 4a, já no `main`: checklist em `docs/testing/phase4a-manual-checklist.md`.)

(Fase 3, já no `main`: checklist em `docs/testing/phase3-manual-checklist.md`.)

(Fase 2: use o artefato `mo2-linux-install` do último run Linux verde do branch `main`; ele traz o `mo2-vfs` ao lado do executável. Checklist em `docs/testing/phase2-manual-checklist.md`.)

Fase 1: Artefato `mo2-linux-install` da [execução Linux #28](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/38807459345) (passo a passo no README, seção Bazzite; checklist em `docs/testing/phase1-manual-checklist.md`).

## Próximos passos

1. CI do PR #8 (Linux e Windows pelo `[windows-ci]`); os PRs #3 a #7 estão com os dois CIs verdes.
2. Testes manuais do usuário, na ordem 4b → 4c.1 → 4c.2 → 4c.3 → 4d.1 → 4d.2, com o build da 4d.2 (seção acima) e **com o MO2 iniciado pelo box**.
3. Merges no `main` na ordem PR #3 → #8, cada PR redirecionado ao `main` depois do merge do anterior; as specs passam a "aprovada; implementada".
4. Fase 4e (criação de BSA/BA2) e Fase 5 (distribuição).
5. Resolver a pendência de compilar os `deps/` com nossos patches no Windows.

## Limitações conhecidas da Fase 4d.2

- Só a Steam: GOG, Epic, EA Desktop e Origin não existem no Linux; as lojas devolvem `{}` e o log registra uma vez que só a Steam é suportada.
- Só NieR:Automata, Cyberpunk 2077 e Baldur's Gate 3 foram validados (fixtures e, depois do teste manual, no Bazzite). Os demais jogos do `basic_games` ficam "detectados, não validados". Lacunas conhecidas: `game_oblivion_remaster.py` monta a pasta do usuário por `QStandardPaths` (documentos e saves ficam fora do prefixo); os jogos que leem `os.getenv("LOCALAPPDATA")` ou `os.path.expandvars("%APPDATA%")` (`game_thug2`, `game_msfs2020`, `game_subnautica`) não são mapeados para o prefixo.
- **Ordem de carga do BG3 (`modsettings.lsx`) não é gerada no Linux:** o `Divine.dll` do LSLib rejeita caminhos Unix (spike da Task 1); nada é baixado nem executado e o usuário recebe um aviso uma vez por sessão (instalação e execução do jogo); as ferramentas "Check For LsLib Updates" e "Reparse Pak Metadata" avisam toda vez que são usadas. Um `modsettings.lsx` feito à mão no perfil continua sendo mapeado para o prefixo do jogo.
- **Mods com DLL proxy precisam de `WINEDLLOVERRIDES` por executável:** CET e RED4ext (Cyberpunk 2077, `version.dll`/`winmm.dll`), o Script Extender do BG3 (`DWrite.dll`) e o FAR do NieR:Automata (`dinput8.dll`) dependem de bibliotecas forçadas (`executableForcedLoads`; no BG3, a opção `force_load_dlls`, ligada por padrão), que o Linux ignora; o Wine usa a DLL própria em vez da do jogo. As opções de inicialização da Steam não valem (o MO2 inicia o jogo pelo Proton). Em **Tools → Modify Executables…**, no grupo **Proton**, linha **Environment**, escrever `WINEDLLOVERRIDES=<dll>=n,b` (`version,winmm=n,b`, `DWrite=n,b`, `dinput8=n,b`). Não há conversão automática das bibliotecas forçadas em `WINEDLLOVERRIDES` (trabalho do lançador do núcleo, fora da 4d.2); o checklist traz um passo opcional.
- Stellar Blade não tem plugin no `basic_games`.
- O caminho `Z:` do `-modlist` do `redMod.exe` do Cyberpunk 2077 não é canonicalizado para o host: funciona porque o `/home` do host é `/var/home` (o mesmo diretório visto de dentro do box); um caminho privado do box não valeria para o Proton do host.
- O `dotnet-runtime-8.0` foi instalado no box `mo2-run` só para o spike do LSLib e não é necessário (`sudo dnf remove dotnet-runtime-8.0`); a imagem do container não mudou.
- O código Windows do `basic_games` não é compilado nem executado no CI.
- Valem as limitações das Fases 1 a 4d.1.

## Limitações conhecidas da Fase 4d.1

- Os plugins Python só funcionam onde o Python 3.14 e o PyQt6 do Fedora existem: o box `mo2-run` com `python3 python3-pyqt6`. O Python e o PyQt6 dentro do pacote ficam para o AppImage da Fase 5. Sem os pacotes (ou com outra versão do Python no box), o MO2 abre sem os plugins Python e o painel de diagnóstico mostra "Python could not be loaded" com o comando `sudo dnf install python3 python3-pyqt6`; se o próprio `libpython3.14` falta, o aviso diz também que o build espera o Python 3.14 (outra versão exige recompilar o MO2). O proxy carrega o `libpython` e o runner em tempo de execução, por isso o aviso aparece em vez de uma falha de carga do `plugin_python.so` (verificado na revisão final com um proxy descartável apontando para um `libpython` inexistente).
- Pacotes Python RPM do box podem encobrir as dependências que um plugin fixa pelo `pip` na própria pasta: o `installer_wizard` acrescenta o seu `lib/` ao fim do `sys.path` (`site.addsitedir`), então um `python3-antlr4-runtime` ou `python3-chardet` instalado no box teria precedência. Hoje nenhum dos dois está no box; o `antlr4` do Fedora tem a mesma versão (4.13.2), o `chardet` não (6.0 contra 5.2.0 fixado pelo plugin).
- Um locale C/POSIX (sem UTF-8) não é tratado nem testado (o proxy passa os caminhos ao Python como os bytes do sistema de arquivos, supostos UTF-8); o box usa `pt_BR.UTF-8`.
- Não há teste automático de um fechamento limpo do MO2 com o Python carregado (o smoke encerra o MO2 por `SIGTERM`); é o passo manual do item 1 do checklist (fechar pelo menu, `crash.log` vazio).
- O Python do MO2 é isolado: não lê `PYTHONPATH` nem as variáveis `PYTHON*` e não enxerga `~/.local` nem `/usr/local`; plugins que dependam de pacotes do usuário não os acham (as dependências próprias de um plugin vão para a pasta dele, pelo `pip install --target`, e o build precisa de rede para isso).
- Os patches dos plugins Python (fnistool, installer_wizard, script_extender_plugin_checker, tool_configurator) só são testados pelo carregamento (smoke); o comportamento em execução é coberto pelo checklist manual.
- `KnownFolder` (módulo `mobase`) tem 10 pastas no Linux, as de `win32compat.h`; um plugin que use as demais recebe `AttributeError`.
- O código Windows destes subtrees não é compilado no CI (o CI do Windows só compila `deps/modorganizer`).
- Uma segunda inicialização do proxy depois de uma falha na inicialização do Python no mesmo processo volta sem o `mobase` (o desenho do upstream: "never re-initialize").
- O `basic_games` não fez parte desta fase (ver a Fase 4d.2).
- Valem as limitações das Fases 1 a 4c.3.

## Limitações conhecidas da Fase 4c.3

- Com o mod do binário desativado, o programa roda da pasta do mod, com um aviso no log (o Nemesis então mostra o erro 1008).
- Execuções sem o sistema de arquivos virtual rodam o programa da pasta do mod, sem a reescrita dos caminhos.
- O Pandora 5.0.0-beta ignora o argumento `-o`: a saída vai para o `overwrite`.
- O xEdit de outros jogos só tem cobertura por fixtures (`SteamGames`): nenhum outro jogo Bethesda foi testado de verdade.
- O jogo precisa ter rodado uma vez pela Steam para o prefixo e o registro `Installed Path` existirem.
- No Linux, Enderal e Enderal SE só são detectados com o loader do SKSE na pasta do jogo (é o `binaryName()` do plugin).
- O Fallout 3 GOTY (22370) é detectável, mas não está na tabela dos testes.
- Valem as limitações das Fases 1 a 4c.2.

## Limitações conhecidas da Fase 4c.2

- Um `DISPLAY` definido mas inalcançável (ex.: `:0` sem servidor X) ainda aborta na Qt ("could not connect to display"); só a falta das variáveis é detectada.
- Um helper que para de responder com programas rodando só é morto quando o mount dele some; se ele nunca desmontar, o mount fica até o usuário fechar os programas e usar `fusermount3 -uz` (o "Unlock" não o libera), e o refresh fica adiado até lá.
- No modo host (MO2 no box, o uso normal no Bazzite), matar o helper travado mata só o cliente `distrobox-host-exec`: o `mo2-vfs` do host só recebe EOF (spec da Fase 3, §1.2) e, se estiver parado (`SIGSTOP`) ou travado, continua vivo no host depois que o mount some, até ser encerrado à mão (`pkill -x mo2-vfs` no host).
- "Executar" sem VFS com um helper travado ainda descarta o helper travado sem matá-lo (o destrutor espera até 5 s e o deixa rodando), como antes.
- Valem as limitações das Fases 1 a 4c.1.

## Limitações conhecidas da Fase 4c.1

- A checagem de atributos de arquivo NTFS (oculto, sistema, comprimido, esparso) do `diagnose_basic` não existe no Linux: a opção `check_fileattributes` não é registrada.
- O modo externo do `tool_inieditor` abre o INI com o `xdg-open` e ignora a opção "associated" (abrir × editar); sem `xdg-open` a falha só é registrada no log.
- ~~A pré-visualização de DDS fica indisponível até o plugin de pré-visualização DDS (Python, Fase 4d): o `preview_base` sempre pula `dds`.~~ Resolvido na Fase 4d.1 (`preview_dds`).
- O `check_fnis` só vale para o Skyrim (LE) (`gameDependency("Skyrim")`, como no upstream) e fica inativo no Skyrim SE; só foi exercitado pelo teste de carga, e a execução do FNIS pelo Proton depende de o usuário usar o FNIS no Skyrim LE (item 7 do checklist, opcional).
- Na primeira abertura do perfil com esta versão, os INIs do perfil que as versões anteriores criaram vazios (ou só com `[Launcher] bEnableFileSelection=1`) são substituídos por cópias dos INIs do prefixo (ou do INI padrão do jogo); um INI do perfil deixado vazio de propósito também é substituído.
- `installer_omod` e `installer_fomod_csharp` (C++/CLI e .NET) e o `bsapacker` (Fase 4e) ficam de fora.
- Valem as limitações das Fases 1 a 4b.

## Limitações conhecidas da Fase 4b

- As listas de plugins dos INIs (`sTestFile`, Oblivion/Fallout 3/NV) não são lidas fora do Windows (spec §1.3).
- Na distrobox o `lootcli` roda no host e usa as bibliotecas Qt6Core, Boost.Locale 1.90 e libcurl de lá (o `build.sh` confere a lista).
- Só o Skyrim SE foi testado.
- O portão do `build.sh` confere só os nomes das bibliotecas (`NEEDED`) do `lootcli`, não as versões dos símbolos: o `lootcli` compilado na distrobox exige `Qt_6.11` e `GLIBCXX_3.4.32` (versões do box hoje). Se o Qt ou o GCC do box passarem à frente dos do host Bazzite, o `lootcli` falha no host (o erro aparece no diálogo do LOOT); a Fase 5 (AppImage) elimina isso.
- Valem as limitações das Fases 1 a 4a, menos "Sem LOOT".

## Limitações conhecidas da Fase 4a

- Carteira trancada (KWallet/GNOME Keyring): a interface espera o prompt de desbloqueio do Secret Service (a libsecret é síncrona).
- Uma instalação do MO2 trata todos os links `nxm://` (a última registrada).
- Sem `modl://`: o botão das configurações gerais para links `modl` não faz nada no Linux (não há handler `modl://`).
- Com o MO2 fechado, um link `nxm://` abre a última instância usada.
- Sem Secret Service, as credenciais ficam só em memória até fechar o MO2 (aviso único; nada vai para o disco).
- Valem as limitações das Fases 1 a 3, menos "Logins ... só em memória" e o `nxm://` de "Sem LOOT, ...".

## Limitações conhecidas da Fase 3

- Jogo e ferramenta ao mesmo tempo no mesmo prefixo não são suportados (verbo `waitforexitandrun` do `umu`; a interface do MO2 fica travada durante a execução).
- O VFS e os programas rodam no host: programas que só existem dentro do container `mo2-run` não executam pelo MO2. Sem `distrobox-host-exec` no `PATH` (em container), "Executar" falha com a causa e não monta nada; `MO2_VFS_BRIDGE=direct` escolhe o helper dentro do container (testes e quem tem a Steam por dentro).
- `umu-launcher` precisa estar no host (sem fallback para `proton run`); sem ele, "Executar" mostra "install umu-launcher on the host".
- Jogos que nunca rodaram pela Steam não têm prefixo: rode o jogo uma vez pela Steam ("has no Proton prefix yet").
- O `umu` cria o link `pfx/drive_c/users/<usuário> → steamuser` no prefixo da Steam (comportamento normal do `umu`, inofensivo).
- O mount só desmonta depois que o `wineserver` do prefixo sai (cerca de 3 s após o jogo fechar); um prefixo travado mantém o mount até o `wineserver` sair ou o usuário usar "Unlock".
- Cache de diretórios do Wine após um `update` a quente do VFS: não testado.
- O `S:` do Proton (game drive) depende de o `umu-run` achar um ponto de montagem entre a pasta do jogo e `/`: ele só mantém a lista `STEAM_COMPAT_LIBRARY_PATHS` nesse caso. Biblioteca da Steam direto no sistema de arquivos raiz, fora de `/var/home` ou de outro mount, faz o `umu` descartar a lista e o `S:` não é criado (ferramentas como o SSEEdit, que leem o caminho do jogo do registro, não acham o jogo).
- SKSE real (`skse64_loader.exe`): verificado no ponto de controle da Task 3 com o `mo2-vfs` direto no host; pela ponte `distrobox-host-exec` (MO2 no box), só no teste manual. Só o Skyrim SE e o SSEEdit são o escopo testado; Flatpak e outros discos só com fixtures; demais jogos Bethesda só compilam; xEdit de outros jogos, BodySlide e Nemesis/Pandora ficam para a Fase 4.
- `.bat`/`.cmd` com caminho Unix contendo espaços em `umu-run cmd /c`: não verificado sob o Wine (checklist).
- A pasta "My Games" (e saves/AppData) é fixada quando o jogo é detectado, ao abrir a instância: um prefixo criado depois (jogo rodado pela Steam pela primeira vez com o MO2 aberto) só vale depois de reiniciar o MO2.
- SSEEdit sob Proton mostra um pop-up "Not implemented" ao abrir: é a limitação de OLE do RichEdit do Wine na janela de texto inicial (`EOleException` em `TfrmRichEdit.BuildTOC`), não o MO2; contorno: `protontricks 489830 riched20`.
- Valem as limitações das Fases 1 e 2, menos "Executar" só para programas nativos e "My Games"/AppData em pastas XDG (agora no prefixo, com volta às pastas XDG quando não há prefixo).

## Limitações conhecidas da Fase 2

(Fase 2; "Executar" de `.exe` e o mount invisível para a Steam do host foram resolvidos na Fase 3.)

- ~~"Executar" só funciona para programas Linux nativos; `.exe`/Proton continuam com o aviso da Fase 3.~~ (resolvido na Fase 3; teste manual aprovado em 2026-10-18)
- ~~O MO2 roda no distrobox `mo2-run`: o mount FUSE existe só no namespace de montagem do container e **não** é visto pela Steam do host. O `findmnt` do host não o mostra; confira dentro do box.~~ (resolvido na Fase 3: o mount é feito no host)
- Só há mount enquanto um programa roda (monta ao executar, desmonta quando o último grupo observado termina); a aba Data e o refresh continuam usando a árvore nativa.
- Enquanto há mount ativo, o refresh de mods fica adiado e roda quando o VFS desmonta; trocar de perfil nesse período mostra a lista nova com a árvore antiga até desmontar.
- `open`+`read` fica em cerca de 3,4x o nativo (passthrough exige `CAP_SYS_ADMIN`); primeira montagem a frio, com os metadados fora do cache do kernel, leva cerca de 3 s.
- Se o `mo2-vfs` morrer durante a execução, o MO2 avisa só ao fim da execução; um `kill -9` deixa o mount órfão até reabrir o MO2 (que limpa).
- `updateForcedLibraries`, `executableBlacklist`, `spawnDelay` e afins do USVFS são ignorados no Linux.
- Mantém as limitações da Fase 1 abaixo, menos "Sem VFS".

## Limitações conhecidas da Fase 1

- (Fase 1) Sem VFS: resolvido na Fase 2 para programas nativos e na Fase 3 para jogos Proton.
- ~~"Executar" mostra aviso; jogos e ferramentas não iniciam~~ (resolvido na Fase 3; teste manual aprovado em 2026-10-18).
- ~~Sem LOOT~~ (resolvido na Fase 4b), sem criação de BSA, sem atualização automática, sem plugins Python, ~~sem `nxm://`~~ (resolvido na Fase 4a, pendente do teste manual) (Fase 4/5).
- ~~Logins do Nexus/Steam ficam só em memória (perdidos ao fechar)~~ (resolvido na Fase 4a: Secret Service; sem ele, só em memória).
- ~~"My Games"/"AppData" apontam para pastas XDG~~ (Fase 3 leva para o prefixo do Proton; sem prefixo, volta às XDG).
- Lixeira só funciona no mesmo sistema de arquivos da home; em outro disco a remoção falha sem apagar nada.
- Roda dentro de uma distrobox Fedora 44 (AppImage na Fase 5).

## Decisões tomadas

| Data | Decisão |
|---|---|
| 2026-10-14 | Ferramenta pública; código multiplataforma (Windows continua compilando) |
| 2026-10-14 | VFS = FUSE próprio em Rust; o restante do MO2 continua em C++ |
| 2026-10-14 | Jogos iniciados direto via Proton (`umu-run`); modificar arquivo de mod altera o original (como USVFS) |
| 2026-10-14 | Desktop Linux primeiro (Bazzite); Steam Deck depois; distribuição em AppImage |
| 2026-10-14 | Dependências do upstream em `deps/` via `git subtree`, acompanhando o `master` do upstream |
| 2026-10-15 | CI só em pontos de controle; CI Windows roda em commits com `[windows-ci]` ou manualmente |
| 2026-10-15 | Fase 1 concluída: teste manual aprovado no Bazzite |
| 2026-10-15 | Um repositório só: o fork `modorganizer` vira o subtree `deps/modorganizer` (commits do fork reaplicados) |
| 2026-10-15 | VFS como processo auxiliar (`mo2-vfs`), não biblioteca: sem Corrosion/`cxx`/`cbindgen`, o CMake do app não compila Rust; falha do VFS não derruba o MO2 |
| 2026-10-15 | Comunicação por JSON (uma linha por mensagem) em stdin/stdout do processo auxiliar, em vez de socket |
| 2026-10-15 | Arquivos originais do jogo protegidos: nunca apagados nem renomeados pelo VFS (excluir esconde até desmontar; renomear copia para a camada de criação); editar conteúdo continua alterando o original |
| 2026-10-15 | Fase 2 demonstra "Executar" com programas Linux nativos; `.exe`/Proton ficam para a Fase 3 |
| 2026-10-16 | Refresh de mods adiado enquanto há mount ativo (a árvore nativa mudaria por baixo do programa); roda ao desmontar |
| 2026-10-16 | Monta só ao executar e desmonta sozinho quando o último grupo de processos observado termina; limpeza de órfãos ao abrir o MO2 |
| 2026-10-17 | Metas de mount/update contam com cache de metadados quente; o valor a frio (~3 s) é registrado ao lado. Meta de `open`+`read` (2x) aceita como não atingida, com causa medida |
| 2026-10-17 | MO2 segue no distrobox `mo2-run` até a Fase 5; mounts de lá não chegam à Steam do host, então a Fase 3 deve rodar o `mo2-vfs` no host (`distrobox-host-exec`) ou antecipar o AppImage |
| 2026-10-17 | Fase 3: VFS e programas rodam **no host**; dentro do container `/home` e `/var/home` são mounts separados e o Wine só vê o VFS pelo mesmo caminho do mount (spike, spec §1.2). Programas que só existem no container deixam de poder ser executados pelo MO2 |
| 2026-10-17 | Fase 3: o `mo2-vfs` inicia o programa (pedido `spawn`, evento `exited`), em vez de só observar um pid: quem cria o processo o observa, e o grupo certo é acompanhado antes de qualquer corrida |
| 2026-10-17 | Fase 3: `umu-run` obrigatório, sem fallback para `proton run`; verbo padrão `waitforexitandrun`, então jogo e ferramenta no mesmo prefixo ao mesmo tempo não são suportados |
| 2026-10-17 | Fase 3: o mount só desmonta quando os grupos acompanhados terminaram **e** o `wineserver` do prefixo saiu (achado pelo `cwd` em `/tmp/.wine-<uid>/server-<dev>-<ino>`), por causa de programas que se relançam como o `skse64_loader.exe` |
| 2026-10-17 | Fase 3: o `umu` cria o link `<usuário> → steamuser` no prefixo da Steam; aceito (inofensivo), documentado no checklist |
| 2026-10-18 | Fase 3: `MO2_VFS_BRIDGE=auto\|direct\|host` escolhe onde roda o helper; em container sem `distrobox-host-exec` o MO2 não cai de volta para o mount no container (falha com a causa); `direct` é o que os testes automáticos e o e2e usam |
| 2026-10-18 | Fase 3: os argumentos do executável são divididos como a linha de comando do Windows (`QProcess::splitCommand`): aspas duplas agrupam e aspas simples são literais (um apóstrofo como em `Assassin's` não abre aspas) |
| 2026-10-18 | Fase 3: `compatToolName(app)` usa primeiro o `config.vdf` da instalação da Steam dona do jogo (entrada do appid, depois a `0`) e só depois as outras raízes; quem tem Steam nativa e Flatpak recebe o Proton da instalação certa |
| 2026-10-18 | Fase 3 (revisão final): um mount deixado por um helper perdido é limpo antes de qualquer mount novo, e nunca montado por cima, mesmo quando um "Executar" sem VFS iniciou um helper novo; tabela de mounts ilegível (`--list-mounts` falhou) conta como "ainda montado"; o programa de uma ativação não é iniciado se o helper se perdeu depois dela (erro em vez de rodar sem o VFS) |
| 2026-10-18 | Fase 3: o teste unitário de `proton`/`environment` no `ExecutablesList` previsto na spec (§8) ficou de fora (código do app fora do `organizer_tests`); coberto pelo `probe-exe` do e2e e por uma conferência da ida e volta pelo `QSettings` |
| 2026-10-18 | Fase 3: o aviso de "sem prefixo" no `game_bethesda` só vale para jogo instalado numa biblioteca da Steam e sem prefixo (appid vazio: sem busca nem log; app não instalado: `debug`); o custo de reler os arquivos da Steam a cada chamada foi aceito, sem cache |
| 2026-10-18 | Fase 3 (teste manual): numa raiz só com mappings de arquivo (ex.: `AppData/Local/<jogo>` com `plugins.txt`/`loadorder.txt`, e as raízes de "My Games" no mesmo caso), a pasta real age como camada de criação, como o USVFS: criar/`mkdir` vão para ela, excluir apaga de fato e renomear move dentro dela. Toda raiz com mapping de diretório mantém a proteção dos originais (decisão de 2026-10-15), mesmo sem camada de criação (o MO2 omite um `overwrite/` apagado; o `Data` continua `EROFS` então). Renomear sobre um destino de mapping de arquivo dá `EXDEV` (a cópia+exclusão grava no arquivo do perfil) |
| 2026-10-18 | Fase 3 (teste manual): o MO2 passa `STEAM_COMPAT_INSTALL_PATH` (pasta do jogo) e `STEAM_COMPAT_LIBRARY_PATHS` (`steamapps` de cada biblioteca, a do jogo primeiro) ao `umu-run`, como a Steam, para o Proton criar o `S:` que o registro do prefixo usa. Caminhos canônicos **como o host os vê** (`vfsbridge::hostCanonicalPath`, por `/run/host` quando o MO2 roda no distrobox, onde `/home` é mount e não link): o `umu` mantém a lista só se achar um ponto de montagem acima do caminho do jogo, e no host do Bazzite `/home` é link para `/var/home`. A primeira versão (`canonicalFilePath` dentro do box) não bastou no teste manual |
| 2026-10-18 | Fase 3: teste manual aprovado pelo usuário no Bazzite (MO2 no box `mo2-run` com a ponte `distrobox-host-exec`): Skyrim SE + SKSE e SSEEdit rodando com mods. Merge no `main` depois do checkpoint de CI (Linux + Windows) |
| 2026-10-18 | Fase 4a: teste manual aprovado pelo usuário no Bazzite (MO2 no box `mo2-run`, Firefox Flatpak, KWallet): itens 1–7 do checklist sem correções. Merge no `main` depois do checkpoint de CI (Linux + Windows) |
| 2026-10-18 | Fase 4a (checkpoint de CI): como root num container sem `CAP_IPC_LOCK` (o docker do CI), o `gnome-keyring-daemon` aborta ("error dropping process capabilities"); o `secret-service.sh` o inicia por `setpriv --inh-caps=-all --bounding-set=-all` quando roda como root. CI Linux (com `secret_service_integration` passando de fato) e Windows verdes; merge no `main` (PR #2) |
| 2026-10-18 | Fase 4b: libloot 0.29.5 compilado do código-fonte (tag, commit fixado); o `lootcli` enxerga os mods pelo `mo2-vfs` (`VfsConnector::spawn()`), no host quando o MO2 está na distrobox; pasta de dados do LOOT em Flatpak (`~/.var/app/io.github.loot.loot/data/LOOT`) se existir, senão `$XDG_DATA_HOME/LOOT` / `~/.local/share/LOOT`; masterlist baixada com libcurl (arquivo temporário, só substitui com HTTP 200); cancelar pelo novo pedido `kill` do `mo2-vfs`; Windows sem mudanças (`#ifdef _WIN32` / `#else`). Spec §1.1 |
| 2026-10-18 | Fase 4b: corrigido em `loot.cpp` (compartilhado com o Windows) um laço infinito do upstream em `Loot::processStdout`: uma linha do `lootcli` não reconhecida nunca era consumida e travava a thread do LOOT |
| 2026-10-18 | Fase 4b: o `lootcli` é ligado com `-Wl,--as-needed` e usa o toml++ só de cabeçalhos, então precisa apenas de bibliotecas do host (o `build.sh` confere a lista) |
| 2026-10-20 | Fase 4c dividida em 4c.1 (plugins C++), 4c.2 (pendências da Fase 2) e 4c.3 (ferramentas pelo VFS), um PR cada, em sequência: `fase-4c1-plugins` parte de `fase-4b-loot` e o PR mira o branch anterior até o anterior entrar no `main` |
| 2026-10-20 | Fase 4c.1 (spec §1.2): plugins importados com `git subtree add --squash` no `master` do upstream, patches no superprojeto (`deps/<plugin>: Linux: …`); a lógica dos plugins não muda, só chamadas Win32 vão para `#ifdef _WIN32` / `#else`; `diagnose_basic` sem a checagem de atributos NTFS no Linux (opção `check_fileattributes` não registrada); `tool_inieditor` externo por `shell::Open` (`xdg-open`), sem efeito da opção "associated"; `installer_omod`, `installer_fomod_csharp`, `bsapacker` e plugins Python fora desta subfase |
| 2026-10-20 | Fase 4c.1: o `bsatk` passa a ser compilado com código independente de posição (PIC), para ser ligado aos plugins `preview_bsa` e `bsa_extractor` (bibliotecas compartilhadas) |
| 2026-10-20 | Fase 4c.1: corrigido no `check_fnis` um bug do upstream: `waitForApplication` recebia o ponteiro do código de saída no lugar do parâmetro `refresh` |
| 2026-10-20 | Fase 4c.1, revisão final (decisão do usuário, opção A): corrigir nesta fase os INIs do perfil no Linux. O `game_bethesda` acha os INIs do prefixo e o INI padrão ignorando maiúsculas (`GameGamebryo::resolveIgnoringCase()`, identidade no Windows) e restaura os INIs do perfil vazios ou só com `[Launcher] bEnableFileSelection=1` (nunca outro conteúdo) ao abrir o perfil (`onProfileChanged`, dentro do plugin, sem gancho novo no app) e antes de executar; o INI dos saves locais e a pasta `__MO_Saves` também; no app, `Profile::absoluteIniFilePath()` ignora maiúsculas no Linux |
| 2026-10-20 | Fase 4c.1 desenvolvida em um worktree separado (`../ModOrganizerLinux-4c1`), para não tocar o `install/` do checkout principal, de onde o MO2 do usuário está rodando |
| 2026-10-20 | Fase 4c.2: escopo (decisão do usuário): pendências 1–5 e 7 da Fase 2, follow-ups de INI da 4c.1 e as vizinhas R1/R2; raízes deixadas montadas reconferidas a cada 5 s por `--cleanup` + `--list-mounts` assíncronos (nunca desmonta mount de helper vivo); helper travado com programa observado deixa a raiz como montada e só é morto quando o mount some; sem tela, mensagem e código 1 antes da `QApplication`; INIs de arquivos e da invalidação de BSA resolvidos ignorando maiúsculas no ponto central. Desenvolvida no worktree `../ModOrganizerLinux-4c2` |
| 2026-10-21 | Fase 4c.3: escopo (decisão do usuário): ferramentas reais testadas no Skyrim SE; xEdit de outros jogos só com fixtures; spike antes da spec |
| 2026-10-21 | Fase 4c.3 (abordagem A): programa com binário ou pasta de trabalho em mods/ roda do Data virtual também no Linux, por reescrita dos caminhos antes do spawn (sem relançar o MO2), só com VFS e com o mod do binário ativo; regra comum ao Windows em virtualizedpaths.h |
| 2026-10-21 | Fase 4c.3 (testes da Task 3, falharam 7 de 30): no Linux o `GameGamebryo::parseSteamLocation()` passa a usar a descoberta da Steam (`steam::findApp`, sob `#ifndef _WIN32`), o que corrige a detecção do Starfield e torna detectáveis o Fallout 3 GOTY e o Fallout 76; Skyrim VR, Fallout 4 VR, Enderal, Fallout New Vegas e Enderal SE ganham um `identifyGamePath()` que também olha nas bibliotecas da Steam no Linux; no Enderal SE o `initializeProfile()` junta o AppData e a pasta do jogo com "/" no Linux (o Windows mantém a linha do upstream) |
| 2026-10-21 | Fase 4d dividida em 4d.1 (runtime `plugin_python` e os seis plugins Python pequenos) e 4d.2 (`basic_games`, com a detecção pela Steam e pelo Proton), um PR cada, empilhados como na 4c: `fase-4d1-python` parte de `fase-4c3-ferramentas` |
| 2026-10-21 | Fase 4d.1 (abordagem A, decisão do usuário): Python 3.14, pybind11 e PyQt6 do Fedora, na imagem do container e no box `mo2-run` (um só Qt no processo); o `pip` só instala dependências próprias de plugin na pasta do plugin. Além dos cinco pacotes previstos, o build precisa de `sip6` (gera o `sip.h` do `pybind11-qt`) e `gmock-devel` (testes do `plugin_python`); os pacotes foram instalados no box com o OK do usuário |
| 2026-10-21 | Fase 4d.1: isolamento com `PyConfig_InitIsolatedConfig` e `sys.path` filtrado por `filterSysPath` à biblioteca padrão do sistema, ao `lib-dynload` e ao `<base_prefix>/lib64\|lib/python3.14/site-packages`, mais as pastas do MO2; no Fedora o `sysconfig` aponta o `platstdlib` para `/usr/local`, então as raízes saem de `sys.base_prefix` (achado da Task 3); o teste de integração prova que pacotes do `~/.local` e do `PYTHONPATH` ficam invisíveis |
| 2026-10-21 | Fase 4d.1: o proxy carrega `libpython3.14.so.1.0` com `RTLD_GLOBAL` (os módulos de extensão do Fedora não se ligam ao `libpython`) e acha a pasta do plugin por `dladdr`; um Python que não carrega `mobase`/PyQt6 mostra "Python could not be loaded" com `sudo dnf install python3 python3-pyqt6` no painel de diagnóstico |
| 2026-10-21 | Fase 4d.1 (revisão final): no Linux o proxy não se liga ao runner; abre o `libpython` (`RTLD_GLOBAL`) e `plugin_python/dlls/librunner.so` (`RTLD_LOCAL`) por `dlopen` e cria o runner pela fábrica `extern "C"` `mo2_create_python_runner`, sem nunca fechar os handles (o interpretador não pode ser finalizado); qualquer falha é o aviso `DLL_NOT_FOUND` do painel. O Windows continua com o `/DELAYLOAD` |
| 2026-10-21 | Fase 4d.1: o pybind11 3 exigiu uma correção do caster de holder em `shared_cpp_owner.h`; o teste `test_filepath_wrappers` do upstream roda no Linux, com a versão de produto vazia esperada |
| 2026-10-21 | Fase 4d.1, patches dos plugins: o `fnistool` passa `RedirectFiles` como `Z:\…`; o `installer_wizard` abre o script com o desktop e usa caminhos nativos no wizard; o `script_extender_plugin_checker` converte os caminhos Windows dos plugins lidos no log (casando `/home` e `/var/home`, sem diferenciar maiúsculas); o `tool_configurator` acha os INIs sem diferenciar maiúsculas; o `cmake_common` cria o arquivo de sucesso do pip com `cmake -E touch` |
| 2026-10-21 | Fase 4d.2, escopo (decisão do usuário): base Steam/Proton para todos os jogos do `basic_games`, validada em NieR:Automata, Cyberpunk 2077 e Baldur's Gate 3; Stellar Blade fora (sem plugin) |
| 2026-10-21 | Fase 4d.2, abordagem A (decisão do usuário): o `steam::findApp()` do `uibase` é exposto como `mobase.utils.steamApp` (só no Linux), o `find_games()` da Steam é preguiçoso (consulta por app ID, não lista) e `%USERPROFILE%`/`%DOCUMENTS%` apontam para o prefixo do Proton do app ID do jogo (fallback XDG com aviso, uma vez por jogo) |
| 2026-10-21 | Fase 4d.2, spike do LSLib (Task 1) e adiamento (decisão do usuário): o `dotnet Divine.dll` falha no Linux (`TryToValidatePath` usa `System.Uri`, e um caminho Unix vira URI relativa: código 134 em toda ação), então a ordem de carga do BG3 fica para depois; no Linux o plugin do BG3 não baixa nem roda o LSLib e avisa o usuário uma vez por sessão; sem pacote novo (`dotnet-runtime-8.0` dispensável) |
| 2026-10-21 | Fase 4d.2: as compilações usam um `TMPDIR` por worktree (`TMPDIR=/tmp/mo2t`); execuções de smoke simultâneas com o mesmo `TMPDIR` colidiam na memória compartilhada da instância única do Qt (o antigo "erro de SHM") |

## Pendências por fase

**Fase 2 — encontradas no teste manual (2026-10-17), adiadas a pedido**
1. ~~Sem tela (`WAYLAND_DISPLAY`/`DISPLAY` vazios, ex. por SSH) o MO2 aborta com "has crashed" e backtrace; deveria dizer claramente que não há display e sair.~~ Resolvido na Fase 4c.2 (Task 4: mensagem e código de saída 1 antes da `QApplication`).
2. ~~Konsole como programa de teste precisa de `--separate --nofork`: sem `--separate` ele entrega a janela ao Konsole do host (D-Bus), sai na hora e o VFS desmonta. Corrigir o checklist e o exemplo de executável.~~ Resolvido na Fase 4c.2 (Task 6: checklist da Fase 2 com `--separate --nofork`).
3. ~~Checklist: rodar o MO2 por SSH exige `WAYLAND_DISPLAY`, `XDG_SESSION_TYPE`, `XDG_RUNTIME_DIR`, `DISPLAY` e `XAUTHORITY` (sem `XAUTHORITY` programas X11 como o xterm não abrem: "Authorization required"); ou rodar do Konsole da área de trabalho.~~ Resolvido na Fase 4c.2 (Task 6: nota no checklist da Fase 2 e no README).
4. ~~Checklist item 6b: pela distrobox com `| tee`, Ctrl+C/fechar o terminal derruba só o cliente do podman — o MO2 continua aberto (o mount também continua, como esperado). Ajustar o texto.~~ Resolvido na Fase 4c.2 (Task 6: texto do item ajustado no checklist da Fase 2).
5. ~~Checklist item 8: o MO2 cria `logs/crash.log` vazio ao iniciar; checar "não vazio".~~ Resolvido na Fase 4c.2 (Task 6: checklist da Fase 2 pede `crash.log` não vazio).
6. ~~`mo2-vfs`: aviso `cannot write to stdout (Broken pipe)` quando o MO2 já fechou — silenciar nesse caso.~~ Resolvido na Fase 3 (Task 1, `EPIPE` sem aviso).
7. ~~Da revisão final (adiado junto, decisão de 2026-10-17): helper travado com grupo observado → `helperLost()` chama `deactivated()` com a raiz ainda montada, e o refresh adiado pode varrer um mount vivo ou travado (só quando `update`/`watch` expira durante uma execução; correção sugerida: `keepLeftMounted()` + `m_cleanupNeeded`).~~ Resolvido na Fase 4c.2 (Task 3: o helper travado com programa observado mantém a raiz como montada e só é morto quando o mount some).

Itens menores encontrados nas revisões e adiados (listas completas nos ledgers de cada fase). As pendências da Fase 1 marcadas para a Fase 2 (teste de `fileMapping()`, varredura de pastas, FILETIME/INI, guard de saves, smoke test) foram resolvidas.

**Próximo checkpoint**
- Compilar no Windows as dependências com os nossos patches (`deps/`); hoje o CI Windows usa as dependências do upstream e nossos patches só foram conferidos por análise (`unifdef`).

**Fase 3 (Steam/Proton) — resolvidas na Fase 3 (spec §9)**
- ~~**Distrobox:** rodar o `mo2-vfs` no host ou antecipar o AppImage~~ (ponte `distrobox-host-exec`, Task 7).
- ~~Processos do Wine/Proton que saem do grupo observado (`setsid`/`wineserver`)~~ (espera do `wineserver` pelo `cwd`, Task 2; SKSE real confirmado na Task 3, 2026-10-18). ~~Identificar processos do Wine/Proton por `/proc/<pid>/cmdline`~~ (substituído pelo `cwd` do `wineserver`).
- ~~`waitForAllUSVFSProcessesWithLock` devolve `Completed` com mount ativo; segundo "Executar" faz `update` duplo; diretório de trabalho vazio usa o cwd do MO2~~ (Tasks 8, 9 e 11).
- ~~Zumbis não reapados~~ (o `mo2-vfs` reapa os filhos que inicia).
- ~~Espera de processos: `UntouchedCommandLineArguments` ainda usa aspas no estilo Windows~~ (argumentos do executável divididos como a linha de comando do Windows, Task 9).
- Novo e resolvido por construção: `/home` × `/var/home` dentro do container são mounts separados; como VFS e programas rodam no host, não há dois caminhos para o mesmo mount.

**Fase 3 (sobras — ficaram para depois)**
- Cache de diretórios do Wine após `update` a quente (não testado; a interface fica travada durante a execução).
- `waitInactive` espera até 5 s após o Unlock; o destrutor do conector pode bloquear a GUI até 5 s com grupo vivo.
- Aviso de VFS perdido só aparece no `afterRun` (execuções que o pulam avisam no próximo Run).
- ~~Estado "não foi possível desmontar" nunca reconfere o mountinfo (refresh fica adiado até o próximo Run)~~ (resolvido na Fase 4c.2, Task 2: a cada 5 s); `reportError()` do `uibase` Linux abre modal mesmo offscreen (`run` pela linha de comando não termina após erro); no Windows, `run` sem UI perde o motivo no diálogo (mudança do `beforeRun` sem `#ifndef _WIN32`).
- Passthrough (desligado por padrão): `O_RDWR` reaberto sem necessidade, janela create-vs-open, `ETXTBSY` espúrio após release tardio, contagem de `io_modes` vaza em open interrompido.
- Whiteouts: o caminho antigo de um rename continua mascarado até desmontar (esconde mod instalado depois); whiteouts por origem superdisfarçam quando uma fonte tem dois destinos; nunca são coletados.
- `AsyncPipe` (fds) na espera de processos.
- Extração temporária de FOMOD/readme em `/tmp` compartilhado: usar `QTemporaryDir` por processo e rejeitar `..` nos caminhos do arquivo compactado.
- Curingas (`*`, `?`) em `shellDelete` só quando o caminho literal não existir.
- Remover os encaminhadores de include obsoletos do `uibase` (mais de 20 arquivos usam).
- Flake conhecido do CI: uma queda do app (core dump) no passo de seleção de instância do smoke test, vista 1 vez e não reproduzida em 60 execuções; acompanhar. Também: `--no-sandbox` do smoke deveria valer só como root.
- Novas, da revisão por tarefa da Fase 3 (a revisão final do branch pode corrigir algumas):
  - `uibase`: o formatador de log de `QVariant` segfaulta com um `QVariant` inválido (`typeName()` nulo); contornado gravando `QString()` em vez de inválido em `environment`.
  - `waitForAllUSVFSProcessesWithLock` pode girar sem pausa se um pid listado não aparecer em `/proc` (zumbi antes de ser reapado).
- `mo2-vfs`, raiz só com mappings de arquivo (2026-10-18): `unlink plugins.txt` seguido de `create plugins.txt` apaga o arquivo do perfil e cria o novo na pasta real (o USVFS redirecionaria para o mapping); mesma classe do rename sobre mapping de arquivo (que já dá `EXDEV`). Não corrigido agora.

**Fase 4 (Ecossistema)**
- Desenho `read64`/`readWinLong` + `static_assert` para substituir o guard de CI dos saves (o guard não vê `read(v)` deduzido nem grafias raras).
- ~~`MO2_VFS_TIMEOUT_MS` também pode alongar o timeout: limitar a 30 s~~ (resolvido na Fase 4c.2, Task 1); `mo2-vfs`: stdin não-UTF-8 tratado como EOF, ~~`println!` do modo de limpeza entra em pânico com EPIPE~~ (resolvido na Fase 3), validar destino "/" em mapeamento de arquivo (hoje `expect()`), mapeamento de arquivo ignora sufixos de skip.
- ~~`plugin_python`: `cmake_common` exige Python 3.13 exato; ramo `pylupdate6.exe`.~~ Resolvido na Fase 4d.1 (Task 1: no Linux a versão vem do Python do sistema e `pyuic6`/`pylupdate6` vêm do `PATH`).
- ~~Credenciais via keyring~~ (Fase 4a); mensagem amigável quando o MO2 foi aberto como root antes (`/tmp/qipc_*`).
- Lixeira por ponto de montagem (`.Trash-$uid`).
- Escrita de INI atômica e respeitando a codificação do arquivo; leitor de INI tratando seções repetidas como o Windows (o `#` não é comentário, como no Windows).
- Texto do aviso da Fase 3 em inglês no código-fonte (traduções).
- Reportar ao upstream os bugs do `bsatk` (extração duplicada, `writeData` invertido, `%s` com path).

**Fase 4c.3 (Ferramentas pelo VFS)**
- O comando `run` às vezes para de esperar o programa antes de ele terminar: visto duas vezes com o log em nível normal no spike de 2026-10-21, nunca em debug. O MO2 sai com "mo2-vfs is still running, left to end by itself" e o mount fica até o programa sair.

**Fase 4d.2 (`basic_games`)**
- Reativar a ordem de carga do Baldur's Gate 3 (`dotnet Divine.dll`, gerando o `modsettings.lsx`) quando o LSLib aceitar caminhos Unix (hoje `System.Uri` os trata como relativos). Não basta remover o desvio do Linux em `LSLibRetriever.download_lslib_if_missing()`: também é preciso levar o `pak_parser` ao `Divine.dll` (`_divine_command` aponta para o `Divine.exe`, `pak_parser.py:41`; `run_divine` usa `subprocess.CREATE_NO_WINDOW`, que só existe no Windows, `pak_parser.py:207`, e deve rodar `dotnet Divine.dll`), trocar o nome do asset baixado do GitHub e retirar o desvio das ferramentas "Check For LsLib Updates" e "Reparse Pak Metadata".
- Mapear para o prefixo os jogos que leem `LOCALAPPDATA`/`%APPDATA%` do ambiente (`game_thug2`, `game_msfs2020`, `game_subnautica`) e a pasta de usuário do `game_oblivion_remaster.py`.

**Fase 5 (Distribuição)**
- Nota de proveniência/licença do `dxgiformat.h`; `7z.so` com libstdc++ dinâmica; separar símbolos de depuração (binário de ~345 MB).
- Cache da imagem do container no CI; avisos do Node 20 nas actions.
