# Fase 4d.2 — `basic_games` no Linux — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Os jogos do `basic_games` são detectados pela Steam e usam as pastas do prefixo do Proton de cada jogo no Linux; NieR:Automata, Cyberpunk 2077 e Baldur's Gate 3 funcionam; a ordem de carga do BG3 (LSLib) fica desativada no Linux com aviso (spec §1.3).

**Architecture:** Um binding novo do `mobase` (`mobase.utils.steamApp`, só Linux) expõe o `steam::findApp` do `uibase`. O `basic_games` (subtree) usa-o no Linux: `find_games()` vira um mapeamento preguiçoso por app ID e a expansão de `%USERPROFILE%`/`%DOCUMENTS%` aponta para o prefixo do jogo. No Linux, o BG3 não chama o LSLib (defeito do LSLib no Unix, spec §1.3) e avisa o usuário. Testes pytest com uma Steam falsa.

**Tech Stack:** Python 3.14 (Fedora), PyQt6, pybind11 (`plugin_python`), C++20/Qt 6 (`uibase`), pytest, .NET 8 (Fedora `dotnet-runtime-8.0`), bash.

**Spec:** `docs/superpowers/specs/2026-10-21-phase4d2-basic-games-design.md` (ler inteira antes de cada tarefa; §N são seções dela).

## Global Constraints

- Worktree `../ModOrganizerLinux-4d2` (`/var/home/fmoura-server/Documentos/Applications/ModOrganizerLinux-4d2`), branch `fase-4d2-basic-games` (a partir de `fase-4d1-python`; a spec já está nele). **Nunca** compilar, instalar ou rodar testes no checkout principal (`../ModOrganizerLinux`) nem nos outros worktrees: o MO2 do usuário roda do `install/` do checkout principal.
- Fonte da verdade da Steam: `MOBase::steam::findApp()` (spec §1.2). Nada de reimplementar a descoberta da Steam em Python.
- No Linux: `%USERPROFILE%` = `<compatData>/pfx/drive_c/users/steamuser`, `%DOCUMENTS%` = `<compatData>/pfx/drive_c/users/steamuser/Documents` do prefixo **do app ID do jogo**; sem app ID, sem jogo instalado ou com `compatData` vazio: o comportamento atual (XDG via `QStandardPaths`) e um aviso no log, uma vez por jogo (spec §3.2).
- GOG, Epic, Origin, EA Desktop: no Linux, `find_games()` devolve `{}` sem erro e registra uma vez que só a Steam é suportada (spec §3.1).
- BG3 no Linux: a geração do `modsettings.lsx` não baixa nem executa o LSLib; o usuário recebe um aviso de que a ordem de carga do BG3 ainda não é gerada no Linux (spec §1.3, §3.3). Decisão do usuário depois do spike da Task 1.
- Importação: `git subtree add --prefix deps/basic_games https://github.com/ModOrganizer2/modorganizer-basic_games master --squash`; hash em `deps/README.md`; os patches Linux listados nas notas de `deps/README.md` (padrão da 4d.1).
- Código Windows idêntico ou em `#ifdef _WIN32`/`#ifndef _WIN32` (C++) e `if sys.platform == "win32":` (Python). O CI do Windows só compila `deps/modorganizer`.
- Commits: patches dentro de `deps/<nome>` em `deps/<nome>: Linux: …` (só arquivos daquele subtree); `build: …`, `tests: …`, `docs: …`. Todo commit termina com:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL
  ```
- Mensagens de log em inglês; comentários no estilo do arquivo. `*_en.ts` regenerados não são commitados. Nada de push sem autorização do usuário.

## Ambiente de execução

No Bazzite, na distrobox `mo2-run`, da raiz do worktree:

```bash
cd /var/home/fmoura-server/Documentos/Applications/ModOrganizerLinux-4d2
mkdir -p /tmp/mo2t
distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --tests
```

O worktree não tem `build-out/`: o primeiro build é completo e longo (o controlador o roda antes da Task 1). Depois: `./build.sh --only <alvo> --tests`. O box tem o Python 3.14, PyQt6, pybind11, sip6, gmock-devel e pytest (4d.1). **O `dotnet-runtime-8.0` é instalado pelo controlador na Task 1, com o OK do usuário.** Nunca `pkill -f`/`pgrep -f` com padrão da própria linha de comando. Arquivos descartáveis em `/tmp/mo2t/…`, apagados no fim.

Jogos reais do usuário (Steam nativa, `/var/home/fmoura-server/.local/share/Steam`): Baldur's Gate 3 (1086940), Cyberpunk 2077 (1091500), NieR:Automata (524220). **Os testes automáticos nunca usam a Steam real** (sempre um `HOME` temporário com uma Steam falsa); só a Task 1 lê um `.pak` do BG3 real, sem escrever na pasta do jogo.

## Review Focus

- Jogo instalado pela Steam mas nunca rodado (sem `compatdata/<id>/pfx`): documentos e saves nas pastas XDG, com o aviso no log, sem exceção (Task 4, teste).
- Jogo com app ID em outra biblioteca da Steam (outro disco) ou Steam Flatpak: detectado (o `steam::findApp` já cobre; o teste da Task 2 usa uma segunda biblioteca).
- Usuário do BG3 pede a ordem de carga no Linux: aviso claro, nada baixado do GitHub, nenhum processo iniciado, MO2 segue funcionando (Task 5, teste).
- Jogo do `basic_games` com `GameSteamId` em lista (vários IDs): o primeiro instalado vence (Task 3, teste).

---

### Task 1: Spike — o `Divine.dll` lê `.pak` do BG3 com o .NET do Fedora

**Files:** nenhum no repositório (resultado registrado no relatório e no ledger).

Pré-requisito (controlador, com o OK do usuário): `distrobox enter mo2-run -- sudo dnf install -y dotnet-runtime-8.0`.

- [ ] **Step 1:** Baixar o LSLib num diretório descartável: `gh release download v1.20.4 -R Norbyte/lslib -p ExportTool-v1.20.4.zip -D /tmp/mo2t/lslib && cd /tmp/mo2t/lslib && unzip -q ExportTool-v1.20.4.zip`. Confirmar `Packed/Tools/Divine.dll` e `Divine.runtimeconfig.json` (`"name": "Microsoft.NETCore.App"`, `"version": "8.0.0"`).
- [ ] **Step 2:** Achar um `.pak` pequeno do BG3 real (somente leitura): `find "/var/home/fmoura-server/.local/share/Steam/steamapps/common/Baldurs Gate 3/Data" -name '*.pak' -size -50M | head -3`.
- [ ] **Step 3:** Rodar os dois comandos que o `pak_parser.py` usa (ler `deps/basic_games/games/baldursgate3/pak_parser.py` no upstream para os `action` exatos: há um que lista/extrai `meta.lsx`), com o destino em `/tmp/mo2t/lslib/out`:
  `distrobox enter mo2-run -- dotnet /tmp/mo2t/lslib/Packed/Tools/Divine.dll -g bg3 -l info -a list-package -s "<pak>"` e o de extração equivalente ao do plugin.
- [ ] **Step 4:** Resultado: **funciona** (código 0 e saída/arquivos esperados) ou **não funciona** (erro, com a mensagem). Registrar no relatório os comandos, saídas e versão do `dotnet --list-runtimes`. Apagar `/tmp/mo2t/lslib`.

Se não funcionar, o controlador registra uma Ruling (Divine.exe por Proton no prefixo do BG3) e ajusta a Task 5 antes de despachá-la.

---

### Task 2: `mobase.utils.steamApp` (Linux)

**Files:**
- Modify: `deps/plugin_python/src/mobase/wrappers/utils.cpp` (função nova em `#ifndef _WIN32`)
- Modify: `deps/plugin_python/src/mobase/CMakeLists.txt` só se for preciso ligar algo novo (o `uibase` já está ligado)
- Test: `deps/plugin_python/tests/python/test_steam.py` (novo; o `pytest` do `plugin_python` já coleta `test_*.py`)

**Interfaces:**
- Produces: `mobase.utils.steamApp(app_id: str) -> dict[str, str] | None`, chaves `appId`, `root`, `library`, `gamePath`, `compatData` (vazio sem prefixo). Usado pelas Tasks 3 e 4.

- [ ] **Step 1: Teste que falha**

`deps/plugin_python/tests/python/test_steam.py`:

```python
import os
import sys
from pathlib import Path

import pytest

import mobase

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux only")


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


@pytest.fixture
def steam(tmp_path, monkeypatch):
    """A HOME with the native Steam and a second library, with three games:
    524220 (with a Proton prefix), 1091500 (in the second library, with a prefix)
    and 1086940 (no prefix)."""
    home = tmp_path / "home"
    root = home / ".local/share/Steam"
    second = tmp_path / "Games/SteamLibrary"
    _write(
        root / "steamapps/libraryfolders.vdf",
        '"libraryfolders"\n{\n'
        f'\t"0"\n\t{{\n\t\t"path"\t\t"{root}"\n\t}}\n'
        f'\t"1"\n\t{{\n\t\t"path"\t\t"{second}"\n\t}}\n'
        "}\n",
    )
    for lib, appid, installdir, prefix in [
        (root, "524220", "NieRAutomata", True),
        (second, "1091500", "Cyberpunk 2077", True),
        (root, "1086940", "Baldurs Gate 3", False),
    ]:
        _write(
            lib / f"steamapps/appmanifest_{appid}.acf",
            f'"AppState"\n{{\n\t"appid"\t\t"{appid}"\n'
            f'\t"installdir"\t\t"{installdir}"\n}}\n',
        )
        (lib / "steamapps/common" / installdir).mkdir(parents=True)
        compat = lib / "steamapps/compatdata" / appid
        compat.mkdir(parents=True)
        if prefix:
            (compat / "pfx/drive_c/users/steamuser").mkdir(parents=True)
    monkeypatch.setenv("HOME", str(home))
    monkeypatch.setenv("XDG_DATA_HOME", str(home / ".local/share"))
    return root, second


def test_installed_game_with_prefix(steam):
    root, _ = steam
    app = mobase.utils.steamApp("524220")
    assert app is not None
    assert app["appId"] == "524220"
    assert Path(app["gamePath"]) == root / "steamapps/common/NieRAutomata"
    assert Path(app["compatData"]) == root / "steamapps/compatdata/524220"


def test_game_in_another_library(steam):
    _, second = steam
    app = mobase.utils.steamApp("1091500")
    assert app is not None
    assert Path(app["library"]) == second
    assert Path(app["compatData"]) == second / "steamapps/compatdata/1091500"


def test_game_without_prefix(steam):
    app = mobase.utils.steamApp("1086940")
    assert app is not None
    assert app["compatData"] == ""


def test_missing_game(steam):
    assert mobase.utils.steamApp("999999") is None
```

(Se o `steam::roots()` do `uibase` ler a Steam por outro caminho além de `$HOME/.local/share/Steam` — leia `deps/uibase/src/linux/steam_linux.cpp` —, ajuste só a fixture para usar o caminho que ele lê, nunca a Steam real. Se ele guardar um cache entre chamadas, use app IDs diferentes por teste ou exponha a invalidação; registre no relatório.)

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only plugin_python --tests` — Expected: FAIL `AttributeError: module 'mobase.utils' has no attribute 'steamApp'`.

- [ ] **Step 2: Implementar**

Em `wrappers/utils.cpp`, `#include <uibase/linux/steam.h>` dentro de `#ifndef _WIN32` junto aos includes, e em `add_utils_bindings(m)`:

```cpp
#ifndef _WIN32
        // Linux: the installed Steam game with this app ID, from the files of the
        // Steam client (uibase, Phase 3); None if it is not installed
        m.def(
            "steamApp",
            [](QString appId) -> py::object {
                const auto app = MOBase::steam::findApp(appId);
                if (!app) {
                    return py::none();
                }
                py::dict d;
                d["appId"]      = app->appId;
                d["root"]       = app->root;
                d["library"]    = app->library;
                d["gamePath"]   = app->gamePath;
                d["compatData"] = app->compatData;
                return d;
            },
            py::arg("app_id"));
#endif
```

(Os `QString` viram `str` pelo caster do `pybind11-qt` já usado no arquivo; se o `py::dict` não aceitar `QString` direto, converta com `app->appId.toStdString()`.)

- [ ] **Step 3: Rodar e ver passar** — o mesmo comando; Expected: os 4 testes de `test_steam.py` PASS e o resto do `plugin_python` verde.

- [ ] **Step 4: Commit** — `deps/plugin_python: Linux: mobase.utils.steamApp, the installed Steam game of an app ID`.

---

### Task 3: `basic_games` importado, alvo de build e detecção pela Steam

**Files:**
- Create (subtree): `deps/basic_games/`
- Modify: `deps/basic_games/steam_utils.py`, `gog_utils.py`, `epic_utils.py`, `origin_utils.py`, `eadesktop_utils.py` (ramos Linux)
- Modify: `build.sh` (alvo `basic_games`), `deps/README.md`
- Create: `tests/basic-games/conftest.py`, `tests/basic-games/test_detection.py`

**Interfaces:**
- Consumes: `mobase.utils.steamApp` (Task 2).
- Produces: no Linux, `steam_utils.find_games()` devolve um `Mapping[str, Path]` preguiçoso; alvo `build_basic_games` no `build.sh` que roda `tests/basic-games` com `--tests`; o fixture `steam` de `tests/basic-games/conftest.py` (Steam falsa, mesma forma da Task 2) usado pela Task 4.

- [ ] **Step 1: Importar** — o comando de subtree das Global Constraints; hash em `deps/README.md`.

- [ ] **Step 2: Testes que falham**

`tests/basic-games/conftest.py`:

```python
# Tests of the basic_games plugin on Linux (Phase 4d.2): the installed package is
# imported with the installed mobase, in a HOME with a fake Steam (never the user's).
import sys
from pathlib import Path

import pytest
from PyQt6.QtWidgets import QApplication

_app = None


def pytest_configure():
    global _app
    _app = QApplication(sys.argv)


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


GAMES = {
    # app ID: (install dir, binary relative to it, with a Proton prefix)
    "524220": ("NieRAutomata", "NieRAutomata.exe", True),
    "1091500": ("Cyberpunk 2077", "bin/x64/Cyberpunk2077.exe", True),
    "1086940": ("Baldurs Gate 3", "bin/bg3.exe", True),
}


@pytest.fixture
def steam(tmp_path, monkeypatch):
    home = tmp_path / "home"
    root = home / ".local/share/Steam"
    _write(
        root / "steamapps/libraryfolders.vdf",
        '"libraryfolders"\n{\n' f'\t"0"\n\t{{\n\t\t"path"\t\t"{root}"\n\t}}\n' "}\n",
    )
    for appid, (installdir, binary, prefix) in GAMES.items():
        _write(
            root / f"steamapps/appmanifest_{appid}.acf",
            f'"AppState"\n{{\n\t"appid"\t\t"{appid}"\n'
            f'\t"installdir"\t\t"{installdir}"\n}}\n',
        )
        _write(root / "steamapps/common" / installdir / binary, "")
        compat = root / "steamapps/compatdata" / appid
        compat.mkdir(parents=True)
        if prefix:
            (compat / "pfx/drive_c/users/steamuser").mkdir(parents=True)
    monkeypatch.setenv("HOME", str(home))
    monkeypatch.setenv("XDG_DATA_HOME", str(home / ".local/share"))
    monkeypatch.setenv("XDG_DOCUMENTS_DIR", str(home / "Documents"))
    return root
```

`tests/basic-games/test_detection.py`:

```python
import importlib
from pathlib import Path


def _load_basic_games():
    import basic_games
    from basic_games.basic_game import BasicGame

    importlib.reload(basic_games.steam_utils)
    BasicGame.steam_games = basic_games.steam_utils.find_games()
    return {type(g).__name__: g for g in basic_games.createPlugins()}


def test_steam_games_are_found_by_app_id(steam):
    from basic_games.steam_utils import find_games

    games = find_games()
    assert "524220" in games
    assert games["1091500"] == steam / "steamapps/common/Cyberpunk 2077"
    assert games.get("999999") is None
    assert "999999" not in games


def test_the_three_games_are_detected(steam):
    plugins = _load_basic_games()
    expected = {
        "NierAutomataGame": "NieRAutomata",
        "Cyberpunk2077Game": "Cyberpunk 2077",
        "BG3Game": "Baldurs Gate 3",
    }
    for cls, installdir in expected.items():
        game = plugins[cls]
        game.detectGame()
        assert game.isInstalled(), cls
        assert Path(game.gameDirectory().absolutePath()) == steam / "steamapps/common" / installdir


def test_other_stores_are_empty_on_linux():
    from basic_games import eadesktop_utils, epic_utils, gog_utils, origin_utils

    assert gog_utils.find_games() == {}
    assert origin_utils.find_games() == {}
    errors = []
    assert epic_utils.find_games(errors) == {} and errors == []
    assert eadesktop_utils.find_games(errors) == {} and errors == []
```

(Os nomes das classes — `NierAutomataGame`, `Cyberpunk2077Game`, `BG3Game` — são os do upstream; **confira em `deps/basic_games/games/game_*.py`** e use os reais. O pacote instalado chama-se `basic_games` em `bin/plugins/`. Se `createPlugins()` exigir algo do MO2 além do `mobase`, use o mínimo necessário e registre.)

Review Focus "GameSteamId em lista": acrescente a `test_detection.py` um teste com um jogo de `GameSteamId` com dois IDs (escolha um real do upstream com lista, ou uma subclasse mínima de `BasicGame` no teste com `GameSteamId = ["111", "524220"]`, `GameBinary = "NieRAutomata.exe"`) que deve ser detectado pelo segundo ID.

No `build.sh`, `ALL_TARGETS` depois de `fnistool`: `basic_games`, e:

```bash
# basic_games (Phase 4d.2): the meta-plugin of the games that are not Bethesda's, with
# its pip dependencies; with --tests its pytest suite (tests/basic-games) imports the
# installed package with the installed mobase, in a HOME with a fake Steam
build_basic_games() {
    python_plugin basic_games basic_games
    [[ $TESTS == 1 ]] || return 0
    [[ -d $INSTALL/bin/plugins/basic_games/lib/vdf ]] ||
        die "basic_games: the pip package vdf is not installed in lib/"
    PYTHONPATH="$INSTALL/bin/plugins/plugin_python/libs:$INSTALL/bin/plugins" \
        python3 -m pytest -p no:cacheprovider "$ROOT/tests/basic-games" -q ||
        die "basic_games tests failed"
}
```

(`python_plugin` é a função da 4d.1; confira a assinatura e o nome real da pasta de `vdf` em `lib/` depois do primeiro build.)

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only basic_games --tests` — Expected: FAIL (`ModuleNotFoundError: winreg` ao importar `steam_utils` no Linux).

- [ ] **Step 3: Implementar os ramos Linux**

`steam_utils.py`: `import winreg` só no Windows (`if sys.platform == "win32": import winreg`), e no fim:

```python
if sys.platform != "win32":
    import collections.abc

    class _LinuxSteamGames(collections.abc.Mapping[str, Path]):
        """The installed Steam games, asked to Mod Organizer by app ID (Linux:
        mobase.utils.steamApp, the Steam module of uibase); it cannot be listed."""

        def _find(self, appid: object) -> Path | None:
            import mobase

            app = mobase.utils.steamApp(str(appid))
            return Path(app["gamePath"]) if app else None

        def __getitem__(self, appid: str) -> Path:
            path = self._find(appid)
            if path is None:
                raise KeyError(appid)
            return path

        def __contains__(self, appid: object) -> bool:
            return self._find(appid) is not None

        def __iter__(self):
            return iter(())

        def __len__(self) -> int:
            return 0

    def find_games() -> dict[str, Path]:  # noqa: F811  (Linux replaces the Windows one)
        return _LinuxSteamGames()  # type: ignore[return-value]
```

(Mantenha a função Windows intacta acima; a redefinição só acontece fora do Windows. Se preferir, coloque o `if sys.platform == "win32": … else: …` em volta das duas versões — o que deixar o Windows byte-idêntico.)

`gog_utils.py`, `origin_utils.py`, `epic_utils.py`, `eadesktop_utils.py`: imports de `winreg` (e outras APIs Windows) só no Windows; no Linux `find_games(...)` devolve `{}` e registra uma vez, com `qInfo`, `"<loja>: only Steam games are detected on Linux"`. Leia cada arquivo: a assinatura (com ou sem `errors`) não muda.

- [ ] **Step 4: Rodar e ver passar** — o mesmo comando; Expected: os testes de `tests/basic-games` PASS.

- [ ] **Step 5: Commits** — `deps/basic_games: Linux: Steam games found by app ID, the other stores empty` (só `deps/basic_games`); `build: basic_games target`; `tests: basic_games: detection with a fake Steam`.

---

### Task 4: Pastas do usuário no prefixo do Proton

**Files:**
- Modify: `deps/basic_games/basic_game.py` (`replace_variables`, `_default_documents_directory`, `_default_saves_directory` ou equivalentes que usam `QStandardPaths`)
- Create: `deps/basic_games/proton_utils.py`
- Test: `tests/basic-games/test_folders.py`

**Interfaces:**
- Consumes: `mobase.utils.steamApp` (Task 2), fixture `steam` (Task 3).
- Produces: `proton_utils.user_profile(steam_ids: list[str], game_name: str) -> str | None` e `proton_utils.documents(steam_ids, game_name) -> str | None` (Linux; `None` → usar XDG).

- [ ] **Step 1: Testes que falham**

`tests/basic-games/test_folders.py`:

```python
from pathlib import Path

from test_detection import _load_basic_games


def _user(steam: Path, appid: str) -> Path:
    return steam / "steamapps/compatdata" / appid / "pfx/drive_c/users/steamuser"


def test_cyberpunk_folders_in_its_prefix(steam):
    game = _load_basic_games()["Cyberpunk2077Game"]
    game.detectGame()
    user = _user(steam, "1091500")
    assert Path(game.documentsDirectory().absolutePath()) == (
        user / "AppData/Local/CD Projekt Red/Cyberpunk 2077"
    )
    assert Path(game.savesDirectory().absolutePath()) == (
        user / "Saved Games/CD Projekt Red/Cyberpunk 2077"
    )


def test_bg3_folders_in_its_prefix(steam):
    game = _load_basic_games()["BG3Game"]
    game.detectGame()
    user = _user(steam, "1086940")
    docs = user / "AppData/Local/Larian Studios/Baldur's Gate 3"
    assert Path(game.documentsDirectory().absolutePath()) == docs
    assert Path(game.savesDirectory().absolutePath()) == (
        docs / "PlayerProfiles/Public/Savegames/Story"
    )


def test_without_prefix_the_xdg_folders(steam, caplog):
    import shutil

    shutil.rmtree(steam / "steamapps/compatdata/1091500/pfx")
    game = _load_basic_games()["Cyberpunk2077Game"]
    game.detectGame()
    docs = Path(game.documentsDirectory().absolutePath())
    assert "compatdata" not in str(docs)
    assert str(docs).startswith(str(Path.home()))
```

(O aviso de "sem prefixo" vai pelo log do Qt/`qWarning`; se for observável no `caplog`/`capfd`, acrescente a asserção; senão, registre no relatório que é verificado no teste manual. O `test_detection` é importável porque o `pytest` roda com `rootdir` em `tests/basic-games`; se não for, mova `_load_basic_games` para o `conftest.py` como fixture.)

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only basic_games --tests` — Expected: FAIL (documentos em `$HOME/AppData/…`, fora do prefixo).

- [ ] **Step 2: `proton_utils.py`**

```python
"""Linux: the Windows user folders of a Steam game are in its Proton prefix
(Phase 4d.2, spec section 3.2); without a prefix, None (the caller keeps the XDG
folders of today)."""

from __future__ import annotations

import sys

from PyQt6.QtCore import qWarning

_warned: set[str] = set()


def _compat_data(steam_ids: list[str], game_name: str) -> str | None:
    if sys.platform == "win32":
        return None
    import mobase

    for appid in steam_ids:
        app = mobase.utils.steamApp(str(appid))
        if app is None:
            continue
        if app["compatData"]:
            return app["compatData"]
        break
    if game_name not in _warned:
        _warned.add(game_name)
        qWarning(
            f"{game_name}: no Proton prefix (run the game once from Steam); "
            "using the folders of Linux for its documents and saves"
        )
    return None


def user_profile(steam_ids: list[str], game_name: str) -> str | None:
    compat = _compat_data(steam_ids, game_name)
    return f"{compat}/pfx/drive_c/users/steamuser" if compat else None


def documents(steam_ids: list[str], game_name: str) -> str | None:
    profile = user_profile(steam_ids, game_name)
    return f"{profile}/Documents" if profile else None
```

(Sem `GameSteamId`, `steam_ids` vazio → o aviso também sai: aceite, ou não avise quando a lista é vazia — escolha não avisar quando vazia, pois jogos sem Steam não têm prefixo, e diga no relatório.)

- [ ] **Step 3: `basic_game.py`**

Em `replace_variables(value, game)`, no Linux use `proton_utils` antes do `QStandardPaths`:

```python
    if value.find("%DOCUMENTS%") != -1:
        docs = None
        if sys.platform != "win32":
            from .proton_utils import documents

            docs = documents(game.steamAPPIds(), game.gameName())
        value = value.replace(
            "%DOCUMENTS%",
            docs
            or QStandardPaths.writableLocation(
                QStandardPaths.StandardLocation.DocumentsLocation
            ),
        )
```

e o mesmo para `%USERPROFILE%` com `user_profile` (fallback `HomeLocation`). Os padrões de `documentsDirectory`/`savesDirectory` que usam `DocumentsLocation` (`_default_documents_directory`, ~linhas 220-235) recebem o mesmo tratamento. `game.steamAPPIds()` é um nome de exemplo: use o acesso real aos IDs da Steam do jogo (`self._mappings.steamAPPId.get()` devolve a lista — leia `BasicGameOptionsMapping`) e o nome real do método/atributo; se `replace_variables` não tiver acesso, passe a lista a partir de onde é chamada. `import sys` no topo se faltar. Windows byte-idêntico (os ramos novos só fora do Windows).

- [ ] **Step 4: Rodar e ver passar** — o mesmo comando; Expected: `test_folders.py` e `test_detection.py` PASS.

- [ ] **Step 5: Commits** — `deps/basic_games: Linux: the user folders of a game in its Proton prefix`; `tests: basic_games: folders in the Proton prefix`.

---

### Task 5: BG3 sem LSLib no Linux, e Cyberpunk

**Files:**
- Modify: `deps/basic_games/games/baldursgate3/bg3_utils.py` (ponto de entrada da geração do `modsettings.lsx`) e, se for por onde o LSLib é acionado, `lslib_retriever.py`/`pak_parser.py` — só o necessário para que, no Linux, nada do LSLib rode
- Modify: o que a revisão achar em `games/game_cyberpunk2077.py`
- Test: `tests/basic-games/test_bg3_linux.py`

**Interfaces:**
- Consumes: Task 3 (pacote instalado, fixture `steam`, `_load_basic_games`), Task 4 (pastas no prefixo).

Contexto (spec §1.3): o `Divine` do LSLib não funciona no Linux (valida caminhos com `System.Uri` e quebra com qualquer caminho Unix). Decisão do usuário: desativar a ordem de carga do BG3 no Linux, com aviso; o resto do plugin do BG3 segue funcionando.

- [ ] **Step 1: Teste que falha**

Leia `bg3_utils.py` (`construct_modsettings_xml` e quem a chama), `lslib_retriever.py` (`download_lslib_if_missing`) e `pak_parser.py` (`run_divine`) no `deps/basic_games` para achar o ponto único por onde a geração começa. Escreva `tests/basic-games/test_bg3_linux.py`:

```python
import subprocess
import urllib.request


def test_load_order_does_not_use_lslib_on_linux(steam, monkeypatch):
    from test_detection import _load_basic_games

    calls = []
    monkeypatch.setattr(subprocess, "run", lambda *a, **k: calls.append(("run", a)))
    monkeypatch.setattr(subprocess, "Popen", lambda *a, **k: calls.append(("popen", a)))
    monkeypatch.setattr(urllib.request, "urlopen", lambda *a, **k: calls.append(("urlopen", a)))
    monkeypatch.setattr(urllib.request, "urlretrieve", lambda *a, **k: calls.append(("urlretrieve", a)))

    game = _load_basic_games()["BG3Game"]
    game.detectGame()
    # <the real entry point of the load-order generation, called the way the plugin
    #  calls it; it must not raise and must report that it is not available on Linux>
    result = ...
    assert calls == []
    assert <the observable "not on Linux" outcome>
```

Substitua as duas linhas marcadas pelo ponto de entrada real e pelo resultado observável que você escolher no Step 2 (por exemplo: a função devolve `False` e uma mensagem foi registrada por um `qWarning`/um diálogo substituído por `monkeypatch`; se o plugin mostra um `QMessageBox`, faça `monkeypatch` de `QMessageBox.information`/`warning`/`exec` e assegure que foi chamado uma vez com o texto do aviso). A classe `BG3Game` e a forma de obter o `BG3Utils` vêm do código real (o `BG3Game` provavelmente cria o `utils` no `init(organizer)` — use o mínimo de `organizer` falso que o código exigir, ou chame a função do `BG3Utils` diretamente se for isso que o plugin expõe; registre a escolha).

Run: `./build.sh --only basic_games --tests` — Expected: FAIL (o código tenta baixar o LSLib ou rodar o `Divine.exe`).

- [ ] **Step 2: Implementar**

No ponto de entrada, no Linux (`if sys.platform != "win32":`), antes de qualquer download ou execução: registrar o motivo no log (`qWarning("Baldur's Gate 3: the load order (modsettings.lsx) is not generated on Linux: LSLib's Divine does not accept Unix paths; see the README")`) e avisar o usuário uma vez por sessão pelo mesmo canal que o plugin já usa para a falta do LSLib (texto em `self._utils.tr(...)` se o plugin traduz), e retornar como o plugin retorna quando o LSLib não está disponível. Windows byte-idêntico. O mapeamento de um `modsettings.lsx` existente do perfil (`bg3_file_mapper.py`) não muda.

- [ ] **Step 3: Revisão Linux do BG3 e do Cyberpunk**

```bash
grep -rn -E 'winreg|startfile|windll|CREATE_NO_WINDOW|\\\\|\.exe["'"'"']|os\.sep|subprocess' deps/basic_games/games/baldursgate3 deps/basic_games/games/game_cyberpunk2077.py deps/basic_games/basic_game.py deps/basic_games/basic_features --include=*.py
```

Para cada ocorrência fora do caminho do LSLib: corrigir em `sys.platform` só o que quebra no Linux (caminho para programa Windows → `Z:\…`; caminho do Linux montado com `\` → `pathlib`; `os.startfile` → `QDesktopServices.openUrl`), ou registrar "não precisa" com o motivo. O `redMod.exe` do Cyberpunk roda por `organizer.startApplication()` — conferir os argumentos com caminhos (a lista de mods, `-modlist=…`): se forem caminhos do Linux para o `redMod.exe`, converter para `Z:\…`.

- [ ] **Step 4: Rodar e ver passar** — `./build.sh --only basic_games --tests`; Expected: todos os testes de `tests/basic-games` PASS.

- [ ] **Step 5: Commits** — `deps/basic_games: Linux: Baldur's Gate 3 does not generate the load order (LSLib)`; um commit por outro patch (`deps/basic_games: Linux: Cyberpunk 2077: …`); `tests: basic_games: Baldur's Gate 3 does not run LSLib on Linux`.

---

### Task 6: Container, documentação, checklist e ponto de controle de CI

**Files:**
- Modify: `README.md`, `ROADMAP.md`, `STATE.md`, `deps/README.md` (notas dos patches)
- Create: `docs/testing/phase4d2-manual-checklist.md`

- [ ] **Step 1: README** — na subseção "Plugins Python", um parágrafo sobre o `basic_games`: jogos da Steam detectados no Linux pelo app ID; documentos e saves no prefixo do Proton (rodar o jogo uma vez pela Steam); só Steam (GOG/Epic/EA/Origin não); no BG3 a ordem de carga (`modsettings.lsx`) ainda não é gerada no Linux (defeito do LSLib com caminhos Unix) — gere-a por fora e o MO2 a mapeia do perfil. Sem pacote novo.

- [ ] **Step 2: Checklist** — `docs/testing/phase4d2-manual-checklist.md`, no formato do da 4d.1:
  - Preparação: MO2 pelo box com o `install/` do worktree `ModOrganizerLinux-4d2`.
  - NieR:Automata: criar instância (o jogo aparece na lista), instalar um mod simples (pasta na raiz do jogo), executar o jogo pelo MO2 (VFS), o mod é visto; fechar.
  - Cyberpunk 2077: criar instância; documentos/saves apontam para `…/compatdata/1091500/pfx/drive_c/users/steamuser/…` (aba Saves lista os saves do jogo, se houver); instalar um mod de arquivo (`archive/pc/mod`); executar pelo MO2; opcional: um mod REDmod e o deploy do `redMod.exe`.
  - BG3: criar instância (detectado; documentos em `…/compatdata/1086940/pfx/drive_c/users/steamuser/AppData/Local/Larian Studios/Baldur's Gate 3`); instalar um mod `.pak` do Nexus; pedir a ordem de carga → o aviso de que ela não é gerada no Linux, sem travar; executar o jogo pelo MO2.
  - Regressão: Skyrim SE (SKSE), os plugins Python da 4d.1.

- [ ] **Step 3: ROADMAP e STATE** — 4d.2 `🟡 Em andamento`; linha de links da 4d.2; STATE com cabeçalho (fase atual 4d.2, branch `fase-4d2-basic-games` a partir de `fase-4d1-python`, worktree `../ModOrganizerLinux-4d2`), progresso (Tasks 1–7 e commits), limitações (só Steam; jogos do `basic_games` além dos três só "detectados, não validados"; ordem de carga do BG3 desativada no Linux pelo defeito do LSLib; Stellar Blade sem plugin), decisões de 2026-10-21 (escopo; abordagem A; spike do LSLib e a ordem do BG3 adiada), e uma pendência para reativar a ordem do BG3 quando o LSLib aceitar caminhos Unix. `deps/README.md`: notas dos patches Linux do `basic_games` e do binding `steamApp` no `plugin_python`.

- [ ] **Step 4: Build completo e commits** — `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --tests` verde (`==> done`); `docs: Phase 4d.2: basic_games in the README, manual checklist, roadmap and state`; `git commit --allow-empty -m "ci: Phase 4d.2 checkpoint [windows-ci]"` (com as linhas de atribuição). Sem push.

---

### Task 7: Teste manual com o usuário e fechamento

- [ ] Com o push autorizado e os CIs verdes, o usuário roda `docs/testing/phase4d2-manual-checklist.md`. Falhas viram correções com teste.
- [ ] STATE e ROADMAP com o resultado.
