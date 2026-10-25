# Fase 6a — Suporte ao Stellar Blade — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** O MO2 do Linux gerencia mods do Stellar Blade (Steam, app ID `3489700`): detecta o jogo, instala cada tipo de mod no lugar certo e o UE4SS carrega todos os mods Lua ativos.

**Architecture:** Um jogo novo do `basic_games` (`deps/basic_games/games/game_stellarblade.py`) com a pasta de dados na raiz do jogo; um `ModDataChecker` próprio que completa caminhos já estruturados e põe arquivos soltos no destino certo; o `mods.txt`/`mods.json` do UE4SS gerados no perfil antes de cada execução e mapeados para `SB/Binaries/Win64/ue4ss/Mods/`. Nada no C++ nem no `mo2-vfs`.

**Tech Stack:** Python 3.14, `mobase` (plugin_python), PyQt6, pytest; bash (`build.sh`).

**Spec:** `docs/superpowers/specs/2026-10-25-phase6a-stellar-blade-design.md` (ler inteira antes de cada tarefa; §N são seções dela).

## Global Constraints

- Worktree `/var/home/fmoura-server/Documentos/Applications/ModOrganizerLinux-6a`, branch `fase-6a-stellar-blade` (a partir de `origin/main`). **Nunca** compilar, instalar ou rodar testes no checkout principal (`../ModOrganizerLinux`) nem em outros worktrees: o MO2 do usuário roda de lá.
- Valores do jogo (spec §2): app ID `"3489700"`; `GameBinary = "SB/Binaries/Win64/SB-Win64-Shipping.exe"`; `GameLauncher = "SB.exe"`; `GameDataPath = "%GAME_PATH%"`; `GameNexusName = "stellarblade"`; `GameShortName = "stellarblade"`; `GameName = "Stellar Blade"`.
- Destinos canônicos (spec §3, §3.2): `SB/Content/Paks/~mods`, `SB/Content/Paks/LogicMods`, `SB/Binaries/Win64`, `SB/Binaries/Win64/ue4ss/Mods`. Comparação de nomes sem diferenciar maiúsculas (o VFS também não diferencia); caminhos criados pelo `fix` usam a grafia canônica.
- Anti-meta (spec §1.3): o `fix` nunca reclassifica conteúdo que já veio estruturado (ex.: `.pak` dentro de `LogicMods` continua em `LogicMods`).
- Saves (spec §4): `GameSavesDirectory = "%USERPROFILE%/AppData/Local/SB/Saved/SaveGames"`, `GameSaveExtension = "sav"`, **sem** saves locais por perfil. Configs: `GameDocumentsDirectory = "%USERPROFILE%/AppData/Local/SB/Saved/Config/WindowsNoEditor"`, `GameIniFiles = ["Engine.ini", "Game.ini", "Input.ini", "GameUserSettings.ini"]`.
- Arquivo novo no subtree, não específico do Linux: sem `if sys.platform`. Estilo dos outros jogos do `basic_games` (ruff/black do upstream, anotações de tipo).
- Commits: arquivos de `deps/basic_games` em `deps/basic_games: …` (só arquivos daquele subtree); `tests: …`, `docs: …`. Todo commit termina com:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  ```
- Mensagens de log em inglês; textos para o usuário no README em português (como o resto do README). Nada de push sem autorização do usuário.

## Ambiente de execução

No Bazzite, na distrobox `mo2-run`, da raiz do worktree:

```bash
cd /var/home/fmoura-server/Documentos/Applications/ModOrganizerLinux-6a
mkdir -p /tmp/mo2t
distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --tests
```

O worktree não tem `build-out/`: o primeiro build é completo e longo (o controlador o roda antes da Task 1). Depois de cada mudança: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only basic_games --tests` (instala o pacote em `install/` e roda `tests/basic-games` com pytest). Nunca `pkill -f`/`pgrep -f` com padrão da própria linha de comando.

## Review Focus

1. Arquivo com uma pasta de embrulho (`MeuMod-v1.2/SB/Content/...` ou `MeuMod/~mods/x.pak`): esperado o mesmo resultado que sem o embrulho — Task 2, `test_wrapper_folder_is_unwrapped`.
2. Mod com `.pak` solto **e** pasta `ue4ss/` na raiz (misto): cada parte vai para o seu destino, nada se perde — Task 2, `test_mixed_loose_pak_and_ue4ss`.
3. Grafia diferente (`~Mods`, `logicmods`, `sb/content/paks`): reconhecida como já estruturada, sem duplicar pastas — Task 2, `test_casing_variants_are_recognized`.
4. Dois mods ativos com a mesma pasta de mod do UE4SS (ex.: uma atualização instalada ao lado): aparece uma vez no `mods.txt` — Task 3, `test_duplicate_ue4ss_mod_dirs_listed_once`.
5. Perfil sem nenhum UE4SS ativo: gerar a lista não falha nem cria nada fora do perfil — Task 3, `test_no_ue4ss_mods_writes_defaults_only`.

---

### Task 1: Jogo `Stellar Blade` no `basic_games` (detecção, executáveis, pastas)

**Files:**
- Create: `deps/basic_games/games/game_stellarblade.py`
- Modify: `tests/basic-games/conftest.py` (entrada `"3489700": ("StellarBlade", "SB/Binaries/Win64/SB-Win64-Shipping.exe", True)` em `GAMES`; também criar `SB.exe` na raiz do jogo falso, se o fixture só cria o binário)
- Create: `tests/basic-games/test_stellarblade.py`

**Interfaces:**
- Produces: classe `StellarBladeGame(BasicGame)` (nome usado pelos testes via `_load_basic_games()["StellarBladeGame"]`); métodos `paksDirectory() -> QDir`, `ue4ssModsDirectory() -> QDir` (absolutos, dentro de `gameDirectory()`), usados pelas Tasks 2 e 3.

- [ ] **Step 1: Write the failing tests** em `tests/basic-games/test_stellarblade.py` (reusar `_load_basic_games` de `test_detection` e o fixture `steam`):

```python
def test_stellar_blade_is_detected(steam):
    game = _load_basic_games()["StellarBladeGame"]
    game.detectGame()
    assert game.isInstalled()
    assert Path(game.gameDirectory().absolutePath()) == steam / "steamapps/common/StellarBlade"
    assert game.steamAPPId() == "3489700"
    assert game.gameNexusName() == "stellarblade"
    assert game.binaryName() == "SB/Binaries/Win64/SB-Win64-Shipping.exe"
    assert Path(game.dataDirectory().absolutePath()) == steam / "steamapps/common/StellarBlade"

def test_stellar_blade_folders_are_in_the_prefix(steam):
    game = _load_basic_games()["StellarBladeGame"]
    game.detectGame()
    user = steam / "steamapps/compatdata/3489700/pfx/drive_c/users/steamuser"
    assert Path(game.savesDirectory().absolutePath()) == user / "AppData/Local/SB/Saved/SaveGames"
    assert Path(game.documentsDirectory().absolutePath()) == user / "AppData/Local/SB/Saved/Config/WindowsNoEditor"
    assert game.iniFiles() == ["Engine.ini", "Game.ini", "Input.ini", "GameUserSettings.ini"]
    assert game.savegameExtension() == "sav"

def test_stellar_blade_ue4ss_dirs(steam):
    game = _load_basic_games()["StellarBladeGame"]
    game.detectGame()
    root = steam / "steamapps/common/StellarBlade"
    assert Path(game.paksDirectory().absolutePath()) == root / "SB/Content/Paks"
    assert Path(game.ue4ssModsDirectory().absolutePath()) == root / "SB/Binaries/Win64/ue4ss/Mods"
```

Também atualizar `test_the_three_games_are_detected`? **Não**: o teste novo cobre o jogo; o antigo continua igual.

- [ ] **Step 2: Run to verify they fail**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only basic_games --tests`
Expected: FAIL com `KeyError: 'StellarBladeGame'`.

- [ ] **Step 3: Implement `StellarBladeGame`** em `deps/basic_games/games/game_stellarblade.py` com os atributos da seção Global Constraints, mais `Name = "Stellar Blade Support Plugin"`, `Author = "FM0Ura"`, `Version = "1.0.0"`, `GameSupportURL` vazio ou omitido. `executables()` devolve o `SB-Win64-Shipping.exe` (nome "Stellar Blade") — usar o padrão do `BasicGame` se ele já fizer isso com `GameBinary`. Sem saves locais por perfil (não registrar `BasicLocalSavegames`).

- [ ] **Step 4: Run to verify they pass** — mesmo comando. Expected: PASS (todos os testes de `tests/basic-games`).

- [ ] **Step 5: Commit**

```bash
git add deps/basic_games/games/game_stellarblade.py
git commit -m "deps/basic_games: add Stellar Blade"
git add tests/basic-games/conftest.py tests/basic-games/test_stellarblade.py
git commit -m "tests: basic-games: Stellar Blade detection and folders"
```

---

### Task 2: Verificador de dados (`StellarBladeModDataChecker`)

**Files:**
- Modify: `deps/basic_games/games/game_stellarblade.py` (classe nova + registro em `init` com `self._register_feature(StellarBladeModDataChecker())`)
- Create: `tests/basic-games/test_stellarblade_checker.py`

**Interfaces:**
- Consumes: Task 1.
- Produces: `StellarBladeModDataChecker(mobase.ModDataChecker)` com `dataLooksValid(tree) -> CheckReturn` e `fix(tree) -> IFileTree`.

Regras (spec §3.1; precedência nesta ordem):

| Entrada (na raiz do arquivo, depois de tirar pastas de embrulho únicas sem nada reconhecível) | Status | Resultado do `fix` |
|---|---|---|
| Só `SB/` | `VALID` | — |
| `Content/`, `Paks/`, `~mods/`, `LogicMods/`, `Binaries/`, `Win64/`, `ue4ss/`, `dwmapi.dll`, `UE4SS.dll`(*) | `FIXABLE` | completa o prefixo que falta até `SB/…` (ex.: `ue4ss/` → `SB/Binaries/Win64/ue4ss/`; `~Mods/` → `SB/Content/Paks/~mods/`; `LogicMods/` → `SB/Content/Paks/LogicMods/`; `dwmapi.dll` → `SB/Binaries/Win64/dwmapi.dll`) |
| `Mods/<m>/` com `Scripts/main.lua`, `dlls/main.dll` ou `enabled.txt` | `FIXABLE` | `SB/Binaries/Win64/ue4ss/Mods/<m>/` |
| `<m>/` com `Scripts/main.lua`, `dlls/main.dll` ou `enabled.txt` | `FIXABLE` | `SB/Binaries/Win64/ue4ss/Mods/<m>/` |
| `.pak`/`.ucas`/`.utoc` soltos | `FIXABLE` | `SB/Content/Paks/~mods/<arquivo>` |
| Lixo na raiz (`*.txt` exceto `enabled.txt`, `*.md`, `*.png`, `*.jpg`, `*.jpeg`, `*.webp`, `manifest.json`) | — | apagado (só na raiz e em pastas de embrulho; nunca dentro de pastas de mod do UE4SS nem de `SB/`) |
| Qualquer `.exe` na raiz, ou nada reconhecível | `INVALID` | — |

(*) `UE4SS.dll` solto na raiz vai para `SB/Binaries/Win64/ue4ss/UE4SS.dll`.

Pasta de embrulho: uma única pasta na raiz (ignorando o lixo) cujo conteúdo casa com alguma linha acima; o `fix` sobe o conteúdo dela. Comparação de nomes com `casefold()`; o destino criado usa a grafia canônica, mas uma pasta já estruturada (`SB/…`) é mantida como veio.

Árvores de teste com `mobase.private.makeTree()` e o helper `make_tree(values)` de `deps/plugin_python/tests/python/test_filetree.py` (copiar o helper para o arquivo de teste). Para conferir o resultado, comparar o conjunto `{e.path("/") for e in tree.walk() if e.isFile()}`.

- [ ] **Step 1: Write the failing tests** em `tests/basic-games/test_stellarblade_checker.py`, um por linha da tabela e da Review Focus 1–3, com os layouts reais da spec §3.2:

```python
V, F, I = mobase.ModDataChecker.VALID, mobase.ModDataChecker.FIXABLE, mobase.ModDataChecker.INVALID

def test_cns_full_layout_is_valid():  # mod 1496
    t = make_tree([("SB", [("Binaries", [("Win64", [("ue4ss", [("Mods", [("DekCNS", ["enabled.txt", ("Scripts", ["main.lua"])])]), "UE4SS-settings.ini"])])]),
                           ("Content", [("Paks", [("LogicMods", ["DekCNS_P.pak", "DekCNS_P.ucas", "DekCNS_P.utoc"]), ("~mods", [("CustomNanosuitSystem", [("Cosmetics", ["a.dekcns.json"])])])])])])])
    assert checker.dataLooksValid(t) == V

def test_ue4ss_itself_layout_is_valid():  # mod 2952
    # SB/Binaries/Win64/dwmapi.dll + ue4ss/Mods/BPModLoaderMod/Scripts/main.lua + ue4ss/Mods/mods.txt → VALID

def test_jiggle_ue4ss_root_is_prefixed():  # mod 1570
    # ue4ss/Mods/JiggleUpdate/Scripts/main.lua, ue4ss/Mods/mods.txt, ue4ss/UE4SS-settings.ini → FIXABLE;
    # depois do fix: {"SB/Binaries/Win64/ue4ss/Mods/JiggleUpdate/Scripts/main.lua", "SB/Binaries/Win64/ue4ss/Mods/mods.txt", "SB/Binaries/Win64/ue4ss/UE4SS-settings.ini"}

def test_catwalk_tilde_mods_root_is_prefixed():  # mod 2717, "~Mods" com M maiúsculo
    # ~Mods/x_P.pak, ~Mods/x_P.ucas, ~Mods/x_P.utoc, ~Mods/CustomNanosuitSystem/Animations/a.dekani.json → FIXABLE;
    # depois: tudo sob "SB/Content/Paks/~mods/…", mesma subestrutura

def test_logicmods_root_stays_logicmods():
    # LogicMods/a.pak → depois: "SB/Content/Paks/LogicMods/a.pak" (nunca ~mods)

def test_loose_paks_go_to_tilde_mods():
    # a.pak, a.ucas, a.utoc, readme.txt, cover.png → FIXABLE; depois: {"SB/Content/Paks/~mods/a.pak", "…/a.ucas", "…/a.utoc"}

def test_bare_lua_mod_folder_goes_to_ue4ss_mods():
    # MyMod/Scripts/main.lua, MyMod/enabled.txt → "SB/Binaries/Win64/ue4ss/Mods/MyMod/Scripts/main.lua", "…/MyMod/enabled.txt"

def test_mods_folder_goes_to_ue4ss_mods():
    # Mods/MyMod/Scripts/main.lua → "SB/Binaries/Win64/ue4ss/Mods/MyMod/Scripts/main.lua"

def test_loose_proxy_dll_goes_to_win64():
    # dwmapi.dll, ue4ss/UE4SS.dll → "SB/Binaries/Win64/dwmapi.dll", "SB/Binaries/Win64/ue4ss/UE4SS.dll"

def test_wrapper_folder_is_unwrapped():  # Review Focus 1
    # MeuMod-v1.2/SB/Content/Paks/~mods/a.pak → FIXABLE; depois "SB/Content/Paks/~mods/a.pak"
    # MeuMod/~mods/a.pak → depois "SB/Content/Paks/~mods/a.pak"

def test_mixed_loose_pak_and_ue4ss():  # Review Focus 2
    # a.pak + ue4ss/Mods/M/Scripts/main.lua → "SB/Content/Paks/~mods/a.pak", "SB/Binaries/Win64/ue4ss/Mods/M/Scripts/main.lua"

def test_casing_variants_are_recognized():  # Review Focus 3
    # sb/content/paks/logicmods/a.pak → VALID ou FIXABLE, e depois do fix (se FIXABLE) continua em LogicMods, sem pasta duplicada

def test_exe_is_invalid():  # mod 861
    # StellarBlade_Chunk_Id_exe.exe → INVALID

def test_nothing_recognizable_is_invalid():
    # readme.txt, Textures/a.dds → INVALID
```

O implementador escreve os corpos marcados com comentário seguindo o primeiro teste; os conjuntos esperados são os do comentário.

- [ ] **Step 2: Run to verify they fail** — `./build.sh --only basic_games --tests`. Expected: FAIL (o jogo não registra verificador; `dataLooksValid` do padrão).

- [ ] **Step 3: Implement `StellarBladeModDataChecker`** em `game_stellarblade.py` seguindo a tabela. Ponto de partida para a detecção de mods do UE4SS: `games/oblivion_remaster/mod_data_checker.py`.

- [ ] **Step 4: Run to verify they pass** — mesmo comando. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add deps/basic_games/games/game_stellarblade.py
git commit -m "deps/basic_games: Stellar Blade: install each kind of mod in its folder"
git add tests/basic-games/test_stellarblade_checker.py
git commit -m "tests: basic-games: Stellar Blade mod data checker"
```

---

### Task 3: `mods.txt`/`mods.json` do UE4SS gerados no perfil

**Files:**
- Modify: `deps/basic_games/games/game_stellarblade.py`
- Create: `tests/basic-games/test_stellarblade_ue4ss.py`

**Interfaces:**
- Consumes: `ue4ssModsDirectory()` (Task 1).
- Produces: `write_ue4ss_mod_lists(profile_dir: Path, mod_dirs: list[Path]) -> None` (função de módulo, pura e testável: `mod_dirs` são as pastas raiz dos mods ativos em ordem de prioridade crescente, mais o `overwrite` por último); `StellarBladeGame.mappings() -> list[mobase.Mapping]`; `_onAboutToRun(app: str, wd: QDir, args: str) -> bool` registrado com `organizer.onAboutToRun` em `init` (como o Cyberpunk), que chama `write_ue4ss_mod_lists` com os mods ativos do perfil (`organizer.modList().allModsByProfilePriority()`, filtrando por `state() & mobase.ModState.ACTIVE`, caminho por `organizer.modList().getMod(name).absolutePath()`) e devolve `True`. Também gerar em `initializeProfile`.

Conteúdo (spec §3.2). Padrões do UE4SS 3.x, nesta ordem, com estes valores:

```python
DEFAULT_UE4SS_MODS = [
    ("CheatManagerEnablerMod", True), ("ActorDumperMod", False), ("ConsoleCommandsMod", True),
    ("ConsoleEnablerMod", True), ("SplitScreenMod", False), ("LineTraceMod", True),
    ("BPModLoaderMod", True), ("BPML_GenericFunctions", True), ("jsbLuaProfilerMod", False),
]
```

Depois deles, cada outra pasta de `<mod>/SB/Binaries/Win64/ue4ss/Mods/*` dos `mod_dirs` (só diretórios; sem `shared`, sem os padrões, sem repetir; comparação sem diferenciar maiúsculas, mantendo a grafia da primeira ocorrência), habilitada, na ordem de prioridade; e por último `Keybinds` habilitado (o UE4SS espera o `Keybinds` no fim). `mods.txt`: uma linha `<nome> : 1|0` por mod. `mods.json`: lista de `{"mod_name": <nome>, "mod_enabled": <bool>}`, `indent=4`. `mappings()`: `<profilePath>/mods.txt` e `<profilePath>/mods.json` → `ue4ssModsDirectory()/mods.txt` e `/mods.json`, `isDirectory=False` (como o Oblivion Remaster).

- [ ] **Step 1: Write the failing tests** em `tests/basic-games/test_stellarblade_ue4ss.py` (com `tmp_path`, mods falsos como pastas):

```python
def test_mod_lists_have_defaults_extras_and_keybinds_last(tmp_path):
    a = tmp_path / "mods/CNS";    (a / "SB/Binaries/Win64/ue4ss/Mods/DekCNS/Scripts").mkdir(parents=True)
    b = tmp_path / "mods/Jiggle"; (b / "SB/Binaries/Win64/ue4ss/Mods/JiggleUpdate/Scripts").mkdir(parents=True)
    (b / "SB/Binaries/Win64/ue4ss/Mods/shared").mkdir()
    write_ue4ss_mod_lists(tmp_path / "profile", [a, b])
    lines = (tmp_path / "profile/mods.txt").read_text().splitlines()
    assert lines[:9] == ["CheatManagerEnablerMod : 1", "ActorDumperMod : 0", "ConsoleCommandsMod : 1",
                         "ConsoleEnablerMod : 1", "SplitScreenMod : 0", "LineTraceMod : 1",
                         "BPModLoaderMod : 1", "BPML_GenericFunctions : 1", "jsbLuaProfilerMod : 0"]
    assert lines[9:] == ["DekCNS : 1", "JiggleUpdate : 1", "Keybinds : 1"]
    data = json.loads((tmp_path / "profile/mods.json").read_text())
    assert data[-1] == {"mod_name": "Keybinds", "mod_enabled": True}
    assert {"mod_name": "ActorDumperMod", "mod_enabled": False} in data

def test_duplicate_ue4ss_mod_dirs_listed_once(tmp_path):  # Review Focus 4
    # dois mods com SB/Binaries/Win64/ue4ss/Mods/JiggleUpdate e um com …/jiggleupdate → "JiggleUpdate : 1" aparece uma vez

def test_default_mod_dirs_are_not_repeated(tmp_path):
    # o UE4SS em si traz …/Mods/BPModLoaderMod e …/Mods/Keybinds → cada um só uma vez, nas posições dos padrões

def test_no_ue4ss_mods_writes_defaults_only(tmp_path):  # Review Focus 5
    # mod_dirs = [pasta sem SB/] e uma pasta inexistente → mods.txt = 9 padrões + "Keybinds : 1"; nada criado fora de tmp_path/"profile"

def test_mappings_point_profile_files_to_ue4ss_mods(steam, tmp_path):
    # jogo detectado, _organizer = MagicMock com profilePath() = str(tmp_path/"profile");
    # mappings() == 2 mapeamentos: source <profile>/mods.txt → destination <jogo>/SB/Binaries/Win64/ue4ss/Mods/mods.txt (e mods.json), isDirectory False
```

- [ ] **Step 2: Run to verify they fail** — `./build.sh --only basic_games --tests`. Expected: FAIL `ImportError: cannot import name 'write_ue4ss_mod_lists'`.

- [ ] **Step 3: Implement** `DEFAULT_UE4SS_MODS`, `write_ue4ss_mod_lists`, `mappings`, `_onAboutToRun` e a chamada em `initializeProfile` em `game_stellarblade.py`. `write_ue4ss_mod_lists` cria `profile_dir` se faltar e só escreve dentro dele. `_onAboutToRun` só age quando o jogo gerenciado é o Stellar Blade (`self._organizer.managedGame() is self`, como os outros jogos que usam o hook); erros de E/S vão para o log (`qWarning`, em inglês) e o hook devolve `True` (não impede o jogo de abrir).

- [ ] **Step 4: Run to verify they pass** — mesmo comando. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add deps/basic_games/games/game_stellarblade.py
git commit -m "deps/basic_games: Stellar Blade: generate the UE4SS mod lists in the profile"
git add tests/basic-games/test_stellarblade_ue4ss.py
git commit -m "tests: basic-games: Stellar Blade UE4SS mod lists"
```

---

### Task 4: Documentação, checklist manual e build completo

**Files:**
- Modify: `README.md` ("Working games": linha do Stellar Blade, "checado manualmente" só depois da Task 5 — até lá "implemented, manual test pending"; seção "Plugins Python": o Stellar Blade, a pasta de dados na raiz, os quatro destinos, o caso do `LogicMods` sem pasta (spec §3.1 regra 2), o `mods.txt` gerado (os `mods.txt` dos mods não valem), e `WINEDLLOVERRIDES=dwmapi=n,b` para o UE4SS, no mesmo formato das outras DLLs proxy)
- Modify: `deps/README.md` (nota do `basic_games`: arquivo nosso `games/game_stellarblade.py`, não vem do upstream)
- Create: `docs/testing/phase6a-manual-checklist.md`
- Modify: `ROADMAP.md` (seção "Fase 6 — Mods do Stellar Blade e Nexus Collections" com 6a/6b e links das specs) e `STATE.md` (fase atual 6a, tabela de tarefas com os commits)

**Interfaces:** nenhuma.

- [ ] **Step 1: Escrever o checklist** `docs/testing/phase6a-manual-checklist.md`, no formato dos checklists anteriores, com os passos da spec §6 e:
  0. Pré-condição: `SB/Content/Paks` só com os paks do jogo e `SB/Binaries/Win64` sem `dwmapi.dll`/`ue4ss` (conferir com `ls`); o Stellar Blade rodou uma vez pela Steam (prefixo existe).
  1. Criar a instância do Stellar Blade no MO2 (`install/` deste worktree, pela distrobox, como nos checklists anteriores).
  2. Instalar, pelo **Download with mod manager** do site: Stellar Blade UE4SS (mod 2952), CNS (mod 1496), Better Jiggle (mod 1570), Catwalk Animations (mod 2717) e um mod `.pak` solto visível (cor de cabelo). Em cada um: nenhuma janela de instalação manual; conferir na aba **Filetree** do mod que está sob `SB/…` no lugar da spec §3.
  3. **Modify Executables → Proton → Environment:** `WINEDLLOVERRIDES=dwmapi=n,b`.
  4. Rodar pelo MO2: o console/log do UE4SS (`SB/Binaries/Win64/ue4ss/UE4SS.log` no `overwrite` ou na pasta real) lista DekCNS e JiggleUpdate carregados; o mod `.pak` aparece no jogo; o menu do CNS abre.
  5. `<perfil>/mods.txt` tem os padrões, `DekCNS : 1`, `JiggleUpdate : 1` e `Keybinds : 1` no fim.
  6. Desativar o mod `.pak` e rodar de novo: o efeito some. Desativar o Jiggle: some do `mods.txt`.
  7. Um `.ini` (ex.: `Engine.ini`) editado em **Tools → INI Editor** (perfil com configs locais) chega a `AppData/Local/SB/Saved/Config/WindowsNoEditor/Engine.ini` durante a execução; se não chegar, anotar (spec §4: configs ficam de fora e viram limitação).
  8. A aba **Saves** lista os `.sav`.

- [ ] **Step 2: README, `deps/README.md`, ROADMAP e STATE** com o conteúdo da lista de arquivos acima.

- [ ] **Step 3: Build e testes completos**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --tests`
Expected: termina sem `die`; `tests/basic-games` verde.

- [ ] **Step 4: Commit**

```bash
git add README.md deps/README.md ROADMAP.md STATE.md docs/testing/phase6a-manual-checklist.md
git commit -m "docs: Phase 6a: Stellar Blade (README, checklist, roadmap, state)"
```

---

### Task 5: Teste manual no Bazzite (com o usuário)

Não é do implementador: o controlador entrega o checklist ao usuário, que o executa com o `install/` deste worktree. Resultados e correções viram tarefas novas; o push, o PR e o CI dependem da autorização do usuário.
