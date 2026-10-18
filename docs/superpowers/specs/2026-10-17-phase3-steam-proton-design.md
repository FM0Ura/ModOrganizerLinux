# Fase 3 — Steam/Proton — Design

- **Data:** 2026-10-17
- **Status:** em revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md) (§3.2 resume esta fase; este documento a substitui onde divergirem)
- **Fase anterior:** [`2026-10-15-phase2-vfs-fuse-design.md`](2026-10-15-phase2-vfs-fuse-design.md) (protocolo, ciclo de vida do mount e `LinuxVfsConnector`, que esta fase estende)
- **Branch:** `fase-3-steam-proton` (a partir de `main`)

## 1. Objetivo

Iniciar jogos e ferramentas Windows pelo MO2, via Proton, com os mods ativos no VFS. Meta: no Bazzite, Skyrim SE + SKSE chegam ao menu principal com mods, e o SSEEdit lê os plugins dos mods e grava na camada overwrite, sem passar pela Steam.

### 1.1 Decisões desta fase

| Tema | Decisão |
|---|---|
| Onde roda o VFS e o jogo | **No host**, mesmo com a interface do MO2 no distrobox `mo2-run` (até a Fase 5). O MO2 inicia o `mo2-vfs` por `distrobox-host-exec` quando está em container. |
| Quem inicia o programa | O **`mo2-vfs`**, com o pedido novo `spawn`: quem cria o processo também o observa, então o grupo de processos certo é sempre acompanhado. |
| Programas Linux nativos | Também iniciados pelo `mo2-vfs`, no host (um mount do host não aparece no container). Um programa que só exista dentro do container deixa de poder ser executado pelo MO2. |
| Lançador | `umu-run` obrigatório (vem no Bazzite). Sem fallback para `proton run`. |
| Verbo do Proton | O padrão do `umu`, `waitforexitandrun`, como a Steam. Jogo e ferramenta ao mesmo tempo no mesmo prefixo não são suportados (a interface do MO2 fica travada durante a execução). |
| Desmontagem | Quando todos os grupos acompanhados terminam **e** o `wineserver` do prefixo saiu (§5.3). |
| Escopo testado | Steam nativa e Skyrim SE no Bazzite; Flatpak e bibliotecas em outros discos com fixtures; demais jogos Bethesda só compilam. Ferramenta validada: SSEEdit. xEdit de outros jogos, BodySlide, Nemesis/Pandora ficam para a Fase 4. |
| Efeito colateral do `umu` | Aceito: cria `pfx/drive_c/users/<usuário> → steamuser` no prefixo da Steam (comportamento normal do `umu`, inofensivo para a Steam). Documentado no checklist. |

### 1.2 Resultado dos spikes (2026-10-17)

Feitos no Bazzite com o binário `mo2-vfs` atual, um mod com um arquivo marcador sobre `Skyrim Special Edition/Data`, `umu-launcher` 1.4.4 e Proton Experimental (a versão gravada no prefixo, `11.0-100`). Código descartado.

| Pergunta | Resultado |
|---|---|
| Wine/Proton via `umu-run` **no host** vê o VFS do host? | Sim, por `/var/home/...` e por `/home/...` (no host, `/home` é symlink). Lê o marcador, lista os arquivos do mod, e a escrita cai na camada overwrite. |
| `umu-run` (pressure-vessel aninhado) **dentro do `mo2-run`** funciona? | Sim (com os módulos `umu` do host em `PYTHONPATH`). |
| Dentro do `mo2-run`, o Wine vê o VFS? | **Só pelo mesmo caminho do mount.** No container, `/home/<usuário>` e `/var/home/<usuário>` são **dois mounts separados** do mesmo diretório; o FUSE fica só sobre um. Pelo outro caminho, o Wine não viu o mod e escreveu no `Data` real. A Steam e o Proton usam `/var/home`; o caminho canônico no container é `/home`. Motivo da decisão "VFS no host". |
| Processos do Proton saem do grupo observado? | Sim: `pressure-vessel`, `wineserver`, `services.exe`, `explorer.exe` e o próprio `SkyrimSE.exe` ficam cada um em sessão e grupo próprios. |
| O mount sobrevive até o jogo fechar com `watch {pgid do umu-run}`? | Sim: o `umu-run` só sai depois do `wineserver` (`waitforexitandrun`); o grupo dele cobriu a execução inteira (~47 s até sair pelo menu). |
| A Steam reinicia o jogo por conta própria (`SteamAPI_RestartAppIfNecessary`)? | Não: com `SteamAppId`/`SteamGameId` e a Steam do host aberta, o `SkyrimSE.exe` ficou na árvore do `umu-run` e chegou ao menu. |
| `distrobox-host-exec` serve de ponte para o protocolo JSON? | Sim: stdin e stdout chegam ao processo do host como pipes; o processo roda no namespace de montagem do host, com pgid próprio. Quando o cliente no container morre (`SIGKILL`), o processo do host recebe EOF no stdin e nenhum sinal. O ambiente do processo é o do host: variáveis do container não passam. |
| O `cwd` do `wineserver` identifica o prefixo? | Sim: com o Proton Experimental real, o `wineserver` (dentro do namespace do pressure-vessel) tinha `cwd` `/tmp/.wine-1000/server-3a-35652`, igual ao calculado de `stat` do `compatdata/489830/pfx` (`dev` 0x3a, `ino` 0x35652), e o link é legível do host. |
| Não testado | SKSE (`skse64_loader.exe` inicia o jogo e sai); cache de diretórios do Wine após `update`. Ver §11. |

### 1.3 Fora de escopo

- Jogo e ferramenta simultâneos no mesmo prefixo; verbo `run` do Proton.
- Fallback para `proton run` sem `umu`.
- Execução de programas dentro do container `mo2-run`.
- LOOT, plugins Python, `nxm://`, demais ferramentas (Fase 4); AppImage (Fase 5); Steam Deck/modo Gaming.
- Pendências da Fase 2 não ligadas ao lançamento (passthrough, whiteouts, FOMOD em `/tmp`, curingas do `shellDelete`, encaminhadores de include do `uibase`): continuam no `STATE.md`.

## 2. Arquitetura

```
 container mo2-run                      host (Bazzite)
┌──────────────────────┐  stdin/stdout  ┌───────────────────────────────────┐
│ ModOrganizer (GUI)   │  JSON lines    │ mo2-vfs                           │
│  LinuxVfsConnector ──┼── distrobox- ──┼─▶ mount/update/unmount (FUSE)     │
│  ProtonLauncher      │   host-exec    │   spawn ─▶ umu-run ─▶ proton ─▶   │
│  (monta argv/env)    │                │           wine ─▶ SkyrimSE.exe    │
└──────────────────────┘                │   watch automático do filho       │
                                        │   + wineserver do prefixo         │
                                        └───────────────────────────────────┘
```

| Unidade | Onde | Função |
|---|---|---|
| Ponte para o host | `deps/modorganizer/src/linux/vfsconnector_linux.cpp`, `vfsclient_linux.cpp` | Decide como iniciar o `mo2-vfs` (direto ou por `distrobox-host-exec`) (§6.1). |
| `spawn` e espera do `wineserver` | `mo2-vfs/src/` (`ipc.rs`, `watch.rs`, módulo novo `spawn.rs`) | Inicia o programa, observa o grupo dele e o `wineserver` do prefixo (§5). |
| Descoberta da Steam | `deps/uibase/src/linux/steamutility_linux.cpp` (estendido) e um parser de VDF | Bibliotecas, jogos, prefixos, Protons e o Proton escolhido por jogo (§3). |
| Pastas do jogo no prefixo | `deps/game_bethesda/src/gamebryo/gamegamebryo.cpp` (blocos Linux) | "My Games", saves e AppData dentro do prefixo (§4). |
| `ProtonLauncher` | `deps/modorganizer/src/linux/` (arquivo novo `protonlauncher_linux.{h,cpp}`), chamado por `processrunner_linux.cpp` e `spawn_linux.cpp` | Monta argv/env/cwd do lançamento a partir do executável, do jogo e da Steam (§6.2). |
| Configuração por executável | `executableslist.cpp`, `editexecutablesdialog.{cpp,ui}` | Proton e variáveis de ambiente por executável (§6.3). |

O CMake do app continua sem Rust; as regras de `deps/modorganizer/src/linux/README.md` valem para os arquivos novos (Linux-only, sem tocar o build do Windows além de `#ifdef` de uma a três linhas).

## 3. Descoberta da Steam (`uibase`)

Código puro (sem Qt GUI), com testes por fixtures.

- **Raízes da Steam**, nesta ordem: `~/.local/share/Steam`, `~/.steam/steam` e `~/.var/app/com.valvesoftware.Steam/.local/share/Steam` (Flatpak). Raízes que dão no mesmo diretório real (`canonicalPath`) contam uma vez.
- **Bibliotecas:** `steamapps/libraryfolders.vdf` de cada raiz (inclusive bibliotecas em outros discos).
- **Jogo:** `steamapps/appmanifest_<appid>.acf` (`installdir`) na biblioteca que o lista → `steamapps/common/<installdir>`. Substitui a busca atual do `findSteamGame`, mantendo a assinatura.
- **Prefixo:** `steamapps/compatdata/<appid>` na biblioteca do jogo; se não existir lá, na biblioteca principal (comportamento da Steam). Válido só se `pfx/` existir.
- **Protons instalados:** `steamapps/common/Proton*` de todas as bibliotecas (nome interno lido do `toolmanifest.vdf`/nome da pasta, conforme o caso) e `compatibilitytools.d/*` com `compatibilitytool.vdf`. Cada um vira `{nome exibido, nome interno, caminho}`.
- **Proton escolhido pela Steam:** `config/config.vdf` → `CompatToolMapping` → `<appid>` → `name` (nome interno, ex.: `proton_experimental`, `GE-Proton11-7`); se não houver entrada, a do appid `0` (padrão global); se não houver nenhuma, a versão gravada em `compatdata/<appid>/version` comparada com a de cada Proton instalado.
- **Parser de VDF texto:** chaves sem diferenciar maiúsculas/minúsculas, escapes `\\` e `\"`, comentários `//`, tolerante a chaves repetidas (vale a última).
- **API** (nomes finais no plano): `steamRoots()`, `steamLibraries()`, `findSteamApp(appid)` → `{gamePath, library, compatData}`, `installedProtons()`, `steamCompatTool(appid)`.

## 4. Pastas do jogo no prefixo (`game_bethesda`)

No Linux, `documentsDirectory()`, `savesDirectory()` e `localAppFolder()` do `GameGamebryo` usam o prefixo do appid do jogo (`steamAPPId()`):

- documentos: `<compatdata>/pfx/drive_c/users/steamuser/Documents/My Games/<pasta do jogo>`;
- AppData local: `<compatdata>/pfx/drive_c/users/steamuser/AppData/Local`.

Sem prefixo (jogo nunca rodado pela Steam), voltam às pastas XDG da Fase 1 e registram um aviso no log. Os mapeamentos que já existem (`plugins.txt`/`loadorder.txt` para `localAppFolder()`, INIs e saves do perfil) passam a ter destino dentro do prefixo e viram mais raízes do `mo2-vfs`; o `mo2-vfs` não muda para isso.

## 5. `mo2-vfs`: `spawn` e espera do `wineserver`

### 5.1 Mensagens novas

| MO2 → `mo2-vfs` | Conteúdo |
|---|---|
| `spawn` | `argv` (lista, `argv[0]` procurado no `PATH` do ambiente final), `env` (objeto: variáveis acrescentadas ou trocadas sobre o ambiente do próprio `mo2-vfs`), `cwd`, `wineprefix` (opcional: caminho do `compatdata/<appid>`, para a espera do §5.3), `output` (opcional: arquivo que recebe o stdout e o stderr do programa; sem ele, `/dev/null`. Nunca o stdout do `mo2-vfs`, que é o canal do protocolo, nem um pipe: o programa não pode morrer com `SIGPIPE` quando o MO2 fecha) |

| `mo2-vfs` → MO2 | Conteúdo |
|---|---|
| `spawned` | `pid` (resposta ao `spawn`) |
| `exited` | `pid`, `code` (código de saída, ou `128 + sinal`); evento espontâneo |
| `status` | ganha `processes`: pids dos programas iniciados por `spawn` que ainda rodam e dos `wineserver` que seguram o mount (§5.3); é o que o MO2 espera em `waitForAllUSVFSProcessesWithLock` |
| `error` | código novo `spawn_failed` (`execve`/`cwd`/`argv[0]` não encontrado), com `message` e `path` |

`spawn` é aceito com ou sem mount ativo (programas sem VFS, "unhooked", usam o mesmo caminho). O filho nasce num grupo próprio (`setpgid(0, 0)`) e o grupo entra na lista de acompanhados antes da resposta `spawned`, sem corrida com um `watch` posterior. O `watch` da Fase 2 continua aceito.

Linha de comando nova: `mo2-vfs --list-mounts` escreve uma linha JSON `{"roots": [...]}` com os pontos de montagem `fuse.mo2-vfs` (ou `fuse` com fonte `mo2-vfs`) do namespace de montagem em que roda, e sai com 0. O MO2 usa isso em vez de ler o próprio `/proc/self/mountinfo`, que no container não mostra os mounts do host (§6.1). `exited` é o único evento espontâneo novo; como os outros espontâneos, o cliente nunca o toma como resposta a um pedido. O ambiente do filho é o do `mo2-vfs` no host (não o do container), mais `env`.

### 5.2 Sessão do `mo2-vfs`

O próprio `mo2-vfs` chama `setsid()` ao iniciar quando não é líder de sessão, para que o comportamento da Fase 2 (Ctrl+C no terminal do MO2 não o atinge, `SIGHUP` ignorado) não dependa de quem o iniciou (`QProcess` com `setChildProcessModifier`, ou `host-spawn` pelo `distrobox-host-exec`). O fim do stdin continua sendo o sinal de que o MO2 saiu (§5.1 da Fase 2). Escrever no stdout depois que o MO2 fechou o pipe (`EPIPE`) não gera aviso (pendência 6 da Fase 2).

### 5.3 Espera do `wineserver`

Para cada `spawn` com `wineprefix`, o `mo2-vfs` guarda o diretório do servidor do Wine daquele prefixo: `/tmp/.wine-<uid>/server-<dev>-<ino>`, com `dev` e `ino` do diretório `<wineprefix>/pfx` em hexadecimal minúsculo, como o Wine calcula. A desmontagem automática passa a exigir, além de nenhum grupo acompanhado vivo, que nenhum processo `wineserver` (pelo `comm`) tenha esse diretório como `cwd` (`/proc/<pid>/cwd`). Isso cobre programas que se relançam fora do grupo, como o `skse64_loader.exe`, que inicia o `SkyrimSE.exe` e sai (o `wineserver` só sai quando o último processo do Wine do prefixo termina, cerca de 3 s depois). A verificação usa o mesmo ciclo de 500 ms dos grupos.

## 6. Integração no MO2

### 6.1 Ponte para o host

- O `LinuxVfsConnector` considera que o MO2 está em container quando existe `/run/.containerenv` ou `/.dockerenv`. Nesse caso inicia `distrobox-host-exec <caminho do mo2-vfs> <argumentos>`; senão, `mo2-vfs` direto (como na Fase 2, e como será no AppImage da Fase 5). `MO2_VFS_BINARY` continua valendo.
- A variável `MO2_VFS_BRIDGE` (os testes automáticos do MO2 e o e2e definem `direct`) troca a detecção: `direct` inicia o helper direto mesmo em container (o CI, que roda num container Fedora sem host, e quem roda o MO2 num container com acesso à Steam por dentro); `host` força o `distrobox-host-exec`; vazio ou `auto` usa a detecção.
- Em container sem `distrobox-host-exec` no `PATH`, o conector não inicia o helper e `activate()`/`spawn` falham com a mensagem "não foi possível executar no host: `distrobox-host-exec` não encontrado". **Não** cai de volta para o mount dentro do container (o problema `/home` × `/var/home` do §1.2).
- O `--cleanup` da inicialização e a conferência dos mounts depois de perder o helper (hoje `/proc/self/mountinfo` do MO2) vão pela ponte, esta com `--list-mounts` (§5.1): os mounts estão no host.
- Caminhos passam sem tradução: no host, `/home` e `/var/home` são o mesmo diretório; o binário `mo2-vfs` vem do diretório de instalação, visível do host pela home.

### 6.2 Lançamento (`ProtonLauncher` e `ProcessRunner`)

1. **Classificação** em `processrunner_linux.cpp`: programa nativo (ELF ou `#!`, como na Fase 2) ou programa Windows (`.exe`, `.bat`, `.cmd`, `.com`, sem diferenciar maiúsculas/minúsculas). Documentos continuam abertos pelo aplicativo associado, sem VFS.
2. **Configuração** (Windows), pelo `ProtonLauncher`:
   - appid: o campo "Steam AppID" do executável, se preenchido; senão `steamAPPId()` do plugin do jogo. Sem appid → erro.
   - prefixo: `findSteamApp(appid).compatData`; sem prefixo → erro "rode o jogo uma vez pela Steam para criar o prefixo".
   - Proton, por prioridade: o do executável (§6.3) → `steamCompatTool(appid)` → erro listando os Protons instalados.
   - argv: `umu-run <caminho do .exe> <argumentos>`; para `.bat`/`.cmd`: `umu-run cmd /c <caminho>` com os argumentos.
   - env: `WINEPREFIX=<compatdata>`, `GAMEID=umu-<appid>`, `STORE=steam`, `SteamAppId=<appid>`, `SteamGameId=<appid>`, `PROTONPATH=<caminho do Proton>`, `STEAM_COMPAT_CLIENT_INSTALL_PATH=<raiz da Steam>`, mais as variáveis do executável por último. O MO2 não mexe na atualização do runtime do `umu` (ela acontece como numa execução normal do `umu-run`).
   - cwd: o diretório de trabalho do executável; vazio → o diretório do `.exe` (corrige a pendência "diretório de trabalho vazio usa o cwd do MO2").
   - `wineprefix` do `spawn` = `<compatdata>`.
3. **Nativos:** argv = binário + argumentos, env = só as variáveis do executável, cwd como acima, sem `wineprefix`.
4. **Fluxo** (o da Fase 2, trocando o início): salvar perfil → `beforeRun` (`activate()`) → `VfsConnector::spawn()` → UILocker com o `pid` → `postRun` espera → `afterRun`. Se o `spawn` falhar, `releaseIfIdle()`. Execuções sem VFS ("unhooked") também usam `spawn` (o helper é iniciado sem montar), para rodarem no host como as outras. `spawn::startBinary` continua só para programas nativos: o único outro chamador é o LOOT, da Fase 4.
5. **Espera:** a mesma da Fase 2, pela árvore de processos do `pid` em `/proc` (o container compartilha o PID do host, então o `pid` do host vale no MO2). O código de saída vem do `exited` desse `pid` (o conector guarda os códigos recebidos; `takeExitCode(pid)`), esperado por até 2 s depois do fim da árvore; vai para o log, sem diálogo quando ≠ 0. Se o helper se perder, a espera continua pela árvore e o motivo vai para `takeLostReason()`.
6. **Log:** cada lançamento registra argv, env acrescentado e cwd enviados ao `spawn`; a saída do programa vai para `<instância>/logs/run-<nome do executável>.log` (`output`).

Corrigidas de carona (pendências da Fase 2 do mesmo fluxo): `waitForAllUSVFSProcessesWithLock` espera os `processes` do `status` do helper, como o Windows espera os processos do USVFS (em vez de devolver `Completed` com mount ativo); o segundo "Executar" com mount ativo envia um só `update`.

### 6.3 Configuração por executável

- `ExecutablesList`: chaves novas no mapa do executável (`ModOrganizer.ini`), lidas e gravadas em todas as plataformas e usadas só no Linux:
  - `proton`: nome interno do Proton, ou vazio para "automático (o mesmo da Steam)";
  - `environment`: lista de `NOME=valor`.
- O campo existente "Steam AppID" passa a escolher também o prefixo (uma ferramenta pode rodar no prefixo de outro jogo).
- `EditExecutablesDialog`: grupo "Proton" (escondido no Windows) com uma lista suspensa ("Automático" + `installedProtons()`) e um campo de variáveis (uma por linha). Os argumentos continuam no campo atual.

## 7. Erros

| Situação | Resultado |
|---|---|
| Em container sem `distrobox-host-exec`, ou a ponte falha | Helper não inicia; "Executar" mostra "não foi possível executar no host" com o motivo; nada é montado. |
| `umu-run` não encontrado no host | O `spawn` falha com `spawn_failed` (`argv[0]` não está no `PATH`); o MO2 mostra "instale o `umu-launcher`" e desmonta (`releaseIfIdle()`). |
| Sem appid, sem prefixo ou sem Proton | Erro antes de montar, com o motivo e o que fazer (§6.2). |
| `spawn` falha (`execve`, `cwd`) | `error spawn_failed`, nenhum grupo acompanhado; o MO2 mostra o erro e chama `releaseIfIdle()`. |
| Programa sai com código ≠ 0 | Só no log. |
| Helper morre durante a execução | Como na Fase 2 (aviso, limpeza de órfãos ao abrir o MO2); a espera continua pela árvore do `pid` em `/proc`; sem `exited`, o código de saída fica desconhecido (log). |
| MO2 fecha ou cai durante a execução | O helper segue sozinho e desmonta quando os grupos e o `wineserver` terminam; sem aviso de `Broken pipe`. |
| `wineserver` não sai (prefixo travado) | Não desmonta; o MO2 continua "em execução" até o usuário usar "Desbloquear" no UILocker; o mount fica até o `wineserver` sair ou até a limpeza de órfãos ao reabrir o MO2. |

## 8. Testes

Automáticos, no CI Linux (sem Steam nem Proton):

- **`mo2-vfs` (Rust, `mo2-vfs/tests/`):**
  - `spawn`: argv/env/cwd aplicados; grupo próprio; `spawned` antes de qualquer `exited`; `exited` com o código de saída e com `128 + sinal`; `spawn_failed` para `argv[0]` inexistente e `cwd` inválido.
  - Relançador: pai que inicia um filho com `setsid` e sai; com `wineprefix` e um processo falso chamado `wineserver` com `cwd` no diretório do servidor do prefixo temporário, o mount sobrevive ao grupo e cai quando o falso `wineserver` sai; sem `wineprefix`, cai com o grupo (comportamento da Fase 2).
  - `setsid()` próprio e `EPIPE` sem aviso.
- **`uibase` (`deps/uibase/tests/`):** parser de VDF; raízes duplicadas; bibliotecas em outro disco; `appmanifest`; `compatdata` na biblioteca do jogo e na principal; Flatpak; `installedProtons()` (oficial e `compatibilitytools.d`); `steamCompatTool()` pelo appid, pelo padrão `0` e pela `version` do prefixo. Fixtures em árvores temporárias, com arquivos copiados de instalações reais (sem dados pessoais).
- **MO2 (`organizer_tests`):** `ProtonLauncher` com uma Steam falsa (prioridade do Proton, override de appid, `.bat`, cwd vazio, variáveis por último, erros); pastas do prefixo no `game_bethesda`; escolha da ponte com `/run/.containerenv` simulado e com `MO2_VFS_BRIDGE` (`auto`, `direct`, `host`); leitura e gravação de `proton`/`environment` no `ExecutablesList`.
- **e2e (`tests/vfs-e2e`):** "Executar" de um `.exe` com um `umu-run` falso no `PATH` (script que registra env e argv, lê um arquivo de mod pelo mount e cria um arquivo novo), passando pelo `spawn` real, com `MO2_VFS_BRIDGE=direct`: o arquivo novo vai para `overwrite/` e o mount cai ao fim.

Manual, no Bazzite (`docs/testing/phase3-manual-checklist.md`):

1. Nova instância Skyrim SE detectada automaticamente; "My Games"/AppData apontam para o prefixo.
2. SKSE instalado como mod (ou na pasta do jogo, conforme as instruções do SKSE) e um mod de teste com plugin: "Executar" `skse64_loader.exe` → o jogo chega ao menu com o plugin ativo (lista de mods ou console do SKSE) → sair pelo menu → o VFS desmonta (`findmnt` no host).
3. SSEEdit: abre pelo VFS, lista os plugins dos mods; um arquivo salvo vai para `overwrite/`.
4. Proton por executável (ex.: GE-Proton) e uma variável (`DXVK_HUD=fps`) fazem efeito.
5. Erros: `compatdata` renomeado temporariamente (prefixo ausente); `umu-run` fora do `PATH`.
6. A pasta do jogo e o prefixo continuam intactos depois (exceto o symlink do `umu`, §1.1).

## 9. Pendências tratadas nesta fase

Da lista "Fase 3" do `STATE.md`: distrobox (§6.1); processos que saem do grupo (§5.3); identificação dos processos do Wine (substituída pelo `cwd` do `wineserver`); `waitForAllUSVFSProcessesWithLock`, `update` duplo e diretório de trabalho vazio (§6.2); zumbis não reapados (o `mo2-vfs` reapa os filhos que inicia). Da lista "Fase 2": `Broken pipe` (§5.2). As demais continuam no `STATE.md`.

Novo, encontrado no spike: dentro do container, `/home` e `/var/home` são mounts separados, e um programa nativo que acesse o jogo pelo outro caminho passa por fora do VFS da Fase 2. Resolvido por construção (§1.1: VFS e programas no host).

## 10. Critérios de pronto

1. Testes do §8 passando no CI Linux; CI Windows compilando (`[windows-ci]`).
2. Checklist manual do §8 aprovado no Bazzite.
3. `ROADMAP.md`, `STATE.md` e a spec geral (§3.2) atualizados com as decisões do §1.1 e o resultado dos spikes.

## 11. Riscos

| Risco | Mitigação |
|---|---|
| `skse64_loader.exe` se comporta diferente do esperado (ex.: o `umu-run` sai antes do jogo e o `wineserver` também) | Espera do `wineserver` (§5.3); primeira tarefa do plano com o SKSE real verifica, antes do resto da integração. |
| Cálculo de `server-<dev>-<ino>` diferente do Wine do Proton, ou `cwd` do `wineserver` ilegível dentro do pressure-vessel | Verificado na mesma tarefa inicial com o Proton real; alternativa: rodar `<proton>/files/bin/wineserver -w` com o `WINEPREFIX` no host. |
| `host-spawn` não repassa sinais ao `mo2-vfs` | Verificado: o EOF do stdin chega quando o cliente morre (§1.2), e é o sinal que o `mo2-vfs` usa; sinais não são necessários. |
| Cache de diretórios do Wine após `update` com o jogo rodando | Com a interface travada durante a execução, `update` a quente é raro; fica registrado como limitação se aparecer no teste manual. |
| Atualização do runtime pelo `umu` na primeira execução (download) | Mensagem no log; o UILocker mostra o programa em execução enquanto isso. |
