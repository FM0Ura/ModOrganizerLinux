# Fase 4c.1 — Plugins C++ restantes — Design

- **Data:** 2026-10-20
- **Status:** em revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md) (§3.3 resume a Fase 4; este documento a substitui onde divergirem)
- **Divisão da Fase 4:** [`2026-10-18-phase4a-nexus-nxm-keyring-design.md`](2026-10-18-phase4a-nexus-nxm-keyring-design.md) §1.1; a 4c foi dividida em 4c.1 (este documento), 4c.2 (pendências adiadas da Fase 2) e 4c.3 (ferramentas pelo VFS), cada uma com spec, plano e PR próprios (decisão de 2026-10-20)
- **Branch:** `fase-4c1-plugins` (a partir de `fase-4b-loot`; o PR mira `fase-4b-loot` até a 4b entrar no `main`)

## 1. Objetivo

Parte de "todas as funcionalidades" (spec geral §1.1): os plugins C++ do upstream que ainda não existem no Linux passam a compilar, carregar e funcionar no MO2 nativo. Meta: no Bazzite, o usuário instala um mod BAIN, pré-visualiza arquivos e BSAs, extrai BSAs na instalação, vê os diagnósticos do MO2, edita INIs pelo INI Editor (com INIs por perfil que o INI Bakery aplica ao jogo) e, se usa FNIS no Skyrim (LE), é avisado para rodá-lo antes do jogo.

### 1.1 Plugins desta fase

| Plugin | Repositório (`ModOrganizer2/…`) | Commit (`master`) | Data | Interface |
|---|---|---|---|---|
| `installer_bain` | `modorganizer-installer_bain` | `0e60c19` | 2026-10-06 | `IPluginInstallerSimple` |
| `installer_bundle` | `modorganizer-installer_bundle` | `1d7f9dc` | 2025-05-29 | `IPluginInstallerSimple` |
| `preview_base` | `modorganizer-preview_base` | `a8b2c51` | 2026-10-06 | `IPluginPreview` |
| `preview_bsa` | `modorganizer-preview_bsa` | `fb477c0` | 2026-10-06 | `IPluginPreview` |
| `diagnose_basic` | `modorganizer-diagnose_basic` | `558e358` | 2026-10-06 | `IPluginDiagnose` |
| `check_fnis` | `modorganizer-check_fnis` | `53522c3` | 2026-10-06 | `IPlugin` |
| `bsa_extractor` | `modorganizer-bsa_extractor` | `d6b32b9` | 2026-10-06 | `IPlugin` |
| `tool_inieditor` | `modorganizer-tool_inieditor` | `18b0e1c` | 2026-10-06 | `IPluginTool` |
| `tool_inibakery` | `modorganizer-tool_inibakery` | `355cac9` | 2026-10-06 | `IPlugin` (+ diagnóstico, conferir) |

Os commits são o `master` em 2026-10-20 (abreviados; o plano usa o hash completo do `git subtree add`). A interface de cada um é conferida no código antes de fixar a checagem de carga (§4).

### 1.2 Decisões desta fase

| Tema | Decisão |
|---|---|
| Divisão da 4c | Três subfases (4c.1 plugins C++, 4c.2 pendências da Fase 2, 4c.3 ferramentas pelo VFS), um PR cada; branches em sequência (`fase-4c1-plugins` a partir de `fase-4b-loot`), cada PR mira o branch anterior até o anterior entrar no `main`. |
| Importação | `git subtree add --squash` em `deps/<plugin>`, no `master` do upstream (regra de versão de `deps/README.md`); patches no próprio superprojeto, commits `deps/<plugin>: Linux: …`. |
| Princípio dos patches | A lógica dos plugins não muda; só chamadas Win32 vão para `#ifdef _WIN32` / `#else`, com o código Windows idêntico. Se um plugin exigir uma correção grande, parar e perguntar ao usuário. |
| `diagnose_basic`: atributos NTFS | A checagem e o "conserto" de atributos de arquivo do `overwrite` (oculto, sistema, comprimido, esparso; `GetFileAttributes`/`SetFileAttributes`/`DeviceIoControl`) ficam só no Windows: no Linux não rodam e a opção `check_fileattributes` não é registrada. ext4/btrfs não têm esses atributos e o Wine não os grava. |
| `tool_inieditor`: editor externo | `ShellExecuteW` ("open"/"edit") vira `shell::Open(arquivo)` do `uibase` (`xdg-open`; no distrobox, o shim abre no host). A opção "associated" (abrir × editar) não tem equivalente no Linux e não tem efeito lá. |
| INIs do perfil no Linux (`game_bethesda`; decisão do usuário em 2026-10-20, opção A: corrigir nesta fase) | Achado na revisão final: o `game_bethesda` procurava os INIs do prefixo (`Skyrim.ini`, `SkyrimPrefs.ini`) e o INI padrão do jogo (`Skyrim_Default.ini`) com os nomes minúsculos de `iniFiles()`, e no Linux criava os INIs do perfil vazios, que o `tool_inibakery` passa a mapear sobre os reais. No Linux: `GameGamebryo::resolveIgnoringCase()` acha o arquivo ignorando maiúsculas (no Windows devolve o caminho dado) em `copyToProfile()`, nas checagens INI do usuário × INI padrão de `initializeProfile()` de cada jogo, no INI de `prepareIni()` e no INI dos saves locais (`Skyrimcustom.ini` é o `skyrimcustom.ini` do perfil); um INI do perfil vazio, ou só com `[Launcher] bEnableFileSelection=1`, é restaurado da origem não vazia (mantendo `bEnableFileSelection=1`), nunca outro conteúdo, quando o perfil é aberto (`onProfileChanged`, também na abertura do MO2) e antes de cada execução. Junto: os saves locais passam a ser mapeados para `__MO_Saves` (no Linux o `QDir` mantinha a `\` do caminho Windows no nome) e `Profile::absoluteIniFilePath()` do app ignora maiúsculas no Linux (INI Editor com INIs por perfil desligados). Visão Windows: só a declaração e a definição identidade de `resolveIgnoringCase()` e as checagens de `initializeProfile()` que a chamam, com a mesma semântica. |
| Separadores de caminho (`check_fnis`, `preview_bsa`) | No Linux, os padrões do FNIS aceitam `/` e `\` (eles começavam com `\` literal e eram comparados com caminhos com `/`), e os caminhos do BSA (com `\`) são divididos em pastas na pré-visualização; Windows sem mudança. |
| Fora de escopo | `installer_omod` e `installer_fomod_csharp` (C++/CLI e .NET, só Windows); `bsapacker` (Fase 4e); plugins Python (Fase 4d). |

## 2. Build

- Um alvo por plugin no `build.sh`, depois de `installer_fomod`, com `cmake_build` e instalação em `bin/plugins`, como `game_bethesda` e os instaladores.
- `preview_bsa` e `bsa_extractor` pedem `find_package(Boost CONFIG REQUIRED COMPONENTS thread interprocess)`; o Fedora não instala pacote CMake para o `interprocess` (só cabeçalhos; o app já contorna, `deps/modorganizer/src/CMakeLists.txt`). No Linux, o patch pede só `thread` e liga `Boost::headers`; no Windows, o `find_package` original.
- O que mais falhar na compilação recebe patch pontual pelo princípio da §1.2.
- CI Windows: sem mudança (usa os plugins do upstream pelo vcpkg); nossos patches ficam em `#else`, conferidos por análise (`unifdef`), como nas fases anteriores.

## 3. Comportamento no Linux

| Plugin | No Linux |
|---|---|
| `installer_bain` | Sem mudança de lógica: detecta pacotes BAIN (pastas `00 Core`, `10 …`) na árvore do arquivo e mostra o diálogo de escolha. |
| `installer_bundle` | Sem mudança: arquivos que contêm outros arquivos, pelo `archive` (7z.so) já portado. |
| `preview_base` | Sem mudança: texto, imagens e INI pela Qt. DDS não: o `preview_base` sempre pula `dds` ("handled by the dds preview plugin"), então a pré-visualização de DDS fica indisponível até o plugin de pré-visualização DDS (Python, Fase 4d). |
| `preview_bsa` | Conteúdo de BSA/BA2 pelo `bsatk`, em árvore de pastas; o patch de Boost e, no Linux, a conversão de `\` para `/` dos caminhos do arquivo antes do `cleanPath()` (§1.2). |
| `bsa_extractor` | Na instalação de um mod com BSA, oferece extrair (opção do plugin), pelo `bsatk`; só o patch de Boost. |
| `diagnose_basic` | Checagens de sempre; a de atributos NTFS em `#ifdef _WIN32` (§1.2). |
| `check_fnis` | Só no Skyrim (LE): o plugin exige o jogo "Skyrim" (`gameDependency("Skyrim")`), então fica inativo no Skyrim SE, como no upstream. Antes de executar, se os dados do FNIS mudaram, pergunta e roda o `GenerateFNISforUsers.exe` por `IOrganizer::startApplication()` (Proton + VFS, como o "Executar", Fase 3) e espera com `waitForApplication()`; no Linux, `HANDLE`/`DWORD` vêm do `win32compat` do `uibase`, e o `#include <Windows.h>` fica em `#ifdef _WIN32`; os padrões dos arquivos do FNIS aceitam `/` (§1.2). |
| `tool_inieditor` | Editor interno (TextViewer) sem mudança; modo externo por `shell::Open` (§1.2). |
| `tool_inibakery` | Sem patch: mapeia os INIs do perfil (`iniFiles()`) sobre os do prefixo do Proton (o VFS ignora maiúsculas, `skyrim.ini` cobre `Skyrim.ini`); depende da correção do `game_bethesda` (§1.2) para que os INIs do perfil não fiquem vazios. |
| `game_bethesda` | INIs do perfil copiados do prefixo e do INI padrão do jogo ignorando maiúsculas; INIs do perfil vazios restaurados; INI dos saves locais e pasta `__MO_Saves` corretos (§1.2). |

## 4. Testes

Automáticos (CI Linux, `./build.sh --tests`):

- **Carga:** `tests/plugin-load` ganha os tipos `preview` (`IPluginPreview`), `tool` (`IPluginTool`), `diagnose` (`IPluginDiagnose`) e `plugin` (`IPlugin`); cada um dos nove `.so` precisa carregar com `QPluginLoader` e implementar a interface de §1.1, chamado depois do alvo de cada plugin, como hoje.
- **Smoke:** no passo da janela principal (instância Skyrim SE de teste), o log do MO2 não tem falha de carga de plugin e os nove aparecem como carregados (a linha exata do `PluginContainer` é fixada no plano).
- **INIs do perfil (`game_bethesda`):** testes GoogleTest com pastas temporárias: cópia de `Skyrim.ini` para `skyrim.ini`, INI padrão `Skyrim_Default.ini` sem INI do usuário, INI do perfil de 0 bytes e o de 36 bytes (`[Launcher]`) restaurados, INI do perfil com conteúdo mantido, INI dos saves locais com outra caixa, pasta `__MO_Saves` e o reparo ao abrir o perfil.
- **Código Linux novo:** teste do modo externo do `tool_inieditor` (chama `shell::Open` com o caminho do INI do perfil) se o plugin for testável com um dublê de `IOrganizer`; senão, só o teste manual. `diagnose_basic`: no Linux a opção `check_fileattributes` não está nas configurações do plugin (`settings()`).

Manual no Bazzite, em `docs/testing/phase4c1-manual-checklist.md`:

1. Instalar um mod BAIN (pacote com `00 Core`/`10 …`): o diálogo BAIN aparece; as opções escolhidas viram os arquivos do mod.
2. Instalar um "bundle" (arquivo com outro arquivo dentro): o MO2 oferece o arquivo interno.
3. Pré-visualizar um `.txt`/`.ini` e uma imagem pela aba Data; ver o conteúdo de um `.bsa` pela pré-visualização.
4. Em Configurações → Plugins → BSA Extractor, desligar "only trigger bsa extraction for alternate game sources" (padrão: ligado), instalar um mod com BSA e responder Sim em "Extract BSA": os arquivos extraídos aparecem no mod.
5. Deixar arquivos no `overwrite`: aviso no ícone de problemas; não há opção de atributos de arquivo nas configurações do plugin.
6. INI Editor (Ferramentas): editar e salvar um INI do perfil; com "Use an external editor to open the files" ligado nas configurações do plugin, abre no editor do host.
7. Só no Skyrim (LE) e para quem usa FNIS (o `check_fnis` fica inativo no Skyrim SE): mudar os arquivos de animação que ele confere (`FNIS_*_List.txt`, `FNIS*Behavior.txt`, `PatchList.txt`, `skeleton*.hkx`) e executar o jogo: pergunta "Run FNIS before …?" e o FNIS roda pelo Proton.
8. INI Bakery com INIs por perfil: ao abrir a instância, `skyrim.ini`/`skyrimprefs.ini` do perfil não estão mais vazios (cópias do prefixo); um valor visível mudado pelo INI Editor vale no jogo; os INIs reais do prefixo não mudam.
9. Saves locais: um save novo aparece nos saves do perfil e o jogo o carrega.
10. INI Editor com INIs por perfil desligados: abre os INIs do prefixo (nomes sem diferenciar maiúsculas).

## 5. Riscos

| Risco | Tratamento |
|---|---|
| Um plugin usa Win32 em cabeçalho do `uibase` que não tem equivalente no `win32compat` | Patch pontual pelo princípio da §1.2; se grande, parar e perguntar. |
| `check_fnis`: o `.exe` do FNIS precisa do prefixo do jogo e do VFS | Coberto pelo `startApplication()` da Fase 3; conferido no teste manual (item 7, opcional). |
| Pré-visualização de DDS | Indisponível até o plugin de pré-visualização DDS (Python, Fase 4d): o `preview_base` sempre pula `dds`; documentado como limitação. |
| INIs do perfil vazios no Linux (nomes com maiúsculas no prefixo), que o INI Bakery mapearia sobre os reais | Corrigido nesta fase (§1.2): buscas ignorando maiúsculas e reparo dos INIs vazios ao abrir o perfil e antes de executar; o reparo só troca um INI vazio (ou só com `[Launcher] bEnableFileSelection=1`) por uma cópia não vazia da origem e registra cada troca no log; checklist itens 8 a 10. |
| `check_fnis` inativo no Skyrim SE | Como no upstream (`gameDependency("Skyrim")`); documentado no checklist (item 7). |
| `subtree pull` de `check_fnis` ou `game_bethesda` perder correções que não estão no upstream | Registrado em `deps/README.md` ("Notes on individual pins"). |
| `shell::Open` sem `xdg-open` (CI, hosts mínimos) | Falha registrada no log, sem travar; não exercitado no CI. |

## 6. Critérios de pronto

1. CI Linux verde (carga dos nove plugins e smoke).
2. CI Windows verde (commit com `[windows-ci]`).
3. Checklist manual aprovado pelo usuário no Bazzite.
4. `deps/README.md` (pins), README, `STATE.md` e `ROADMAP.md` atualizados (4c dividida em 4c.1–4c.3).
5. PR próprio `fase-4c1-plugins` → `fase-4b-loot` (redirecionado ao `main` depois do merge da 4b).
