# Estado do projeto

> Atualize este arquivo ao fim de cada tarefa, checkpoint ou decisão. O plano de cada fase continua sendo a fonte dos detalhes; aqui fica só o "onde estamos".

**Última atualização:** 2026-10-17
**Fase atual:** 2 — VFS FUSE · **Status:** ✅ implementada e aprovada no teste manual (2026-10-17) — aguardando merge no `main` · **Anterior:** Fase 1 — Build nativo ✅
**Branch de trabalho:** `fase-2-vfs-fuse` (Fase 1 em `claude/serene-maxwell-1dyrbg`)

## Repositório

Tudo fica em [`FM0Ura/ModOrganizerLinux`](https://github.com/FM0Ura/ModOrganizerLinux): build, CI, testes, docs, o app em `deps/modorganizer` e as demais dependências em `deps/`. O fork [`FM0Ura/modorganizer`](https://github.com/FM0Ura/modorganizer) (`bb46b18`) foi incorporado em 2026-10-15 e não recebe mais mudanças.

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

(Fase 2: use o artefato `mo2-linux-install` do último run Linux verde do branch `fase-2-vfs-fuse`; ele traz o `mo2-vfs` ao lado do executável. Checklist em `docs/testing/phase2-manual-checklist.md`.)

Fase 1: Artefato `mo2-linux-install` da [execução Linux #28](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/38807459345) (passo a passo no README, seção Bazzite; checklist em `docs/testing/phase1-manual-checklist.md`).

## Próximos passos

1. As 7 pendências da Fase 2 (6 do teste manual + 1 da revisão final) ficam para depois, em "Pendências por fase".
2. Merge de `fase-2-vfs-fuse` no `main` (só com o seu ok); depois atualizar as referências ao branch antigo `claude/serene-maxwell-1dyrbg` no README, `deps/README.md` e checklist da Fase 1.
3. Começar a Fase 3 (Steam/Proton), tratando primeiro o item do distrobox (ver "Pendências por fase").
4. Resolver a pendência de compilar os `deps/` com nossos patches no Windows.

## Limitações conhecidas da Fase 2

- "Executar" só funciona para programas Linux nativos; `.exe`/Proton continuam com o aviso da Fase 3.
- O MO2 roda no distrobox `mo2-run`: o mount FUSE existe só no namespace de montagem do container e **não** é visto pela Steam do host. O `findmnt` do host não o mostra; confira dentro do box.
- Só há mount enquanto um programa roda (monta ao executar, desmonta quando o último grupo observado termina); a aba Data e o refresh continuam usando a árvore nativa.
- Enquanto há mount ativo, o refresh de mods fica adiado e roda quando o VFS desmonta; trocar de perfil nesse período mostra a lista nova com a árvore antiga até desmontar.
- `open`+`read` fica em cerca de 3,4x o nativo (passthrough exige `CAP_SYS_ADMIN`); primeira montagem a frio, com os metadados fora do cache do kernel, leva cerca de 3 s.
- Se o `mo2-vfs` morrer durante a execução, o MO2 avisa só ao fim da execução; um `kill -9` deixa o mount órfão até reabrir o MO2 (que limpa).
- `updateForcedLibraries`, `executableBlacklist`, `spawnDelay` e afins do USVFS são ignorados no Linux.
- Mantém as limitações da Fase 1 abaixo, menos "Sem VFS".

## Limitações conhecidas da Fase 1

- (Fase 1) Sem VFS: resolvido na Fase 2 para programas nativos; jogos Proton na Fase 3.
- "Executar" mostra aviso; jogos e ferramentas não iniciam (Fase 3).
- Sem LOOT, sem criação de BSA, sem atualização automática, sem plugins Python, sem `nxm://` (Fase 4/5).
- Logins do Nexus/Steam ficam só em memória (perdidos ao fechar) até haver integração com keyring.
- "My Games"/"AppData" apontam para pastas XDG (Fase 3 leva para o prefixo do Proton).
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

## Pendências por fase

**Fase 2 — encontradas no teste manual (2026-10-17), adiadas a pedido**
1. Sem tela (`WAYLAND_DISPLAY`/`DISPLAY` vazios, ex. por SSH) o MO2 aborta com "has crashed" e backtrace; deveria dizer claramente que não há display e sair.
2. Konsole como programa de teste precisa de `--separate --nofork`: sem `--separate` ele entrega a janela ao Konsole do host (D-Bus), sai na hora e o VFS desmonta. Corrigir o checklist e o exemplo de executável.
3. Checklist: rodar o MO2 por SSH exige `WAYLAND_DISPLAY`, `XDG_SESSION_TYPE`, `XDG_RUNTIME_DIR`, `DISPLAY` e `XAUTHORITY` (sem `XAUTHORITY` programas X11 como o xterm não abrem: "Authorization required"); ou rodar do Konsole da área de trabalho.
4. Checklist item 6b: pela distrobox com `| tee`, Ctrl+C/fechar o terminal derruba só o cliente do podman — o MO2 continua aberto (o mount também continua, como esperado). Ajustar o texto.
5. Checklist item 8: o MO2 cria `logs/crash.log` vazio ao iniciar; checar "não vazio".
6. `mo2-vfs`: aviso `cannot write to stdout (Broken pipe)` quando o MO2 já fechou — silenciar nesse caso.
7. Da revisão final (adiado junto, decisão de 2026-10-17): helper travado com grupo observado → `helperLost()` chama `deactivated()` com a raiz ainda montada, e o refresh adiado pode varrer um mount vivo ou travado (só quando `update`/`watch` expira durante uma execução; correção sugerida: `keepLeftMounted()` + `m_cleanupNeeded`).

Itens menores encontrados nas revisões e adiados (listas completas nos ledgers de cada fase). As pendências da Fase 1 marcadas para a Fase 2 (teste de `fileMapping()`, varredura de pastas, FILETIME/INI, guard de saves, smoke test) foram resolvidas.

**Próximo checkpoint**
- Compilar no Windows as dependências com os nossos patches (`deps/`); hoje o CI Windows usa as dependências do upstream e nossos patches só foram conferidos por análise (`unifdef`).

**Fase 3 (Steam/Proton)**
- **Distrobox:** rodar o `mo2-vfs` no host ou antecipar o AppImage (ver decisões); sem isso a Steam do host não vê o mount.
- Processos do Wine/Proton que saem do grupo observado (`setsid`/`wineserver`) e cache de diretórios do Wine após `update`.
- Identificar processos do Wine/Proton por `/proc/<pid>/cmdline` (aparecem como `wine64-preloader`).
- `waitForAllUSVFSProcessesWithLock` devolve `Completed` com mount ativo (trocar de perfil mostra a lista nova com a árvore antiga até desmontar); segundo "Executar" faz `update` duplo; diretório de trabalho vazio usa o cwd do MO2.
- Zumbis não reapados; `waitInactive` espera até 5 s após o Unlock; o destrutor do conector pode bloquear a GUI até 5 s com grupo vivo.
- Aviso de VFS perdido só aparece no `afterRun` (execuções que o pulam avisam no próximo Run).
- Estado "não foi possível desmontar" nunca reconfere o mountinfo (refresh fica adiado até o próximo Run); `reportError()` do `uibase` Linux abre modal mesmo offscreen (`run` pela linha de comando não termina após erro); no Windows, `run` sem UI perde o motivo no diálogo (mudança do `beforeRun` sem `#ifndef _WIN32`).
- Passthrough (desligado por padrão): `O_RDWR` reaberto sem necessidade, janela create-vs-open, `ETXTBSY` espúrio após release tardio, contagem de `io_modes` vaza em open interrompido.
- Whiteouts: o caminho antigo de um rename continua mascarado até desmontar (esconde mod instalado depois); whiteouts por origem superdisfarçam quando uma fonte tem dois destinos; nunca são coletados.
- Espera de processos: `AsyncPipe` (fds); `UntouchedCommandLineArguments` ainda usa aspas no estilo Windows.
- Extração temporária de FOMOD/readme em `/tmp` compartilhado: usar `QTemporaryDir` por processo e rejeitar `..` nos caminhos do arquivo compactado.
- Curingas (`*`, `?`) em `shellDelete` só quando o caminho literal não existir.
- Remover os encaminhadores de include obsoletos do `uibase` (mais de 20 arquivos usam).
- Flake conhecido do CI: uma queda do app (core dump) no passo de seleção de instância do smoke test, vista 1 vez e não reproduzida em 60 execuções; acompanhar. Também: `--no-sandbox` do smoke deveria valer só como root.

**Fase 4 (Ecossistema)**
- Desenho `read64`/`readWinLong` + `static_assert` para substituir o guard de CI dos saves (o guard não vê `read(v)` deduzido nem grafias raras).
- `MO2_VFS_TIMEOUT_MS` também pode alongar o timeout: limitar a 30 s; `mo2-vfs`: stdin não-UTF-8 tratado como EOF, `println!` do modo de limpeza entra em pânico com EPIPE, validar destino "/" em mapeamento de arquivo (hoje `expect()`), mapeamento de arquivo ignora sufixos de skip.
- `plugin_python`: `cmake_common` exige Python 3.13 exato; ramo `pylupdate6.exe`.
- Credenciais via keyring; mensagem amigável quando o MO2 foi aberto como root antes (`/tmp/qipc_*`).
- Lixeira por ponto de montagem (`.Trash-$uid`).
- Escrita de INI atômica e respeitando a codificação do arquivo; leitor de INI tratando seções repetidas como o Windows (o `#` não é comentário, como no Windows).
- Texto do aviso da Fase 3 em inglês no código-fonte (traduções).
- Reportar ao upstream os bugs do `bsatk` (extração duplicada, `writeData` invertido, `%s` com path).

**Fase 5 (Distribuição)**
- Nota de proveniência/licença do `dxgiformat.h`; `7z.so` com libstdc++ dinâmica; separar símbolos de depuração (binário de ~345 MB).
- Cache da imagem do container no CI; avisos do Node 20 nas actions.
