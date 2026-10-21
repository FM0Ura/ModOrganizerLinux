# Fase 4d.2 — `basic_games` no Linux — Design

- **Data:** 2026-10-21
- **Status:** em revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md) (§3.3 resume a Fase 4; este documento a substitui onde divergirem)
- **Divisão da 4d (decisão do usuário, 2026-10-21):** 4d.1 é o `plugin_python` e os plugins Python pequenos ([`2026-10-21-phase4d1-python-design.md`](2026-10-21-phase4d1-python-design.md)); esta é a 4d.2, o `basic_games`.
- **Branch:** `fase-4d2-basic-games` (a partir de `fase-4d1-python`, no worktree `../ModOrganizerLinux-4d2`; o PR mira `fase-4d1-python` até as anteriores entrarem no `main`)

## 1. Objetivo

Os jogos do `basic_games` (o meta-plugin Python do MO2 com ~80 jogos não Bethesda) são detectados pela Steam e usam as pastas do prefixo do Proton de cada jogo, como no Windows usam as do usuário. Meta: NieR:Automata, Cyberpunk 2077 e Baldur's Gate 3 do usuário funcionam no MO2 do Linux — detecção, instalação de mods, execução pelo VFS e, no BG3, a ordem de carga (`modsettings.lsx`).

### 1.1 Escopo (decisão do usuário, 2026-10-21)

| Item | Nesta fase |
|---|---|
| Base comum Steam/Proton (detecção e pastas no prefixo) | Para todos os jogos do `basic_games` |
| Validação real | NieR:Automata, Cyberpunk 2077, Baldur's Gate 3 (Steam do usuário) |
| Ordem de carga do BG3 (LSLib/`Divine`) | Sim |
| Stellar Blade | Fora (não há plugin no upstream; plugin novo fica para depois) |
| GOG, Epic, Origin, EA Desktop | Fora no Linux (fora de escopo do projeto: ROADMAP, "Jogos fora da Steam") |

### 1.2 Decisões desta fase

| Tema | Decisão |
|---|---|
| Fonte da verdade da Steam (abordagem A, decisão do usuário) | O `steam::findApp()` do `uibase` (Fase 3: Steam nativa e Flatpak, bibliotecas em outros discos, `compatdata` na biblioteca do jogo ou na principal), exposto ao Python por um binding novo do `mobase`, só no Linux. Descartadas: reimplementar em Python (duplicaria e divergiria do C++); misto Python/C++. |
| Pastas do usuário | No Linux, `%USERPROFILE%` é `<compatdata>/pfx/drive_c/users/steamuser` e `%DOCUMENTS%` é `…/steamuser/Documents` **do prefixo do jogo**; sem prefixo (jogo nunca rodado pelo Proton), as pastas XDG de hoje, com aviso no log (como o `game_bethesda`, Fase 3). |
| LSLib no BG3 | `dotnet <tools>/Divine.dll` com o `dotnet-runtime-8.0` do Fedora (o `Divine` do `ExportTool` é .NET 8 portável, `Microsoft.NETCore.App`), em vez de `Divine.exe`. Sem `dotnet` no sistema: aviso ao usuário com o pacote a instalar. Confirmado por um spike no início do plano; se o `Divine.dll` não ler `.pak` no Linux, o plano cai para rodar o `Divine.exe` por Proton no prefixo do BG3 (registrado como risco). |
| Importação e patches | `git subtree add --squash` do `master` do `modorganizer-basic_games` em `deps/basic_games`; patches `deps/basic_games: Linux: …` em `if sys.platform == "win32":` / `else:`; o binding em `deps/plugin_python: Linux: …` (`#ifndef _WIN32`). Windows idêntico. |
| Dependências `pip` | As do upstream (`plugin-requirements.txt`: `psutil`, `vdf`, `lzokay`, `pyyaml`), instaladas pelo `cmake_common` na pasta do plugin, como o `installer_wizard` da 4d.1 (compilando extensões C quando não houver wheel; o box e o CI têm o compilador). |

## 2. `mobase.steamApp` (`deps/plugin_python`)

Função nova do módulo `mobase`, só no Linux (`#ifndef _WIN32`), em `wrappers/utils.cpp` (ao lado de `getKnownFolder`, no submódulo `mobase.utils` se é lá que as utilidades ficam):

```
steamApp(app_id: str) -> dict[str, str] | None
```

Chama `MOBase::steam::findApp(app_id)`; devolve `None` se o jogo não está instalado, ou um `dict` com `appId`, `root`, `library`, `gamePath` e `compatData` (string vazia se o jogo nunca rodou pelo Proton). Os stubs (`mobase-stubs`, se estiverem no subtree) ganham a assinatura.

## 3. `basic_games` no Linux (`deps/basic_games`)

### 3.1 Detecção (`steam_utils.py`)

No Linux, `find_games()` não usa `winreg`: devolve um mapeamento preguiçoso (`collections.abc.Mapping` de app ID → `Path`) cujas consultas (`in`, `[]`, `get`) chamam `mobase.utils.steamApp(id)` (ou o caminho real do binding) e devolvem `Path(gamePath)`; a iteração (usada só por `__main__`/depuração) devolve vazio. O formato que o resto do `basic_games` consome (`dict[str, Path]`) não muda. `gog_utils`, `epic_utils`, `origin_utils`, `eadesktop_utils` no Linux devolvem `{}` sem erro (sem `winreg`), registrando uma vez que só a Steam é suportada.

### 3.2 Pastas (`basic_game.py`)

A expansão de `%DOCUMENTS%` e `%USERPROFILE%` (função que hoje usa `QStandardPaths`) passa a receber o jogo e, no Linux, usa o prefixo dele: `steamApp(<GameSteamId>)["compatData"] + "/pfx/drive_c/users/steamuser"` (e `/Documents`). Vale para `GameDocumentsDirectory`, `GameSavesDirectory` e os padrões de `documentsDirectory`/`savesDirectory` que hoje usam `DocumentsLocation`. Sem `GameSteamId`, sem jogo instalado ou sem prefixo: o comportamento atual (XDG) e um aviso no log, uma vez por jogo. `%GAME_PATH%` e `%GAME_DOCUMENTS%` não mudam.

### 3.3 BG3 (`games/baldursgate3`)

- `pak_parser.py`: no Linux, o comando é `dotnet <tools_dir>/Divine.dll …` (mesmos argumentos), sem `creationflags=CREATE_NO_WINDOW` (só existe no Windows); `dotnet` achado no `PATH`; ausente → mensagem ao usuário (`dotnet-runtime-8.0` no box `mo2-run`) e a geração do `modsettings.lsx` é pulada, como hoje quando o LSLib falta.
- `lslib_retriever.py`: o download do `ExportTool` do GitHub e a extração não mudam; a lista de arquivos necessários inclui `Divine.dll` e `Divine.runtimeconfig.json`, já presentes.
- O mapeamento do `modsettings.lsx` (`bg3_file_mapper.py`) aponta para `%USERPROFILE%/AppData/Local/Larian Studios/…` — resolvido pelo §3.2 no prefixo do jogo.
- Caminhos com `\` e `.exe` revisados como na 4d.1 (Task 4).

### 3.4 Cyberpunk 2077 (`game_cyberpunk2077.py`)

O `redMod.exe` roda por `organizer.startApplication()` (Proton com o VFS, Fases 3 e 4c.3). Revisão de caminhos `\`/`.exe` e de argumentos com caminhos (que vão para um programa Windows: `Z:\…`, como o `fnistool` da 4d.1).

### 3.5 Instalação

`bin/plugins/basic_games/` (módulo), com `lib/` das dependências `pip`; o MO2 já o carrega pelo `plugin_python` da 4d.1.

## 4. Dependências

`dotnet-runtime-8.0` no `build/Containerfile` e no box (README); só o BG3 o usa em tempo de execução.

## 5. Testes

- **`mobase.steamApp`:** teste Python (pytest do `plugin_python`) com uma Steam falsa num `HOME` temporário (`libraryfolders.vdf`, `appmanifest_<id>.acf`, `compatdata/<id>/pfx`): devolve o `dict` certo; `None` para um app ausente; `compatData` vazio sem `pfx`.
- **`basic_games` com Steam falsa:** teste Python (pytest, com `mobase` disponível) que carrega o `basic_games` e, para NieR (524220), Cyberpunk (1091500) e BG3 (1086940), confere `isInstalled`/`gameDirectory()` e que `documentsDirectory()`/`savesDirectory()` do Cyberpunk e do BG3 estão no prefixo do seu app ID; e o fallback XDG sem prefixo.
- **BG3/LSLib:** spike no início do plano (`dotnet Divine.dll -g bg3 -l info` lendo um `.pak` real de mod do BG3); depois, um teste automatizado só se houver um `.pak` pequeno redistribuível — senão, coberto pelo teste manual.
- **Smoke:** o `basic_games` carregado (a verificação da 4d.1 já cobre pacotes Python em `bin/plugins`).
- **Manual (usuário):** NieR, Cyberpunk e BG3 — instância nova de cada pelo MO2, detecção, instalar um mod, executar pelo VFS, e no BG3 a ordem de carga gerada no prefixo.

## 6. Riscos

| Risco | Mitigação |
|---|---|
| `Divine.dll` não roda no .NET do Linux (dependência nativa só Windows) | Spike no início; fallback: `Divine.exe` por Proton no prefixo do BG3 (mais lento). |
| Extensões C do `pip` (`psutil`, `lzokay`) sem wheel para 3.14 | O `pip` compila (gcc e `python3-devel` no box e no CI); falha de build clara. |
| Um jogo do `basic_games` usa outra pasta especial do Windows além de `%USERPROFILE%`/`%DOCUMENTS%` | A revisão do §3.2 lista as variáveis usadas pelos ~80 jogos; as que não forem mapeadas ficam registradas como limitação. |
| Plugins do upstream com código só Windows não previsto | Revisão por grep (como na 4d.1) só nos três jogos validados e no núcleo; os demais jogos ficam "detectados, não validados". |

## 7. Critérios de pronto

- `mobase.steamApp` e o `basic_games` com Steam falsa passam nos testes.
- No Bazzite, NieR, Cyberpunk e BG3 são detectados, instalam e executam mods pelo VFS; o BG3 gera a ordem de carga no prefixo.
- Smoke, `./build.sh --tests` e CIs verdes.
- README (pacote `dotnet-runtime-8.0`, `basic_games`), checklist manual, ROADMAP e STATE atualizados.
