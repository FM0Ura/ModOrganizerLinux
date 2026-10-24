# Fase 4d.2 — `basic_games` no Linux — Design

- **Data:** 2026-10-21
- **Status:** em revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md) (§3.3 resume a Fase 4; este documento a substitui onde divergirem)
- **Divisão da 4d (decisão do usuário, 2026-10-21):** 4d.1 é o `plugin_python` e os plugins Python pequenos ([`2026-10-21-phase4d1-python-design.md`](2026-10-21-phase4d1-python-design.md)); esta é a 4d.2, o `basic_games`.
- **Branch:** `fase-4d2-basic-games` (a partir de `fase-4d1-python`, no worktree `../ModOrganizerLinux-4d2`; o PR mira `fase-4d1-python` até as anteriores entrarem no `main`)

## 1. Objetivo

Os jogos do `basic_games` (o meta-plugin Python do MO2 com ~80 jogos não Bethesda) são detectados pela Steam e usam as pastas do prefixo do Proton de cada jogo, como no Windows usam as do usuário. Meta: NieR:Automata, Cyberpunk 2077 e Baldur's Gate 3 do usuário funcionam no MO2 do Linux — detecção, instalação de mods, execução pelo VFS. A ordem de carga do BG3 (`modsettings.lsx`) fica para depois (§1.3).

### 1.1 Escopo (decisão do usuário, 2026-10-21)

| Item | Nesta fase |
|---|---|
| Base comum Steam/Proton (detecção e pastas no prefixo) | Para todos os jogos do `basic_games` |
| Validação real | NieR:Automata, Cyberpunk 2077, Baldur's Gate 3 (Steam do usuário) |
| Ordem de carga do BG3 (LSLib/`Divine`) | Não: desativada no Linux com aviso (§1.3, decisão do usuário depois do spike) |
| Stellar Blade | Fora (não há plugin no upstream; plugin novo fica para depois) |
| GOG, Epic, Origin, EA Desktop | Fora no Linux (fora de escopo do projeto: ROADMAP, "Jogos fora da Steam") |

### 1.2 Decisões desta fase

| Tema | Decisão |
|---|---|
| Fonte da verdade da Steam (abordagem A, decisão do usuário) | O `steam::findApp()` do `uibase` (Fase 3: Steam nativa e Flatpak, bibliotecas em outros discos, `compatdata` na biblioteca do jogo ou na principal), exposto ao Python por um binding novo do `mobase`, só no Linux. Descartadas: reimplementar em Python (duplicaria e divergiria do C++); misto Python/C++. |
| Pastas do usuário | No Linux, `%USERPROFILE%` é `<compatdata>/pfx/drive_c/users/steamuser` e `%DOCUMENTS%` é `…/steamuser/Documents` **do prefixo do jogo**; sem prefixo (jogo nunca rodado pelo Proton), as pastas XDG de hoje, com aviso no log (como o `game_bethesda`, Fase 3). |
| LSLib no BG3 | Não roda no Linux (§1.3): a geração do `modsettings.lsx` fica desativada no Linux, com aviso; o resto do plugin do BG3 funciona. |
| Importação e patches | `git subtree add --squash` do `master` do `modorganizer-basic_games` em `deps/basic_games`; patches `deps/basic_games: Linux: …` em `if sys.platform == "win32":` / `else:`; o binding em `deps/plugin_python: Linux: …` (`#ifndef _WIN32`). Windows idêntico. |
| Dependências `pip` | As do upstream (`plugin-requirements.txt`: `psutil`, `vdf`, `lzokay`, `pyyaml`), instaladas pelo `cmake_common` na pasta do plugin, como o `installer_wizard` da 4d.1 (compilando extensões C quando não houver wheel; o box e o CI têm o compilador). |

### 1.3 Spike do LSLib (2026-10-21) e decisão

O `Divine` do LSLib v1.20.4 (`ExportTool`, .NET 8) não funciona no Linux: em toda ação, `Divine.CLI.CommandLineActions.TryToValidatePath` faz `Uri.TryCreate(path, RelativeOrAbsolute)` e depois `result.IsFile` sem `try`; no Unix um caminho `/…` vira URI relativa e o `IsFile` lança `InvalidOperationException` (saída 134). Todas as grafias testadas falham (`file://`, `Z:/`, `Z:\`, relativa). O runtime (.NET 8.0.31 do Fedora) está bem; o defeito é do LSLib. **Decisão do usuário:** a ordem de carga do BG3 fica para depois — desativada no Linux com aviso; o defeito é reportado ao LSLib (com a confirmação do usuário antes de publicar); quando corrigido, o caminho `dotnet Divine.dll` é pequeno. Descartados nesta fase: `Divine.exe` por Proton (segundos por chamada e captura da saída pelo host) e um leitor de `.pak` em Python (engenharia nova e outras ações do `Divine` a cobrir).

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

- No Linux, a geração do `modsettings.lsx` (o ponto de entrada em `bg3_utils.py`, `construct_modsettings_xml`, e qualquer outro caminho que chame o LSLib: download em `lslib_retriever.py`, `pak_parser.run_divine`) não roda: nada é baixado do GitHub nem executado; o usuário vê uma vez (`QMessageBox` ou o mesmo canal que o plugin já usa para a falta do LSLib) que a ordem de carga do BG3 ainda não é gerada no Linux e deve ser feita por fora (ex.: pelo jogo/mod manager do jogo), e o log registra o motivo. O resto do plugin (detecção, instalação, verificação de dados, mapeamento de arquivos) funciona.
- O mapeamento do `modsettings.lsx` existente do perfil (`bg3_file_mapper.py`) continua apontando para `%USERPROFILE%/AppData/Local/Larian Studios/…` — resolvido pelo §3.2 no prefixo do jogo (um `modsettings.lsx` feito à mão no perfil chega ao jogo).
- Caminhos com `\` e `.exe` revisados como na 4d.1 (Task 4).

### 3.4 Cyberpunk 2077 (`game_cyberpunk2077.py`)

O `redMod.exe` roda por `organizer.startApplication()` (Proton com o VFS, Fases 3 e 4c.3). Revisão de caminhos `\`/`.exe` e de argumentos com caminhos (que vão para um programa Windows: `Z:\…`, como o `fnistool` da 4d.1).

### 3.5 Instalação

`bin/plugins/basic_games/` (módulo), com `lib/` das dependências `pip`; o MO2 já o carrega pelo `plugin_python` da 4d.1.

## 4. Dependências

Nenhum pacote novo (o `dotnet-runtime-8.0`, instalado no box para o spike, não é usado; pode ser removido com `sudo dnf remove dotnet-runtime-8.0`).

## 5. Testes

- **`mobase.steamApp`:** teste Python (pytest do `plugin_python`) com uma Steam falsa num `HOME` temporário (`libraryfolders.vdf`, `appmanifest_<id>.acf`, `compatdata/<id>/pfx`): devolve o `dict` certo; `None` para um app ausente; `compatData` vazio sem `pfx`.
- **`basic_games` com Steam falsa:** teste Python (pytest, com `mobase` disponível) que carrega o `basic_games` e, para NieR (524220), Cyberpunk (1091500) e BG3 (1086940), confere `isInstalled`/`gameDirectory()` e que `documentsDirectory()`/`savesDirectory()` do Cyberpunk e do BG3 estão no prefixo do seu app ID; e o fallback XDG sem prefixo.
- **BG3:** teste de que, no Linux, a geração da ordem de carga não chama o LSLib (nem download nem `subprocess`) e devolve o aviso.
- **Smoke:** o `basic_games` carregado (a verificação da 4d.1 já cobre pacotes Python em `bin/plugins`).
- **Manual (usuário):** NieR, Cyberpunk e BG3 — instância nova de cada pelo MO2, detecção, instalar um mod, executar pelo VFS; no BG3, o aviso da ordem de carga.

## 6. Riscos

| Risco | Mitigação |
|---|---|
| Usuário do BG3 espera a ordem de carga | Aviso claro no MO2 e no README; pendência registrada (§1.3). |
| Extensões C do `pip` (`psutil`, `lzokay`) sem wheel para 3.14 | O `pip` compila (gcc e `python3-devel` no box e no CI); falha de build clara. |
| Um jogo do `basic_games` usa outra pasta especial do Windows além de `%USERPROFILE%`/`%DOCUMENTS%` | A revisão do §3.2 lista as variáveis usadas pelos ~80 jogos; as que não forem mapeadas ficam registradas como limitação. |
| Plugins do upstream com código só Windows não previsto | Revisão por grep (como na 4d.1) só nos três jogos validados e no núcleo; os demais jogos ficam "detectados, não validados". |

## 7. Critérios de pronto

- `mobase.steamApp` e o `basic_games` com Steam falsa passam nos testes.
- No Bazzite, NieR, Cyberpunk e BG3 são detectados, instalam e executam mods pelo VFS; o BG3 avisa que a ordem de carga não é gerada no Linux.
- Smoke, `./build.sh --tests` e CIs verdes.
- README (`basic_games`, limitação do BG3), checklist manual, ROADMAP e STATE atualizados.
