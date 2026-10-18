# Fase 4b — LOOT — Design

- **Data:** 2026-10-18
- **Status:** em revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md) (§3.3 resume a Fase 4; este documento a substitui onde divergirem)
- **Fases anteriores:** [`2026-10-15-phase2-vfs-fuse-design.md`](2026-10-15-phase2-vfs-fuse-design.md) (`mo2-vfs`, `spawn`), [`2026-10-17-phase3-steam-proton-design.md`](2026-10-17-phase3-steam-proton-design.md) (ponte `vfsbridge`, `plugins.txt` no prefixo), [`2026-10-18-phase4a-nexus-nxm-keyring-design.md`](2026-10-18-phase4a-nexus-nxm-keyring-design.md) (divisão da Fase 4 em 4a–4e)
- **Branch:** `fase-4b-loot` (a partir de `main`)

## 1. Objetivo

Item 4 do critério de sucesso do projeto (spec geral §1.1): ordenar plugins com o LOOT. Meta: no Bazzite, com o MO2 no distrobox `mo2-run`, o usuário clica em "Ordenar" na aba de plugins de uma instância de Skyrim SE com mods; o MO2 atualiza a masterlist, ordena com o LOOT enxergando os plugins dos mods, mostra as mensagens do relatório e aplica a ordem nova ao perfil, que o jogo usa ao rodar pelo MO2.

### 1.1 Decisões desta fase

| Tema | Decisão |
|---|---|
| Origem do libloot | **Compilado do código-fonte**, tag `0.29.5` (commit `a12fcfcef21b96ac6fcffaac06439d11e18db932`), a mesma versão do registry vcpkg do MO2 para Windows. O libloot é Rust com wrapper C++ (`cpp/`); o LOOT só publica binários para Windows. Descartados: snapshots Linux do GitHub Actions do LOOT (exigem login, expiram em 90 dias) e um lootcli reescrito em Rust (divergiria do upstream que o Windows usa). |
| Como o lootcli vê os mods | **Pelo `mo2-vfs`**, como o USVFS no Windows (`sp.hooked = true` no upstream): o MO2 monta o VFS e inicia o lootcli com `VfsConnector::spawn()`, o caminho do "Executar". No distrobox, o lootcli roda no host. Descartados: rodar no container contando com a propagação da montagem FUSE do host (frágil) e passar as pastas dos mods como "additional data paths" do libloot (outra semântica de prioridade, `overwrite` e arquivos ocultos). |
| Pasta de dados do LOOT | A do **LOOT em Flatpak** (`~/.var/app/io.github.loot.loot/data/LOOT`) se for um diretório; senão `$XDG_DATA_HOME/LOOT` (`~/.local/share/LOOT` sem a variável), a do LOOT nativo. Como no Windows (`%LOCALAPPDATA%\LOOT`), a userlist do LOOT do usuário vale para o MO2. |
| Download da masterlist | **libcurl** (API "easy", síncrona) no lugar do WinHTTP; grava em arquivo temporário e só substitui a masterlist com HTTP 200. |
| Cancelar | Novo pedido `kill` do `mo2-vfs`: um `kill()` do container para um processo do host pode dar `EPERM`. |
| Windows | Nada muda: o MO2 Windows continua usando o `lootcli.exe` do upstream. As mudanças em `deps/lootcli` ficam em `#ifdef _WIN32` / `#else`, com o código Windows idêntico. |

### 1.2 Ambiente de referência (verificado em 2026-10-18)

- Host Bazzite 44: `libQt6Core.so.6` e `libQt6Network.so.6` (Qt 6.11.2) com os plugins TLS, `libboost_locale.so.1.90.0`, `libcurl.so.4`, `libstdc++.so.6.0.36`.
- O lootcli compilado no container `fedora:44` roda no host com o `libloot.so` ao lado (RUNPATH `$ORIGIN`); as demais bibliotecas vêm do host. Na Fase 5 (AppImage) o MO2 roda no host e tudo vai junto.
- Os programas iniciados pelo `mo2-vfs` estão no espaço de PIDs do host, compartilhado com o distrobox (`/proc` e `kill(pid, 0)` funcionam; `EPERM` conta como vivo, `envmodule_linux.cpp`).

### 1.3 Fora de escopo

- Listas de plugins dos INIs (`sTestFile`, Oblivion/Fallout 3/NV): fora do Windows o libloadorder deriva "My Games" da local path supondo um prefixo Wine, e a local path é a pasta do perfil (§3.4). Limitação conhecida.
- Interface própria do LOOT (editar a userlist, grupos): continua sendo o LOOT do usuário.
- Jogos além do Skyrim SE no teste manual: todos os jogos do `game_bethesda` que o LOOT conhece são passados ao lootcli, mas só o Skyrim SE é testado.

## 2. Arquitetura

```
MO2 (container ou host)                         host
──────────────────────                          ────
Loot::start
 ├─ prepareVFS() + vfs().activate() ─────────▶ mo2-vfs monta Data (visão dos mods)
 ├─ vfs().spawn({lootcli, args, output}) ───▶ mo2-vfs inicia bin/loot/lootcli
 │                                              ├─ libloot.so ($ORIGIN)
 │                                              ├─ masterlist ← libcurl (GitHub)
 │                                              ├─ lê <perfil>/plugins.txt, loadorder.txt
 │                                              └─ escreve lootcli.log, lootreport.json,
 │                                                 loot-loadorder.txt  (logs/ da instância)
 └─ thread do LOOT: waitForCompletion
     ├─ lê lootcli.log aos poucos → processStdout (protocolo lootcli)
     ├─ pid vivo? kill(pid, 0)
     └─ fim: código de saída do evento exited (exitcodes::take)
        └─ relatório + ordem nova aplicada ao perfil (código compartilhado)
                                                mo2-vfs desmonta quando o grupo acaba
```

## 3. Componentes

### 3.1 Build do libloot

- Script `build/libloot/build-libloot.sh`, no padrão de `build/7zip/build-7zip.sh`: baixa a tag `0.29.5` de `https://github.com/loot/libloot`, confere o commit fixado (§1.1) e guarda o fonte em `build-out/libloot/src`.
- Build pelo CMake de `cpp/` (que chama `cargo build --release`), com `LIBLOOT_BUILD_TESTS=OFF` e `LIBLOOT_INSTALL_DOCS=OFF`; `CARGO_HOME` como o do `mo2-vfs` (`~/.cache/mo2-cargo` quando o padrão não é gravável).
- Instala `libloot.so`, os headers e o pacote CMake (`find_package(libloot CONFIG)`, alvo `libloot::libloot`) em `install/`, com `lib/` como nos demais alvos.
- Novo alvo `libloot` no `build.sh`, antes de `lootcli`.

### 3.2 Build do lootcli

- `build_lootcli` deixa de passar `-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`: compila o executável e o pacote do header.
- `src/CMakeLists.txt` do lootcli: fora do Windows, sem `WIN32`/`WIN32_EXECUTABLE`/`version.rc`, liga `CURL::libcurl` e define RUNPATH `$ORIGIN`. O `install()` atual já põe `lootcli` e `libloot.so` em `bin/loot/`.
- Container (`build/Containerfile`): entram `tomlplusplus-devel` e `libcurl-devel`.
- CI Linux: o `./build.sh --tests` passa a compilar libloot e lootcli; `actions/cache` para `build-out/libloot` com chave na versão e no commit do libloot, para não recompilar o Rust a cada execução.

### 3.3 lootcli no Linux (`deps/lootcli`)

Mudanças em `#ifdef _WIN32` / `#else`; o código Windows não muda.

- **Entrada (`main.cpp`):** `int main(int argc, char** argv)` (o `argv` já é UTF-8) e `setlocale(LC_ALL, "C.UTF-8")`; mesmo tratamento de parâmetros e mesmo `LOOTWorker`.
- **`pch.h`:** sem `Shlobj.h`, `Windows.h`, `io.h` e `shellapi.h` fora do Windows; `lootthread.cpp` sem `windows.h`, `winhttp.h` e `strsafe.h`.
- **Download (`GetFile`):** versão Linux com libcurl: segue redirecionamentos, timeout de 60 s, TLS do sistema, falha com HTTP ≠ 200; grava em `masterlist.yaml.download` na mesma pasta e faz `rename` sobre `masterlist.yaml` só no sucesso. Erro: log `Error downloading masterlist: <mensagem da curl ou HTTP nnn>` e código de saída 1.
- **Pasta de dados (`GetLOOTAppData`):** §1.1; a função recebe o ambiente e a home de forma injetável, para os testes. Dela vêm `settings.toml` (fonte da masterlist e idioma, lidos como no Windows), `games/<pasta>/masterlist.yaml` e `userlist.yaml`. O `--gamePath` continua sobrescrevendo o caminho do jogo do `settings.toml`.
- **Masterlist inexistente com `--skipUpdateMasterlist`:** o upstream retorna `FALSE` (0, sucesso); no Linux, código 1.
- Os erros e o progresso continuam saindo na saída padrão, no protocolo do `lootcli.h`; a saída de erros vai para o mesmo arquivo (§3.4).

### 3.4 Integração no app (`deps/modorganizer`)

O fluxo de `Loot` não muda (`start()` → thread → `waitForCompletion()` → relatório → diálogo); as partes de plataforma ganham versão Linux em `src/linux/loot_linux.cpp`, com `#ifdef _WIN32` em `loot.cpp` onde o código atual é Windows.

- **Arquivos:** no Linux, `lootcli.log` (saída), `lootreport.json` e `loot-loadorder.txt` ficam em `<dados da instância>/logs/`, ao lado dos `run-*.log`: o `/tmp` do container e o do host podem ser diferentes. No Windows continuam em `QDir::temp()`.
- **`spawnLootcli` (Linux):** `prepareVFS()` (como hoje), `vfs().activate()` e `vfs().spawn()` com `argv` = caminho do lootcli e os parâmetros como lista (sem as aspas que o Windows põe na linha de comando), `workingDirectory` = `bin/loot`, sem `winePrefix`, `output` = `lootcli.log`. Os caminhos passados ao lootcli (`--gamePath`, `--pluginListPath`, `--out`, `--pluginListOutputPath`) passam por `vfsbridge::hostCanonicalPath`. Falha no spawn: `releaseIfIdle()` e "failed to start loot: <motivo>". A montagem dos parâmetros é uma função própria, testável.
- **Local path do libloot:** a pasta do perfil (pai de `--pluginListPath`), como no Windows; o libloadorder lê ali o `plugins.txt`/`loadorder.txt` do perfil.
- **`AsyncPipe` (Linux):** passa a ler o arquivo de saída: `create()` cria ou trunca o arquivo e devolve um handle válido; `read()` devolve os bytes novos desde a última leitura, sem bloquear. O `processStdout()` compartilhado recebe as mesmas linhas que no Windows.
- **`waitForCompletion` (Linux):** laço de 100 ms na thread do LOOT: lê a saída; testa o pid (`kill(pid, 0)`, `ESRCH` = terminou); no fim, lê o restante da saída e pega o código de saída do evento `exited` (`exitcodes::take(pid, 2 s)`; o plano confirma que a chamada é segura fora da thread principal ou a leva para ela). Código ≠ 0: "Loot failed. Exit code was: 0x…", como no Windows. Sem `exited` (o helper caiu): falha, com `takeLostReason()` no log.
- **Cancelar:** a thread do LOOT pede `vfs().kill(pid)` na thread principal (`QMetaObject::invokeMethod`, fila) e espera o fim do processo; resultado cancelado, perfil intacto.
- **Disponibilidade:** `lootUnavailableReason()` continua exigindo `bin/loot/lootcli` executável; a mensagem passa a ser "lootcli não encontrado em <caminho>".
- **Depois do sucesso:** código compartilhado sem mudança: a ordem de `loot-loadorder.txt` é aplicada ao perfil; o `plugins.txt`/`loadorder.txt` do perfil chegam ao prefixo pelos mappings da Fase 3 quando o jogo roda.
- Ao terminar o grupo do lootcli, o `mo2-vfs` desmonta sozinho se nenhum outro programa usa a montagem (regra atual).

### 3.5 `mo2-vfs`: pedido `kill`

- Pedido `{"op":"kill","pid":N}`. Só vale para um pid iniciado pelo `spawn` que ainda está em `children`; qualquer outro recebe `error` com o novo código `not_spawned` (o helper não mata processos arbitrários).
- O helper manda `SIGTERM` ao grupo (`killpg(N)`; pgid = pid, `spawn.rs`) e responde `ok`; se o grupo ainda existir após 3 s, manda `SIGKILL`. O `exited` de sempre vem quando o processo termina, e o desmonte automático segue a regra atual.
- C++: `VfsConnector::kill(qint64 pid)`; o padrão não faz nada; o `LinuxVfsConnector` manda o pedido e lança `std::runtime_error` com a mensagem de erro do helper.
- O MO2 e o `mo2-vfs` são distribuídos juntos (`bin/`); sem negociação de versão, como nas mudanças de protocolo das Fases 2 e 3.

## 4. Erros e riscos

| Situação | Tratamento |
|---|---|
| `bin/loot/lootcli` ausente | Botão desativado com a dica "lootcli não encontrado em …" |
| VFS não monta (sem FUSE etc.) | Mensagem de falha de montagem do "Executar"; lootcli não é iniciado |
| Spawn falha (inclui `distrobox-host-exec` ausente) | "failed to start loot: <motivo>" + `releaseIfIdle()` |
| Biblioteca faltando no host | O erro do carregador vai para `lootcli.log`; o diálogo mostra o código de saída e o conteúdo do log |
| Sem rede / HTTP ≠ 200 | `Error downloading masterlist: …`; masterlist anterior preservada; código 1 |
| Masterlist inexistente com `--skipUpdateMasterlist` | Erro, código 1 (§3.3) |
| Cancelar | `kill`; diálogo fecha como cancelado; perfil intacto |
| Helper do VFS cai durante o LOOT | O pid some sem `exited`: falha, com `takeLostReason()` |
| Jogo rodando pelo MO2 ao mesmo tempo | A interface fica travada durante o "Executar" (limitação da Fase 3); não há LOOT simultâneo |
| Compilação Rust lenta no CI | Cache de `build-out/libloot` (§3.2) |

## 5. Testes

Automáticos, no CI Linux (`./build.sh --tests`):

- **`mo2-vfs` (Rust):** parse de `kill`; `spawn` de `sleep 60` + `kill` → `exited` com código 143 em até 5 s; `kill` de pid não iniciado → `not_spawned`; programa que ignora `SIGTERM` recebe `SIGKILL` (`exited` com 137).
- **lootcli (GoogleTest novo, com `BUILD_TESTING`):** resolução da pasta de dados (Flatpak presente; `XDG_DATA_HOME` definido; nenhum dos dois); download por `file://` com libcurl: sucesso, e falha (arquivo inexistente) que preserva a masterlist antiga.
- **App (`organizer_tests`):** `AsyncPipe` lendo arquivo (leituras incrementais, arquivo vazio); parâmetros do `VfsSpawnRequest` do lootcli (lista sem aspas, caminhos, `output`); `lootUnavailableReason`; `LinuxVfsConnector::kill` contra o helper real (como os testes atuais do conector).
- **`tests/loot-e2e` (novo, no estilo de `tests/vfs-e2e`):** instância sintética de Skyrim SE: `Data` do jogo com `Skyrim.esm` e dois mods com plugins do [`testing-plugins`](https://github.com/Ortham/testing-plugins) 1.6.2 (baixado com hash fixo); masterlist local com uma regra `after` (pasta de dados por `XDG_DATA_HOME`, `--skipUpdateMasterlist`); lootcli iniciado pelo `mo2-vfs` montado. Confere que a ordem gerada respeita a regra, que os plugins dos mods entram na ordem (prova do caminho pelo VFS) e que o relatório JSON é válido.
- O download real da masterlist não é testado no CI (rede); fica no teste manual.

Manual no Bazzite (MO2 no box), em `docs/testing/phase4b-manual-checklist.md`:

1. Skyrim SE com mods (os da 4a): "Ordenar" baixa a masterlist na pasta de dados (§1.1), ordena e mostra as mensagens do relatório.
2. A ordem nova aparece na aba de plugins; o jogo abre com ela (SKSE).
3. Com o LOOT Flatpak instalado: uma regra da userlist criada nele é respeitada.
4. Cancelar no meio: perfil intacto; VFS desmontado (`mo2-vfs --list-mounts` vazio).
5. Sem rede, na primeira ordenação da sessão: erro de download claro. Com o modo offline do MO2 ligado (que, como a segunda ordenação da sessão, passa `--skipUpdateMasterlist`), ordena com a masterlist em cache.

## 6. Critérios de pronto

1. Testes novos (Rust, lootcli, app, `loot-e2e`) verdes no CI Linux.
2. CI Windows verde (commit com `[windows-ci]`), porque `loot.cpp` e `vfsconnector.h` mudam.
3. Checklist manual aprovado pelo usuário no Bazzite.
4. README, `STATE.md` e `ROADMAP.md` atualizados: sai a limitação "Sem LOOT"; entram as limitações da §1.3 e a do uso das bibliotecas do host no distrobox.
