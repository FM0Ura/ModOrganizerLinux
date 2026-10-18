# Estado do projeto

> Atualize este arquivo ao fim de cada tarefa, checkpoint ou decisão. O plano de cada fase continua sendo a fonte dos detalhes; aqui fica só o "onde estamos".

**Última atualização:** 2026-10-18
**Fase atual:** 3 — Steam/Proton · **Status:** 🟡 em teste manual (ponto de controle do SKSE aprovado em 2026-10-18; revisão final do branch e rodada de correções feitas; faltam o checkpoint de CI e o teste manual do usuário) · **Anterior:** Fase 2 — VFS FUSE ✅ (merge no `main` em 2026-10-17)
**Branch principal:** `main` (Fases 1 e 2; `fase-2-vfs-fuse` foi integrado por fast-forward; `claude/serene-maxwell-1dyrbg` é o antigo branch da Fase 1). **Branch da Fase 3:** `fase-3-steam-proton` (a partir de `main`; ainda não enviado ao GitHub)

## Repositório

Tudo fica em [`FM0Ura/ModOrganizerLinux`](https://github.com/FM0Ura/ModOrganizerLinux): build, CI, testes, docs, o app em `deps/modorganizer` e as demais dependências em `deps/`. O fork [`FM0Ura/modorganizer`](https://github.com/FM0Ura/modorganizer) (`bb46b18`) foi incorporado em 2026-10-15 e não recebe mais mudanças.

## Fase 3 — progresso

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
| — | Revisão final do branch + rodada de correções (mount deixado nunca é montado por cima, mesmo com helper novo do `spawn()`; tabela de mounts ilegível conta como montado; programa de uma ativação não roda sem o VFS se o helper se perdeu; docs do resultado da Task 3) | ✅; falta o checkpoint de CI (push só com autorização do usuário) e o teste manual | `79f9233`, `99d405b`, este commit |
| — | Teste manual (2026-10-18): SSEEdit sob Proton abortava com "Access denied" (código 5, plugins nunca carregavam) ao criar `Plugins.sseviewsettings` em `AppData/Local/Skyrim Special Edition`, raiz que só tem os mappings de arquivo do perfil e nenhuma camada de criação (`EROFS`). Corrigido: raiz só com mappings de arquivo grava na pasta real, como o USVFS (decisão de 2026-10-18); renomear sobre um destino de mapping de arquivo dá `EXDEV`. O pop-up "Not implemented" do SSEEdit é do Wine, não do MO2 (limitações conhecidas da Fase 3) | ✅ | `99ace0c`, `504bc0f` |

### Critérios de pronto (spec da Fase 3, §10)

| Critério | Status |
|---|---|
| 1. Testes do §8 passam no CI Linux; CI Windows compilando | ⏳ nenhum run de CI ainda neste branch (nada foi enviado); local: `./build.sh --tests` na distrobox com `MO2_REQUIRE_FUSE=1`, `cargo fmt --check` e `cargo clippy` verdes; Windows: o último commit do branch leva `[windows-ci]`. Desvio do §8: o teste unitário de `proton`/`environment` no `ExecutablesList` não existe (código do app fora do `organizer_tests`); coberto pelo `probe-exe` do e2e e por uma conferência da ida e volta pelo `QSettings` de valores com vírgula (registrado na spec) |
| 2. Checklist manual no Bazzite | ⏳ [`docs/testing/phase3-manual-checklist.md`](docs/testing/phase3-manual-checklist.md); o ponto de controle do SKSE (Task 3) já passou |
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

Fase 3: o branch `fase-3-steam-proton` ainda não foi enviado, então o pacote é a compilação local (`./build.sh` na distrobox `mo2-run`, resultado em `install/`, com o `mo2-vfs` ao lado); depois de um run Linux verde, o artefato `mo2-linux-install` desse run. Checklist em `docs/testing/phase3-manual-checklist.md`.

(Fase 2: use o artefato `mo2-linux-install` do último run Linux verde do branch `main`; ele traz o `mo2-vfs` ao lado do executável. Checklist em `docs/testing/phase2-manual-checklist.md`.)

Fase 1: Artefato `mo2-linux-install` da [execução Linux #28](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/38807459345) (passo a passo no README, seção Bazzite; checklist em `docs/testing/phase1-manual-checklist.md`).

## Próximos passos

1. Com a autorização do usuário: push do branch e conferência dos dois CIs (Linux e Windows, pelo `[windows-ci]`).
2. Teste manual do usuário com `docs/testing/phase3-manual-checklist.md`, **com o MO2 iniciado pelo box** (`distrobox enter mo2-run -- .../ModOrganizer`): o ponto de controle da Task 3 rodou o `mo2-vfs` direto no host, então é o teste manual que exercita a ponte `distrobox-host-exec` real com um jogo gráfico (item 2 do checklist). Só então o merge em `main` e a spec passa a "aprovada; implementada".
3. As 6 pendências restantes da Fase 2 (do teste manual e da revisão final) continuam para depois, em "Pendências por fase".
4. Resolver a pendência de compilar os `deps/` com nossos patches no Windows.

## Limitações conhecidas da Fase 3

- Jogo e ferramenta ao mesmo tempo no mesmo prefixo não são suportados (verbo `waitforexitandrun` do `umu`; a interface do MO2 fica travada durante a execução).
- O VFS e os programas rodam no host: programas que só existem dentro do container `mo2-run` não executam pelo MO2. Sem `distrobox-host-exec` no `PATH` (em container), "Executar" falha com a causa e não monta nada; `MO2_VFS_BRIDGE=direct` escolhe o helper dentro do container (testes e quem tem a Steam por dentro).
- `umu-launcher` precisa estar no host (sem fallback para `proton run`); sem ele, "Executar" mostra "install umu-launcher on the host".
- Jogos que nunca rodaram pela Steam não têm prefixo: rode o jogo uma vez pela Steam ("has no Proton prefix yet").
- O `umu` cria o link `pfx/drive_c/users/<usuário> → steamuser` no prefixo da Steam (comportamento normal do `umu`, inofensivo).
- O mount só desmonta depois que o `wineserver` do prefixo sai (cerca de 3 s após o jogo fechar); um prefixo travado mantém o mount até o `wineserver` sair ou o usuário usar "Unlock".
- Cache de diretórios do Wine após um `update` a quente do VFS: não testado.
- SKSE real (`skse64_loader.exe`): verificado no ponto de controle da Task 3 com o `mo2-vfs` direto no host; pela ponte `distrobox-host-exec` (MO2 no box), só no teste manual. Só o Skyrim SE e o SSEEdit são o escopo testado; Flatpak e outros discos só com fixtures; demais jogos Bethesda só compilam; xEdit de outros jogos, BodySlide e Nemesis/Pandora ficam para a Fase 4.
- `.bat`/`.cmd` com caminho Unix contendo espaços em `umu-run cmd /c`: não verificado sob o Wine (checklist).
- A pasta "My Games" (e saves/AppData) é fixada quando o jogo é detectado, ao abrir a instância: um prefixo criado depois (jogo rodado pela Steam pela primeira vez com o MO2 aberto) só vale depois de reiniciar o MO2.
- SSEEdit sob Proton mostra um pop-up "Not implemented" ao abrir: é a limitação de OLE do RichEdit do Wine na janela de texto inicial (`EOleException` em `TfrmRichEdit.BuildTOC`), não o MO2; contorno: `protontricks 489830 riched20`.
- Valem as limitações das Fases 1 e 2, menos "Executar" só para programas nativos e "My Games"/AppData em pastas XDG (agora no prefixo, com volta às pastas XDG quando não há prefixo).

## Limitações conhecidas da Fase 2

(Fase 2; "Executar" de `.exe` e o mount invisível para a Steam do host foram resolvidos na Fase 3.)

- ~~"Executar" só funciona para programas Linux nativos; `.exe`/Proton continuam com o aviso da Fase 3.~~ (resolvido na Fase 3, pendente do teste manual)
- ~~O MO2 roda no distrobox `mo2-run`: o mount FUSE existe só no namespace de montagem do container e **não** é visto pela Steam do host. O `findmnt` do host não o mostra; confira dentro do box.~~ (resolvido na Fase 3: o mount é feito no host)
- Só há mount enquanto um programa roda (monta ao executar, desmonta quando o último grupo observado termina); a aba Data e o refresh continuam usando a árvore nativa.
- Enquanto há mount ativo, o refresh de mods fica adiado e roda quando o VFS desmonta; trocar de perfil nesse período mostra a lista nova com a árvore antiga até desmontar.
- `open`+`read` fica em cerca de 3,4x o nativo (passthrough exige `CAP_SYS_ADMIN`); primeira montagem a frio, com os metadados fora do cache do kernel, leva cerca de 3 s.
- Se o `mo2-vfs` morrer durante a execução, o MO2 avisa só ao fim da execução; um `kill -9` deixa o mount órfão até reabrir o MO2 (que limpa).
- `updateForcedLibraries`, `executableBlacklist`, `spawnDelay` e afins do USVFS são ignorados no Linux.
- Mantém as limitações da Fase 1 abaixo, menos "Sem VFS".

## Limitações conhecidas da Fase 1

- (Fase 1) Sem VFS: resolvido na Fase 2 para programas nativos e na Fase 3 para jogos Proton.
- ~~"Executar" mostra aviso; jogos e ferramentas não iniciam~~ (resolvido na Fase 3, pendente do teste manual).
- Sem LOOT, sem criação de BSA, sem atualização automática, sem plugins Python, sem `nxm://` (Fase 4/5).
- Logins do Nexus/Steam ficam só em memória (perdidos ao fechar) até haver integração com keyring.
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

## Pendências por fase

**Fase 2 — encontradas no teste manual (2026-10-17), adiadas a pedido**
1. Sem tela (`WAYLAND_DISPLAY`/`DISPLAY` vazios, ex. por SSH) o MO2 aborta com "has crashed" e backtrace; deveria dizer claramente que não há display e sair.
2. Konsole como programa de teste precisa de `--separate --nofork`: sem `--separate` ele entrega a janela ao Konsole do host (D-Bus), sai na hora e o VFS desmonta. Corrigir o checklist e o exemplo de executável.
3. Checklist: rodar o MO2 por SSH exige `WAYLAND_DISPLAY`, `XDG_SESSION_TYPE`, `XDG_RUNTIME_DIR`, `DISPLAY` e `XAUTHORITY` (sem `XAUTHORITY` programas X11 como o xterm não abrem: "Authorization required"); ou rodar do Konsole da área de trabalho.
4. Checklist item 6b: pela distrobox com `| tee`, Ctrl+C/fechar o terminal derruba só o cliente do podman — o MO2 continua aberto (o mount também continua, como esperado). Ajustar o texto.
5. Checklist item 8: o MO2 cria `logs/crash.log` vazio ao iniciar; checar "não vazio".
6. ~~`mo2-vfs`: aviso `cannot write to stdout (Broken pipe)` quando o MO2 já fechou — silenciar nesse caso.~~ Resolvido na Fase 3 (Task 1, `EPIPE` sem aviso).
7. Da revisão final (adiado junto, decisão de 2026-10-17): helper travado com grupo observado → `helperLost()` chama `deactivated()` com a raiz ainda montada, e o refresh adiado pode varrer um mount vivo ou travado (só quando `update`/`watch` expira durante uma execução; correção sugerida: `keepLeftMounted()` + `m_cleanupNeeded`).

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
- Estado "não foi possível desmontar" nunca reconfere o mountinfo (refresh fica adiado até o próximo Run); `reportError()` do `uibase` Linux abre modal mesmo offscreen (`run` pela linha de comando não termina após erro); no Windows, `run` sem UI perde o motivo no diálogo (mudança do `beforeRun` sem `#ifndef _WIN32`).
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
- `MO2_VFS_TIMEOUT_MS` também pode alongar o timeout: limitar a 30 s; `mo2-vfs`: stdin não-UTF-8 tratado como EOF, ~~`println!` do modo de limpeza entra em pânico com EPIPE~~ (resolvido na Fase 3), validar destino "/" em mapeamento de arquivo (hoje `expect()`), mapeamento de arquivo ignora sufixos de skip.
- `plugin_python`: `cmake_common` exige Python 3.13 exato; ramo `pylupdate6.exe`.
- Credenciais via keyring; mensagem amigável quando o MO2 foi aberto como root antes (`/tmp/qipc_*`).
- Lixeira por ponto de montagem (`.Trash-$uid`).
- Escrita de INI atômica e respeitando a codificação do arquivo; leitor de INI tratando seções repetidas como o Windows (o `#` não é comentário, como no Windows).
- Texto do aviso da Fase 3 em inglês no código-fonte (traduções).
- Reportar ao upstream os bugs do `bsatk` (extração duplicada, `writeData` invertido, `%s` com path).

**Fase 5 (Distribuição)**
- Nota de proveniência/licença do `dxgiformat.h`; `7z.so` com libstdc++ dinâmica; separar símbolos de depuração (binário de ~345 MB).
- Cache da imagem do container no CI; avisos do Node 20 nas actions.
