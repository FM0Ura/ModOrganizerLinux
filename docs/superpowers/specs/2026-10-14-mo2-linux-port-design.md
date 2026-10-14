# Port nativo do Mod Organizer 2 para Linux — Design

- **Data:** 2026-10-14
- **Status:** aguardando revisão
- **Escopo deste documento:** roteiro geral do port (fases 1–5) e especificação detalhada da **Fase 1 — Build nativo**. As fases 2–5 receberão especificações próprias antes de serem implementadas.

## 1. Objetivo

Disponibilizar para a comunidade um Mod Organizer 2 (MO2) que rode **nativamente no Linux**, com todas as funcionalidades da versão Windows, gerenciando mods de jogos da Steam executados via **Proton**.

### 1.1 Critério de sucesso do projeto

No Bazzite (plataforma de referência), o usuário consegue:

1. instalar o MO2 a partir de um AppImage;
2. criar uma instância para Skyrim Special Edition detectada automaticamente na biblioteca Steam;
3. baixar mods do Nexus via link `nxm://` e instalá-los (inclusive FOMOD);
4. ordenar plugins com LOOT;
5. iniciar o jogo via SKSE com os mods ativos, **sem que a pasta do jogo seja alterada**;
6. usar ferramentas Windows (xEdit, BodySlide, Nemesis/Pandora) através do MO2, enxergando a mesma visão virtual.

### 1.2 Decisões já tomadas

| Tema | Decisão |
|---|---|
| Público | Ferramenta pública, todos os jogos suportados pelo MO2 |
| Compatibilidade | O fork `modorganizer` continua compilando no Windows; código específico isolado por plataforma, para continuar recebendo atualizações do upstream |
| VFS | Sistema de arquivos FUSE próprio (`mo2-vfs`), escrito em **Rust** |
| Linguagem do restante | MO2 permanece em C++/Qt 6; não haverá reescrita em Rust |
| Execução | MO2 inicia jogos e ferramentas **diretamente via Proton** (prefixo do jogo) |
| Escrita em arquivo de mod | Igual ao USVFS: modifica o arquivo real do mod |
| Plataformas | Desktop Linux primeiro (referência: Bazzite); Steam Deck em fase posterior |
| Distribuição | AppImage |
| Desempenho | Medido em todas as fases; otimizações apenas onde benchmarks mostram ganho, em commits isolados |

### 1.3 Fora de escopo (por enquanto)

- Steam Deck / modo Gaming.
- Jogos que não sejam da Steam (GOG/Epic via Heroic/Lutris) — a arquitetura não impede, mas não será testado.
- Criação de arquivos BSA/BA2 no Linux (ver Fase 4).
- Flatpak.

## 2. Arquitetura geral

### 2.1 Repositórios

| Repositório | Papel |
|---|---|
| `FM0Ura/ModOrganizerLinux` | Superprojeto: container de build, `build.sh`, manifests vcpkg para Linux, empacotamento, CI, documentação e specs |
| `FM0Ura/modorganizer` | Fork do aplicativo MO2 com as mudanças multiplataforma |
| `deps/` (no superprojeto) | Dependências do upstream (`cmake_common`, `uibase`, `archive`, `bsatk`, `esptk`, `lootcli`, `dds-header`; depois `plugin_python` e plugins de jogo) importadas via `git subtree --squash` e modificadas no próprio superprojeto; `git subtree pull` traz atualizações do upstream |
| `mo2-vfs` | Crate Rust do VFS; começa como subdiretório do superprojeto e pode virar repositório próprio |

### 2.2 Bloqueios conhecidos no código atual

| Componente | Problema no Linux | Solução | Fase |
|---|---|---|---|
| USVFS | Só funciona no Windows (injeção de DLL) | `VfsConnector` + `mo2-vfs` (FUSE) | 1 (stub), 2 |
| ~40 arquivos com Win32 (`env*`, `spawn`, `processrunner`, `loot`, `shared/util`…) | APIs inexistentes | Implementações Linux do namespace `env` (`src/linux/`) | 1 |
| `envfs` (varredura com `NtQueryDirectoryFile`) | API NT | Implementação com `openat`/`getdents64` | 1 |
| `libbsarch` (Delphi) | Não existe para Linux | Desabilitado no Linux; leitura via `bsatk` | 1 |
| `archive` | Carrega `7z.dll` | Carregar `7z.so` do 7-Zip oficial para Linux | 1 |
| Plugins de jogo | Detecção pelo registro do Windows | Módulo `steam/` | 3 |
| `lootcli` | Executável Windows | Build nativo (libloot suporta Linux) | 4 |

### 2.3 Fases

| Fase | Entrega | Pronto quando |
|---|---|---|
| **1. Build nativo** | MO2 compila e abre no Linux com plugins mínimos (`game_bethesda`, `installer_quick`, `installer_manual`, `installer_fomod`); Win32 isolado em implementações Linux do namespace `env` (`src/linux/`); sem VFS | Gerencia instâncias, perfis, mods e instala arquivos no Bazzite; build Windows continua passando |
| **2. VFS FUSE** | Crate `mo2-vfs` + integração no `VfsConnector` | Testes de integração passando; MO2 monta a visão virtual em diretório de teste |
| **3. Steam/Proton** | Descoberta de jogos/prefixos; execução de `.exe` via Proton com VFS montado | Skyrim SE + SKSE rodando com mods |
| **4. Ecossistema** | Plugins de jogo, `plugin_python`, LOOT, handler `nxm://`, ferramentas | Critério de sucesso 1.1 atendido (exceto AppImage) |
| **5. Distribuição** | AppImage, CI de release, documentação de usuário | Critério de sucesso 1.1 atendido integralmente |

## 3. Resumo de design das fases 2–5

Estas seções registram as decisões; cada fase terá spec própria.

### 3.1 Fase 2 — `mo2-vfs`

- **Entrada:** a mesma `MappingType` (`std::vector<Mapping{source, destination, isDirectory, createTarget}>`) gerada por `OrganizerCore::fileMapping` e hoje enviada ao USVFS.
- **Interface C++:** classe `VfsConnector` com a interface de `UsvfsConnector` (`updateMapping`, `updateParams`, `updateForcedLibraries`); no Windows delega ao USVFS, no Linux ao `mo2-vfs` via API C (`cxx`/`cbindgen`).
- **Pontos de montagem:** um mount FUSE por pasta de destino distinta (ex.: `<jogo>/Data`, `<prefixo>/.../Documents/My Games/<Jogo>`, `<prefixo>/.../AppData/Local/<Jogo>`). Antes de montar, um descritor da pasta original é aberto para que seu conteúdo seja a camada de menor prioridade.
- **Resolução:** árvore em memória construída a partir dos mappings; lookup case-insensitive (chave normalizada), listagem com a grafia original; vencedor = maior prioridade.
- **Leitura:** repassada ao arquivo real; FUSE passthrough (kernel ≥ 6.9) quando disponível.
- **Criação:** arquivos novos vão para `overwrite/` (ou overwrite customizado do executável).
- **Modificação:** altera o arquivo real do mod (comportamento do USVFS).
- **Exclusão/renomeação:** sobre o arquivo real; para a camada base, exclusão mascarada em memória até desmontar.
- **Atualização a quente:** troca atômica da árvore; invalidação do cache do kernel.
- **Ciclo de vida:** monta antes de iniciar o executável, desmonta quando o grupo de processos termina; arquivo de estado permite limpeza (`fusermount3 -u`) após falha.
- **Testes:** unitários da árvore (sem FUSE), integração montando em diretório temporário, benchmarks `readdir`/`open` com ~2.000 mods.

### 3.2 Fase 3 — Steam/Proton

- Módulo `steam/`: lê `libraryfolders.vdf`, `appmanifest_<id>.acf`, `config.vdf` (ferramenta de compatibilidade por jogo) e `compatdata/<appid>`. Suporta Steam nativa, Flatpak (`~/.var/app/com.valvesoftware.Steam`) e bibliotecas em outros discos.
- Caminhos “Meus Documentos”/“AppData” dos plugins de jogo passam a apontar para dentro do prefixo do jogo.
- Execução: monta VFS → `umu-run` (fallback `<proton>/proton run`) com `STEAM_COMPAT_DATA_PATH`, `STEAM_COMPAT_CLIENT_INSTALL_PATH`, `SteamAppId`, `SteamGameId`, `WINEPREFIX` → acompanha processos (`pidfd`, `wineserver -w`) → desmonta.
- Por executável: versão do Proton, variáveis de ambiente e argumentos configuráveis.
- Ferramentas usam o prefixo do jogo.
- **Riscos a validar no início da fase:** visibilidade do mount FUSE dentro do `pressure-vessel`; cache de diretórios do Wine.

### 3.3 Fase 4 — Ecossistema

- Plugins de jogo usando o módulo `steam/` para detecção completa e caminhos dentro do prefixo; demais plugins (BAIN, previews, ferramentas).
- `plugin_python` (pybind11 + Python embutido) portado.
- `lootcli` compilado nativamente.
- Handler `nxm://` via arquivo `.desktop` + `xdg-mime`, registrado na primeira execução.
- Criação de BSA/BA2: avaliar alternativa nativa ao `libbsarch`.

### 3.4 Fase 5 — Distribuição

- AppImage via `linuxdeploy` + plugin Qt, incluindo 7-Zip, `lootcli` e Python.
- CI de release no GitHub Actions; documentação de usuário.

## 4. Fase 1 — Build nativo (especificação detalhada)

### 4.1 Objetivo

O executável `ModOrganizer` compila com GCC no Linux e abre a interface completa. É possível criar instância, gerenciar perfis, instalar/ativar/ordenar mods e ver a árvore de dados. **Não** há VFS nem execução de jogos nesta fase: “Executar” mostra uma mensagem informando que a funcionalidade chega na Fase 3. O build Windows continua funcionando.

### 4.2 Camada de plataforma (`env` + `src/linux/`)

O namespace `env` (`env*.h`) do MO2 já é, na prática, a camada de plataforma; ele é mantido como interface. As implementações Windows existentes permanecem nos arquivos atuais (compilados só no Windows) e as implementações Linux ficam em `src/linux/*_linux.cpp`, selecionadas no CMake por `if(WIN32)`/`else()`. Tipos Windows opacos que aparecem em cabeçalhos (`HANDLE`, `DWORD`, `FILETIME`) vêm do `uibase/win32compat.h` no Linux. Nenhum `#ifdef _WIN32` espalhado pelo restante do código, exceto onde a mudança for de uma linha e um wrapper seria mais confuso que o `#ifdef`.

| Área | Arquivos atuais afetados | Implementação Linux |
|---|---|---|
| Processos (spawn, espera, lista de processos, “bloqueado por processo”) | `spawn.*`, `processrunner.*`, `uilocker.*`, `envmodule.*` | `posix_spawn`/`QProcess`, `pidfd_open` + `poll`, leitura de `/proc` |
| Sistema de arquivos (varredura rápida, propriedades, lixeira) | `envfs.*`, `shared/util.*`, `filetreeitem.cpp`, `savestab.cpp`, `profile.cpp` | `openat`/`getdents64`; lixeira via especificação freedesktop (`gio trash` ou implementação própria) |
| Segurança/privilégios (admin, antivírus, permissões) | `envsecurity.*`, `sanitychecks.cpp` | Verificações não aplicáveis retornam “sem problemas”; checagem de permissão via `access()` |
| Shell (abrir pasta/arquivo, menu de contexto) | `envshell.*`, `envwindows.*` | `QDesktopServices`; menu de contexto do shell desabilitado |
| Atalhos | `envshortcut.*` | Arquivos `.desktop` em `~/.local/share/applications` e área de trabalho |
| Métricas/monitores, DPI | `envmetrics.*` | APIs do Qt (`QScreen`) |
| Erros do sistema | `shared/windows_error.*` | `std::system_error` com `errno` |
| Dumps de falha | `env.cpp`, `envdump.h` | Core dump do sistema + registro de backtrace (`backtrace()`) no log |
| Versão de arquivos/módulos | `envmodule.*`, `env.cpp` | Leitura de versão PE dos `.exe` via biblioteca portátil (ex.: parser PE próprio mínimo) quando necessário; módulos carregados via `/proc/self/maps` |
| Integração com USVFS | `usvfsconnector.*`, `organizercore.*`, `processrunner.*` | `VfsConnector` com implementação nula no Linux (substituída na Fase 2) |
| LOOT | `loot.cpp` | Comunicação com o processo via `QProcess`/pipes; desabilitado até a Fase 4 se o `lootcli` não estiver presente |

A comunicação entre instâncias (`multiprocess.cpp`) já usa `QSharedMemory`/`QLocalServer` e não precisa mudar.

### 4.2.1 Plugins mínimos na Fase 1

Sem plugin de jogo não é possível criar instância, e sem plugins instaladores não é possível instalar mods. Por isso a Fase 1 inclui `game_bethesda` (todos os jogos Bethesda compilam; Skyrim SE é testado), `installer_quick`, `installer_manual` e `installer_fomod`, importados em `deps/`. Detecção do jogo: `findSteamGame` do `uibase` (bibliotecas Steam) ou caminho manual. Pastas `My Games`/`AppData` apontam para as pastas XDG do usuário até a Fase 3.

### 4.3 Dependências

| Dependência | Ação na Fase 1 |
|---|---|
| Qt 6 (Widgets, WebEngine, WebSockets, NetworkAuth) | Pacotes do sistema no container (não via vcpkg) |
| Boost, lz4, zlib, spdlog, GoogleTest | Pacotes do sistema no container (no Linux não usamos vcpkg) |
| `mo2-cmake` (`deps/cmake_common`) | Caminho GCC: `mo2_configure_target` hoje só define C++23 e flags para MSVC; deploy de Qt/PDB vira no-op no Linux |
| `mo2-uibase` (`deps/uibase`) | Implementações Linux em arquivos `*_linux.cpp`; a API pública expõe tipos Windows (`DWORD`, `HANDLE`, `LPCWSTR`, `KNOWNFOLDERID`), mantidos no Linux por um `uibase/win32compat.h` para preservar compatibilidade de código-fonte com plugins |
| `mo2-archive` (`deps/archive`) + 7-Zip | `dlopen` de `7z.so` compilado da fonte oficial do 7-Zip 26.01; tipos COM vêm de `Common/MyWindows.h` do próprio 7-Zip |
| `mo2-bsatk`, `mo2-esptk`, `mo2-dds-header`, `mo2-lootcli-header` | Compilar no Linux; patches pontuais se necessário |
| `mo2-libbsarch` | Não usado no Linux; código que depende dele protegido por opção CMake `MO2_WITH_LIBBSARCH` (ON no Windows, OFF no Linux) |
| `usvfs` | Não usado no Linux |

No Windows nada muda: o build continua usando vcpkg e o registry oficial do MO2. Por isso as mudanças no `modorganizer` precisam compilar também contra o `uibase` **upstream** no Windows (o CI Windows do fork usa o upstream).

### 4.4 Build

- **Container:** `ModOrganizerLinux/build/Containerfile` baseado na mesma versão do Fedora usada pelo Bazzite atual (`fedora:44` na data deste documento) com GCC, CMake, Ninja, Qt 6 devel, Rust (`rustup`, para a Fase 2), Boost, spdlog, lz4, zlib e GoogleTest. Usável com `podman`/`docker` no CI e via `distrobox` no Bazzite.
- **Script:** `ModOrganizerLinux/build.sh` — compila `deps/*` e o `modorganizer` (caminho passado por `MO2_SRC`, por padrão `../modorganizer`) na ordem de dependência e instala em `./install/`.
- **CMake:** novo preset `linux-gcc` em `CMakePresets.json` (Ninja, sem vcpkg, `-Wall -Wextra`). Presets Windows inalterados. No Linux: sem `WIN32_EXECUTABLE`, sem `.rc`/manifests, sem `Shlwapi`/`Bcrypt`/`Version`/`Dbghelp`.
- **Instalação Linux:** mesmo layout do Windows — `bin/ModOrganizer` com `plugins/`, `stylesheets/`, `tutorials/`, `translations/` e `resources/` ao lado — mais `lib/` para as bibliotecas, com `RPATH` relativo. Assim o código que localiza recursos via `applicationDirPath()` não muda.
- **Caminhos de dados:** instâncias globais em `$XDG_DATA_HOME/ModOrganizer` (equivalente a `%LOCALAPPDATA%/ModOrganizer`); instâncias portáteis continuam funcionando ao lado do executável.

### 4.5 Comportamento de funcionalidades ainda indisponíveis

- Executar programas/jogos: diálogo informativo; nenhuma falha.
- LOOT: botão desabilitado com dica, se `lootcli` ausente.
- Criação de BSA: opção oculta no Linux.
- Atualização automática (`selfupdater`): desabilitada no Linux até a Fase 5.
- Plugins de jogo sem suporte a Linux: a instância pode ser criada informando o caminho do jogo manualmente.

### 4.6 Desempenho

- Benchmark `refresh` (tempo de `DirectoryRefresher` + construção de `DirectoryEntry`) usando uma instância sintética gerada por script com 2.000 mods / 200.000 arquivos.
- Registrar tempo de inicialização até a janela principal.
- Resultados salvos em `ModOrganizerLinux/benchmarks/results/` como linha de base para as próximas fases. Nenhuma otimização é obrigatória na Fase 1; a varredura `getdents64` deve ser no mínimo equivalente à implementação Windows em ordem de grandeza.

### 4.7 Testes

- **Unitários (novos, GoogleTest):** implementações Linux em `src/linux/` — varredura de diretórios, conversão de caminhos, atalhos `.desktop`, lixeira, lista de processos.
- **CI Linux (GitHub Actions):** build no container + testes unitários + `ModOrganizer --help` executando sem erro (smoke test headless via `QT_QPA_PLATFORM=offscreen`).
- **CI Windows:** build do fork com o preset `vs2022-windows` para garantir compatibilidade.
- **Manual no Bazzite:** criar instância (Skyrim SE com caminho manual), criar perfil, instalar 3 mods de arquivos `.7z/.zip/.rar` (incluindo um FOMOD), reordenar, ver aba Data e conflitos, reabrir o MO2 e confirmar persistência.

### 4.8 Critérios de pronto da Fase 1

1. `build.sh` gera `install/bin/ModOrganizer` a partir de um container limpo.
2. Checklist manual da seção 4.7 aprovado no Bazzite.
3. CI Linux e CI Windows verdes.
4. Linha de base de desempenho registrada.
5. Nenhum `#include <windows.h>` fora de arquivos `*_win.cpp` ou blocos `#ifdef _WIN32` justificados.

### 4.9 Riscos

| Risco | Mitigação |
|---|---|
| `std::wstring` usado em todo o código (`DirectoryEntry` etc.) — `wchar_t` tem 4 bytes no Linux | Manter `wstring` na Fase 1 (funciona, só usa mais memória); medir no benchmark e avaliar migração para UTF-8/`QString` como otimização |
| Comparação de caminhos case-insensitive no MO2 depende de APIs Windows | Implementar comparação case-insensitive em `src/linux/` com `QString::compare(..., Qt::CaseInsensitive)` / casefold Unicode |
| Separador `\\` hard-coded em muitos lugares | Normalizar em `src/linux/` e corrigir pontos encontrados; teste que percorre a árvore de dados |
| `mo2-cmake` muito acoplado ao MSVC | Fork com caminho Linux mínimo |
| Qt WebEngine pesado/ausente em algumas distros | Fornecido no container e no AppImage |
| Divergência com o upstream | Mudanças pequenas e isoladas; merge periódico do upstream documentado |
