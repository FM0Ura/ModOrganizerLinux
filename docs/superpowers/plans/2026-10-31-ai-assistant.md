# Assistente de IA Integrado (LangGraph + Ponte MCP para CLIs) — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrar ao Mod Organizer 2 Linux o plugin Python `ai_assistant` com painel `QDockWidget` acoplável, catálogo de 14 ferramentas sobre o `mobase` (diagnóstico de logs, resolução de arquivos na árvore virtual e CRUD de mods/plugins/INIs), motor LangChain + LangGraph com `SqliteSaver`, servidor MCP local (`127.0.0.1`) para agentes CLI (`claude`, `codex`, `agy`) e portão de confirmação visual para o Modo Seguro.

**Architecture:** Um plugin de pacote Python em `deps/plugin_ai_assistant/` construído via `mo2_configure_python(ai_assistant MODULE)` e instalado em `install/bin/plugins/ai_assistant/` com suas dependências (`langchain-core`, `langgraph`, `langgraph-checkpoint-sqlite`, `langchain-openai`, `langchain-anthropic`) em `lib/`. Um `ToolRegistry` central expõe as ferramentas simultaneamente para o `StateGraph` do LangGraph e para o servidor MCP JSON-RPC local (`127.0.0.1`), enquanto todas as chamadas ao `mobase` e aprovações do Modo Seguro passam por um `MainThreadDispatcher` na thread principal do Qt.

**Tech Stack:** Python 3.14, `mobase` (`plugin_python`), PyQt6, `pydantic`, `langchain-core`, `langgraph`, `langgraph-checkpoint-sqlite`, `langchain-openai`, `langchain-anthropic`, `pytest`.

**Spec:** `docs/superpowers/specs/2026-10-31-ai-assistant-design.md`

## Global Constraints

- **Worktree isolado:** `.claude/worktrees/ai-assistant` (branch `feature-ai-assistant` a partir de `main`). Nunca editar ou compilar fora deste worktree.
- **Zero escrita no import / hermeticidade do smoke test:** O `tests/smoke/modorganizer-smoke.sh` verifica que nada é criado no `$HOME` real e executa `restart_once` no mesmo processo (`MO2_SMOKE_RESTART_ONCE=1`). Nenhum arquivo, diretório ou banco SQLite pode ser criado durante `import` ou `init()` antes de o usuário interagir ou sem respeitar `organizer.pluginDataPath()`.
- **Idempotência em reinício no mesmo processo:** `onUserInterfaceInitialized` pode ser chamado duas vezes no mesmo processo durante `Change Game` / `restart_once`. O servidor MCP local e o registro do `QDockWidget` devem suportar reinicialização limpa sem falha de porta nem *double free*.
- **Thread safety do `mobase`:** Toda chamada a métodos mutáveis de `IOrganizer`, `IModList`, `IPluginList` e `IInstallationManager` deve rodar na thread principal do Qt (`MainThreadDispatcher`).
- **Bloqueio com VFS montado:** Qualquer ferramenta com `is_mutating=True` deve verificar se o VFS (`mo2-vfs`) está montado para a instância atual antes de executar; se estiver montado, recusa imediatamente com `"Blocked: Virtual filesystem is currently mounted (game or tool is running)."`.
- **Confinamento de caminhos (`realpath`):** `install_mod_from_downloads`, `set_profile_ini_setting` e `read_diagnostic_logs` devem rejeitar caminhos com `..` ou links simbólicos que escapem das raízes permitidas (`downloadsPath`, `profilePath`, pasta da instância, pasta do jogo e prefixo Proton).
- **Segurança de credenciais:** O arquivo `config.json` dentro de `<pluginDataPath>/ai_assistant/` é sempre gravado com permissão `0o600` e chaves de API nunca aparecem em logs.
- **Execução no container de build:** Todos os comandos `./build.sh` rodam dentro da distrobox `mo2-run` a partir da raiz do worktree `.claude/worktrees/ai-assistant` com `TMPDIR=/tmp/mo2t-ai`.

## Review Focus

1. **Path Traversal em `install_mod_from_downloads` e `set_profile_ini_setting`** (`../../etc/passwd` ou symlink apontando para fora de `downloads/` ou `profile/`): deve ser recusado com erro claro sem tocar no arquivo — Task 3, `test_path_traversal_rejected_in_downloads_and_ini`.
2. **Execução de `write_tool` enquanto o `mo2-vfs` está montado:** mesmo em Modo Autônomo ou já aprovado, se a verificação de mount indicar VFS ativo, a mutação não acontece — Task 2, `test_write_tool_blocked_when_vfs_mounted`.
3. **Requisição ao servidor MCP sem token de sessão ou com token inválido:** deve retornar HTTP 401/403 imediatamente sem listar nem executar ferramentas — Task 5, `test_mcp_rejects_unauthorized_requests`.
4. **Rejeição pelo usuário no Card do Modo Seguro durante execução do LangGraph ou MCP:** a função `mobase` não é chamada, o estado do `MockOrganizer` permanece intacto e o agente recebe `"Action rejected by user"` como resposta da ferramenta para continuar a conversa sem lançar exceção não tratada — Task 2, `test_safe_mode_rejection_does_not_mutate`.
5. **Reinicialização no mesmo processo (`onUserInterfaceInitialized` chamado 2 vezes):** não duplica docks na mesma `QMainWindow` nem falha ao reutilizar o servidor MCP — Task 6, `test_reinit_in_same_process_is_clean`.

---

### Task 1: Estrutura do Plugin `ai_assistant`, Config (`0600`) e Alvo no `build.sh`

**Files:**
- Create: `deps/plugin_ai_assistant/CMakeLists.txt`
- Create: `deps/plugin_ai_assistant/plugin-requirements.txt`
- Create: `deps/plugin_ai_assistant/src/CMakeLists.txt`
- Create: `deps/plugin_ai_assistant/src/__init__.py`
- Create: `deps/plugin_ai_assistant/src/config.py`
- Create: `deps/plugin_ai_assistant/src/plugin.py`
- Modify: `build.sh:24-31,490-500`
- Create: `tests/ai-assistant/conftest.py`
- Create: `tests/ai-assistant/test_config_and_plugin.py`

**Interfaces:**
- Produces:
  - `ai_assistant.config.AssistantConfig` (dataclass com `provider: str`, `permission_mode: str`, `model_name: str`, `ollama_base_url: str`, `openai_base_url: str`, `api_keys: dict[str, str]`, `load(data_dir: Path) -> AssistantConfig`, `save(data_dir: Path) -> None`, `detect_available_clis() -> dict[str, bool]`)
  - `ai_assistant.plugin.AiAssistantPlugin(mobase.IPluginTool)` e `ai_assistant.createPlugin() -> mobase.IPluginTool`

- [ ] **Step 1: Write the failing tests** em `tests/ai-assistant/conftest.py` e `tests/ai-assistant/test_config_and_plugin.py`:

```python
# tests/ai-assistant/conftest.py
from __future__ import annotations

import sys
from pathlib import Path
import pytest
from PyQt6.QtCore import QDir
from PyQt6.QtWidgets import QApplication

_app = None


def pytest_configure():
    global _app
    if QApplication.instance() is None:
        _app = QApplication(sys.argv)


class FakeMod:
    def __init__(self, name: str, path: Path, version: str = "1.0.0", nexus_id: int = 100, is_sep: bool = False, is_foreign: bool = False):
        self._name = name
        self._path = path
        self._version = version
        self._nexus_id = nexus_id
        self._is_sep = is_sep
        self._is_foreign = is_foreign

    def name(self) -> str:
        return self._name

    def absolutePath(self) -> str:
        return str(self._path)

    def newestVersion(self):
        return self._version

    def nexusId(self) -> int:
        return self._nexus_id

    def isSeparator(self) -> bool:
        return self._is_sep

    def isForeign(self) -> bool:
        return self._is_foreign


class FakeModList:
    def __init__(self, mods_dir: Path):
        self._mods_dir = mods_dir
        self._mods: dict[str, FakeMod] = {}
        self._active: dict[str, bool] = {}
        self._priority: dict[str, int] = {}

    def add_fake_mod(self, name: str, active: bool = True, priority: int | None = None, is_sep: bool = False) -> FakeMod:
        mod_path = self._mods_dir / name
        mod_path.mkdir(parents=True, exist_ok=True)
        mod = FakeMod(name, mod_path, is_sep=is_sep)
        self._mods[name] = mod
        self._active[name] = active
        self._priority[name] = len(self._priority) if priority is None else priority
        return mod

    def allModsByProfilePriority(self, profile=None) -> list[str]:
        return [k for k, _ in sorted(self._priority.items(), key=lambda x: x[1])]

    def state(self, name: str) -> int:
        if name not in self._mods:
            return 0
        # 0x02 == STATE_ACTIVE in mobase
        return 0x02 if self._active.get(name, False) else 0x01

    def priority(self, name: str) -> int:
        return self._priority.get(name, -1)

    def getMod(self, name: str):
        return self._mods.get(name)

    def setActive(self, names, active: bool) -> int:
        if isinstance(names, str):
            names = [names]
        count = 0
        for n in names:
            if n in self._mods and not self._mods[n].isSeparator():
                self._active[n] = bool(active)
                count += 1
        return count

    def setPriority(self, name: str, new_priority: int) -> bool:
        if name not in self._mods:
            return False
        self._priority[name] = int(new_priority)
        return True


class FakePluginList:
    def __init__(self):
        self._plugins: dict[str, dict] = {
            "Skyrim.esm": {"active": True, "order": 0, "master": True, "light": False, "origin": "data"},
            "SkyUI_SE.esp": {"active": True, "order": 1, "master": False, "light": False, "origin": "SkyUI"},
        }

    def pluginNames(self) -> list[str]:
        return list(self._plugins.keys())

    def state(self, name: str) -> int:
        if name not in self._plugins:
            return 0
        return 2 if self._plugins[name]["active"] else 1

    def loadOrder(self, name: str) -> int:
        return self._plugins.get(name, {}).get("order", -1)

    def isMasterFlagged(self, name: str) -> bool:
        return self._plugins.get(name, {}).get("master", False)

    def isLightFlagged(self, name: str) -> bool:
        return self._plugins.get(name, {}).get("light", False)

    def origin(self, name: str) -> str:
        return self._plugins.get(name, {}).get("origin", "")

    def setState(self, name: str, state: int) -> None:
        if name in self._plugins:
            self._plugins[name]["active"] = (state == 2)


class FakeGame:
    def __init__(self, game_dir: Path):
        self._game_dir = game_dir

    def gameName(self) -> str:
        return "Skyrim Special Edition"

    def gameShortName(self) -> str:
        return "SkyrimSE"

    def gameDirectory(self) -> QDir:
        return QDir(str(self._game_dir))


class FakeOrganizer:
    def __init__(self, root: Path):
        self.root = root
        self.base_dir = root / "instance"
        self.mods_dir = self.base_dir / "mods"
        self.downloads_dir = self.base_dir / "downloads"
        self.overwrite_dir = self.base_dir / "overwrite"
        self.profile_dir = self.base_dir / "profiles/Default"
        self.logs_dir = self.base_dir / "logs"
        self.plugin_data_dir = root / "plugin_data"
        self.game_dir = root / "game"
        for d in (self.mods_dir, self.downloads_dir, self.overwrite_dir, self.profile_dir, self.logs_dir, self.plugin_data_dir, self.game_dir):
            d.mkdir(parents=True, exist_ok=True)
        self._mod_list = FakeModList(self.mods_dir)
        self._plugin_list = FakePluginList()
        self._game = FakeGame(self.game_dir)
        self.installed_archives: list[tuple[str, str]] = []
        self.refresh_count = 0

    def basePath(self) -> str:
        return str(self.base_dir)

    def modsPath(self) -> str:
        return str(self.mods_dir)

    def downloadsPath(self) -> str:
        return str(self.downloads_dir)

    def overwritePath(self) -> str:
        return str(self.overwrite_dir)

    def profilePath(self) -> str:
        return str(self.profile_dir)

    def profileName(self) -> str:
        return "Default"

    def pluginDataPath(self) -> str:
        return str(self.plugin_data_dir)

    def modList(self) -> FakeModList:
        return self._mod_list

    def pluginList(self) -> FakePluginList:
        return self._plugin_list

    def managedGame(self) -> FakeGame:
        return self._game

    def resolvePath(self, subpath: str) -> str:
        for mod_name in reversed(self._mod_list.allModsByProfilePriority()):
            if self._mod_list.state(mod_name) == 0x02:
                candidate = self.mods_dir / mod_name / subpath
                if candidate.exists():
                    return str(candidate)
        return ""

    def installMod(self, archive_path: str, name_suggestion: str = ""):
        self.installed_archives.append((archive_path, name_suggestion))
        mod_name = name_suggestion or Path(archive_path).stem
        return self._mod_list.add_fake_mod(mod_name, active=True)

    def createMod(self, name: str):
        return self._mod_list.add_fake_mod(name, active=False, is_sep=name.endswith("_separator"))

    def refresh(self, save_changes: bool = True) -> None:
        self.refresh_count += 1

    def onUserInterfaceInitialized(self, callback) -> None:
        self._ui_callback = callback


@pytest.fixture
def fake_organizer(tmp_path):
    org = FakeOrganizer(tmp_path)
    org.modList().add_fake_mod("USSEP", active=True, priority=0)
    org.modList().add_fake_mod("SkyUI", active=True, priority=1)
    org.modList().add_fake_mod("Lux", active=False, priority=2)
    return org
```

```python
# tests/ai-assistant/test_config_and_plugin.py
from __future__ import annotations

import stat
from pathlib import Path
from ai_assistant import createPlugin
from ai_assistant.config import AssistantConfig, detect_available_clis


def test_config_roundtrip_and_0600_permissions(tmp_path: Path):
    data_dir = tmp_path / "ai_assistant"
    cfg = AssistantConfig(
        provider="claude_cli",
        permission_mode="safe",
        model_name="claude-3-7-sonnet",
        api_keys={"openai": "sk-secret-key-123"},
    )
    cfg.save(data_dir)
    cfg_file = data_dir / "config.json"
    assert cfg_file.exists()
    mode = stat.S_IMODE(cfg_file.stat().st_mode)
    assert mode == 0o600

    loaded = AssistantConfig.load(data_dir)
    assert loaded.provider == "claude_cli"
    assert loaded.permission_mode == "safe"
    assert loaded.api_keys["openai"] == "sk-secret-key-123"


def test_detect_available_clis_returns_expected_keys():
    clis = detect_available_clis()
    assert set(clis.keys()) == {"claude", "codex", "agy"}
    for val in clis.values():
        assert isinstance(val, bool)


def test_plugin_metadata_and_hermetic_init(fake_organizer, tmp_path: Path):
    plugin = createPlugin()
    assert plugin.name() == "AI Assistant"
    assert plugin.init(fake_organizer) is True
    # Hermetic check: init() alone must not write config.json before user action
    assert not (Path(fake_organizer.pluginDataPath()) / "ai_assistant/config.json").exists()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests`
Expected: FAIL (`unknown target: ai_assistant`).

- [ ] **Step 3: Implement CMake files, `plugin-requirements.txt`, `config.py`, `plugin.py` and `build.sh` target**

Create `deps/plugin_ai_assistant/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
project(ai_assistant)

find_package(mo2-cmake CONFIG REQUIRED)

add_subdirectory(src)
```

Create `deps/plugin_ai_assistant/plugin-requirements.txt`:
```text
langchain-core>=0.3.0
langgraph>=0.2.0
langgraph-checkpoint-sqlite>=2.0.0
langchain-openai>=0.2.0
langchain-anthropic>=0.2.0
```

Create `deps/plugin_ai_assistant/src/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)

add_custom_target(ai_assistant ALL)
mo2_configure_python(ai_assistant MODULE)
```

Create `deps/plugin_ai_assistant/src/__init__.py`:
```python
"""Entry point for the AI Assistant MO2 plugin."""

from __future__ import annotations

import os
import sys

_lib_dir = os.path.join(os.path.dirname(__file__), "lib")
if os.path.isdir(_lib_dir) and _lib_dir not in sys.path:
    sys.path.insert(0, _lib_dir)

from .plugin import AiAssistantPlugin  # noqa: E402


def createPlugin() -> AiAssistantPlugin:
    return AiAssistantPlugin()
```

Create `deps/plugin_ai_assistant/src/config.py`:
```python
from __future__ import annotations

from dataclasses import dataclass, field, asdict
import json
import os
import shutil
import subprocess
from pathlib import Path

PROVIDERS = (
    "claude_cli",
    "codex_cli",
    "agy_cli",
    "ollama",
    "gemini",
    "openai",
    "anthropic",
)

PERMISSION_MODES = ("safe", "autonomous")


def _in_container() -> bool:
    return os.path.exists("/.dockerenv") or os.path.exists("/run/.containerenv")


def detect_available_clis() -> dict[str, bool]:
    result: dict[str, bool] = {}
    in_box = _in_container() and shutil.which("distrobox-host-exec") is not None
    for binary in ("claude", "codex", "agy"):
        if shutil.which(binary) is not None:
            result[binary] = True
        elif in_box:
            try:
                proc = subprocess.run(
                    ["distrobox-host-exec", "sh", "-c", f"command -v {binary}"],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=3,
                    check=False,
                )
                result[binary] = proc.returncode == 0
            except Exception:
                result[binary] = False
        else:
            result[binary] = False
    return result


@dataclass
class AssistantConfig:
    provider: str = "claude_cli"
    permission_mode: str = "safe"
    model_name: str = ""
    ollama_base_url: str = "http://localhost:11434/v1"
    openai_base_url: str = "https://api.openai.com/v1"
    gemini_base_url: str = "https://generativelanguage.googleapis.com/v1beta/openai/"
    api_keys: dict[str, str] = field(default_factory=dict)

    @classmethod
    def load(cls, data_dir: Path) -> AssistantConfig:
        cfg_file = Path(data_dir) / "config.json"
        if not cfg_file.is_file():
            return cls()
        try:
            raw = json.loads(cfg_file.read_text(encoding="utf-8"))
            return cls(
                provider=raw.get("provider", "claude_cli"),
                permission_mode=raw.get("permission_mode", "safe"),
                model_name=raw.get("model_name", ""),
                ollama_base_url=raw.get("ollama_base_url", "http://localhost:11434/v1"),
                openai_base_url=raw.get("openai_base_url", "https://api.openai.com/v1"),
                gemini_base_url=raw.get(
                    "gemini_base_url",
                    "https://generativelanguage.googleapis.com/v1beta/openai/",
                ),
                api_keys=dict(raw.get("api_keys") or {}),
            )
        except Exception:
            return cls()

    def save(self, data_dir: Path) -> None:
        data_path = Path(data_dir)
        data_path.mkdir(parents=True, exist_ok=True)
        cfg_file = data_path / "config.json"
        tmp_file = data_path / "config.json.tmp"
        fd = os.open(str(tmp_file), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                json.dump(asdict(self), f, indent=2, ensure_ascii=False)
            os.chmod(tmp_file, 0o600)
            os.replace(tmp_file, cfg_file)
            os.chmod(cfg_file, 0o600)
        finally:
            if tmp_file.exists():
                tmp_file.unlink(missing_ok=True)
```

Create `deps/plugin_ai_assistant/src/plugin.py`:
```python
from __future__ import annotations

import mobase
from PyQt6.QtCore import QCoreApplication
from PyQt6.QtGui import QIcon


class AiAssistantPlugin(mobase.IPluginTool):
    def __init__(self) -> None:
        super().__init__()
        self._organizer: mobase.IOrganizer | None = None
        self._dock = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        return True

    def name(self) -> str:
        return "AI Assistant"

    def localizedName(self) -> str:
        return self._tr("AI Assistant")

    def author(self) -> str:
        return "ModOrganizerLinux"

    def description(self) -> str:
        return self._tr("Integrated AI agent for mod management, log analysis, and debugging.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.FINAL)

    def settings(self) -> list[mobase.PluginSetting]:
        return [
            mobase.PluginSetting("enabled", self._tr("Enable the AI Assistant dock panel"), True),
        ]

    def displayName(self) -> str:
        return self._tr("AI Assistant")

    def tooltip(self) -> str:
        return self._tr("Open or focus the AI Assistant panel")

    def icon(self) -> QIcon:
        return QIcon()

    def display(self) -> None:
        if self._dock is not None:
            self._dock.show()
            self._dock.raise_()

    def _tr(self, text: str) -> str:
        return QCoreApplication.translate("AiAssistantPlugin", text)
```

Modify `build.sh`:
Add `ai_assistant` to `ALL_TARGETS` and define `build_ai_assistant()`:
```bash
build_ai_assistant() {
    python_plugin ai_assistant ai_assistant
    [[ $TESTS == 1 ]] || return 0
    [[ -d $INSTALL/bin/plugins/ai_assistant/lib/langgraph ]] ||
        die "ai_assistant: langgraph is not installed in lib/"
    PYTHONDONTWRITEBYTECODE=1 \
        PYTHONPATH="$INSTALL/bin/plugins/plugin_python/libs:$INSTALL/bin/plugins:$INSTALL/bin/plugins/ai_assistant/lib" \
        python3 -m pytest -p no:cacheprovider "$ROOT/tests/ai-assistant" -q ||
        die "ai_assistant tests failed"
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests`
Expected: PASS (3 passed).

- [ ] **Step 5: Commit**

```bash
git -C .claude/worktrees/ai-assistant checkout -- '*.ts' 2>/dev/null || true
git -C .claude/worktrees/ai-assistant add build.sh deps/plugin_ai_assistant tests/ai-assistant
git -C .claude/worktrees/ai-assistant commit -m "deps/plugin_ai_assistant: scaffold plugin, config (0600) and build.sh target"
```

---

### Task 2: `ToolRegistry` e `MainThreadDispatcher` (Portão Modo Seguro vs Autônomo e Bloqueio VFS)

**Files:**
- Create: `deps/plugin_ai_assistant/src/tools/__init__.py`
- Create: `deps/plugin_ai_assistant/src/tools/registry.py`
- Create: `deps/plugin_ai_assistant/src/tools/dispatcher.py`
- Create: `tests/ai-assistant/test_registry_and_dispatcher.py`

**Interfaces:**
- Produces:
  - `ai_assistant.tools.registry.ToolSpec` (`name: str`, `description: str`, `args_schema: type[BaseModel]`, `func: Callable`, `is_mutating: bool`, `destructive: bool`)
  - `ai_assistant.tools.registry.ToolRegistry` (`register(...)`, `get(name) -> ToolSpec`, `all_specs() -> list[ToolSpec]`, `to_mcp_tools() -> list[dict]`, `to_langchain_tools(dispatcher) -> tuple[list[BaseTool], list[BaseTool]]`)
  - `ai_assistant.tools.dispatcher.MainThreadDispatcher(QObject)` (`execute(tool_name: str, args: dict) -> dict`, `set_permission_mode(mode: str)`, `set_vfs_check(fn: Callable[[], bool])`, `approval_requested: pyqtSignal(str, str, dict, bool)`, `resolve_approval(request_id: str, decision: str)`)

- [ ] **Step 1: Write the failing tests** em `tests/ai-assistant/test_registry_and_dispatcher.py`:

```python
from __future__ import annotations

import threading
from pydantic import BaseModel, Field
from ai_assistant.tools.registry import ToolRegistry
from ai_assistant.tools.dispatcher import MainThreadDispatcher


class DummyReadArgs(BaseModel):
    query: str = Field(default="", description="Filter")


class DummyWriteArgs(BaseModel):
    mod_name: str = Field(..., description="Mod to toggle")


def _make_registry(fake_organizer) -> ToolRegistry:
    reg = ToolRegistry()

    @reg.register(
        name="dummy_read",
        description="Read mods",
        args_schema=DummyReadArgs,
        is_mutating=False,
    )
    def dummy_read(org, query: str = "") -> dict:
        return {"mods": org.modList().allModsByProfilePriority(), "query": query}

    @reg.register(
        name="dummy_write",
        description="Disable mod",
        args_schema=DummyWriteArgs,
        is_mutating=True,
    )
    def dummy_write(org, mod_name: str) -> dict:
        org.modList().setActive([mod_name], False)
        return {"disabled": mod_name}

    return reg


def test_registry_exports_mcp_and_langchain_tools(fake_organizer):
    reg = _make_registry(fake_organizer)
    disp = MainThreadDispatcher(fake_organizer, reg, permission_mode="autonomous")
    mcp_list = reg.to_mcp_tools()
    assert len(mcp_list) == 2
    assert {t["name"] for t in mcp_list} == {"dummy_read", "dummy_write"}
    safe_lc, write_lc = reg.to_langchain_tools(disp)
    assert [t.name for t in safe_lc] == ["dummy_read"]
    assert [t.name for t in write_lc] == ["dummy_write"]


def test_write_tool_blocked_when_vfs_mounted(fake_organizer):
    reg = _make_registry(fake_organizer)
    disp = MainThreadDispatcher(
        fake_organizer,
        reg,
        permission_mode="autonomous",
        vfs_mounted_check=lambda: True,
    )
    res = disp.execute("dummy_write", {"mod_name": "USSEP"})
    assert res["ok"] is False
    assert "Virtual filesystem is currently mounted" in res["error"]
    assert fake_organizer.modList().state("USSEP") == 0x02


def test_safe_mode_rejection_does_not_mutate(fake_organizer):
    reg = _make_registry(fake_organizer)
    disp = MainThreadDispatcher(
        fake_organizer,
        reg,
        permission_mode="safe",
        vfs_mounted_check=lambda: False,
    )

    def auto_reject(req_id: str, tool_name: str, args: dict, destructive: bool):
        disp.resolve_approval(req_id, "reject")

    disp.approval_requested.connect(auto_reject)
    result_box = {}

    def worker_call():
        result_box["res"] = disp.execute("dummy_write", {"mod_name": "USSEP"})

    t = threading.Thread(target=worker_call)
    t.start()
    from PyQt6.QtWidgets import QApplication
    while t.is_alive():
        QApplication.processEvents()
    t.join(timeout=2)

    assert result_box["res"]["ok"] is False
    assert "rejected by user" in result_box["res"]["error"].lower()
    assert fake_organizer.modList().state("USSEP") == 0x02


def test_safe_mode_approve_session_allows_subsequent_writes(fake_organizer):
    reg = _make_registry(fake_organizer)
    disp = MainThreadDispatcher(
        fake_organizer,
        reg,
        permission_mode="safe",
        vfs_mounted_check=lambda: False,
    )
    prompts = []

    def approve_all_once(req_id: str, tool_name: str, args: dict, destructive: bool):
        prompts.append(tool_name)
        disp.resolve_approval(req_id, "approve_session")

    disp.approval_requested.connect(approve_all_once)
    out = []

    def worker_call():
        out.append(disp.execute("dummy_write", {"mod_name": "USSEP"}))
        out.append(disp.execute("dummy_write", {"mod_name": "SkyUI"}))

    t = threading.Thread(target=worker_call)
    t.start()
    from PyQt6.QtWidgets import QApplication
    while t.is_alive():
        QApplication.processEvents()
    t.join(timeout=2)

    assert len(prompts) == 1
    assert out[0]["ok"] is True
    assert out[1]["ok"] is True
    assert fake_organizer.modList().state("USSEP") == 0x01
    assert fake_organizer.modList().state("SkyUI") == 0x01
```

- [ ] **Step 2: Run test to verify it fails**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests`
Expected: FAIL (`ModuleNotFoundError: No module named 'ai_assistant.tools'`).

- [ ] **Step 3: Implement `registry.py` and `dispatcher.py`**

Create `deps/plugin_ai_assistant/src/tools/__init__.py`:
```python
from .registry import ToolRegistry, ToolSpec
from .dispatcher import MainThreadDispatcher

__all__ = ["ToolRegistry", "ToolSpec", "MainThreadDispatcher"]
```

Create `deps/plugin_ai_assistant/src/tools/registry.py`:
```python
from __future__ import annotations

from dataclasses import dataclass
import json
from typing import Callable, Any
from pydantic import BaseModel
from langchain_core.tools import StructuredTool, BaseTool


@dataclass
class ToolSpec:
    name: str
    description: str
    args_schema: type[BaseModel]
    func: Callable[..., dict[str, Any]]
    is_mutating: bool = False
    destructive: bool = False


class ToolRegistry:
    def __init__(self) -> None:
        self._specs: dict[str, ToolSpec] = {}

    def register(
        self,
        *,
        name: str,
        description: str,
        args_schema: type[BaseModel],
        is_mutating: bool = False,
        destructive: bool = False,
    ) -> Callable:
        def decorator(func: Callable[..., dict[str, Any]]) -> Callable[..., dict[str, Any]]:
            self._specs[name] = ToolSpec(
                name=name,
                description=description,
                args_schema=args_schema,
                func=func,
                is_mutating=is_mutating,
                destructive=destructive,
            )
            return func

        return decorator

    def get(self, name: str) -> ToolSpec | None:
        return self._specs.get(name)

    def all_specs(self) -> list[ToolSpec]:
        return list(self._specs.values())

    def to_mcp_tools(self) -> list[dict[str, Any]]:
        tools: list[dict[str, Any]] = []
        for spec in self._specs.values():
            schema = spec.args_schema.model_json_schema()
            tools.append(
                {
                    "name": spec.name,
                    "description": spec.description,
                    "inputSchema": schema,
                }
            )
        return tools

    def to_langchain_tools(self, dispatcher) -> tuple[list[BaseTool], list[BaseTool]]:
        safe_tools: list[BaseTool] = []
        write_tools: list[BaseTool] = []
        for spec in self._specs.values():
            def _make_runner(tool_name: str):
                def _run(**kwargs) -> str:
                    res = dispatcher.execute(tool_name, kwargs)
                    return json.dumps(res, ensure_ascii=False)
                return _run

            lc_tool = StructuredTool.from_function(
                func=_make_runner(spec.name),
                name=spec.name,
                description=spec.description,
                args_schema=spec.args_schema,
            )
            if spec.is_mutating:
                write_tools.append(lc_tool)
            else:
                safe_tools.append(lc_tool)
        return safe_tools, write_tools
```

Create `deps/plugin_ai_assistant/src/tools/dispatcher.py`:
```python
from __future__ import annotations

import os
import threading
import uuid
from pathlib import Path
from typing import Callable, Any
from PyQt6.QtCore import QObject, QThread, pyqtSignal, pyqtSlot
from PyQt6.QtWidgets import QApplication
from .registry import ToolRegistry


def default_vfs_mounted_check(organizer) -> bool:
    try:
        mounts = Path("/proc/mounts").read_text(encoding="utf-8", errors="replace")
        game_dir = organizer.managedGame().gameDirectory().absolutePath()
        real_game = os.path.realpath(game_dir)
        for line in mounts.splitlines():
            parts = line.split()
            if len(parts) >= 3 and ("mo2-vfs" in parts[0] or "fuse" in parts[2]):
                mount_point = parts[1].replace("\\040", " ")
                if mount_point.startswith(real_game) or mount_point.startswith(game_dir):
                    return True
    except Exception:
        pass
    return False


class MainThreadDispatcher(QObject):
    approval_requested = pyqtSignal(str, str, dict, bool)
    _invoke_signal = pyqtSignal(str)

    def __init__(
        self,
        organizer,
        registry: ToolRegistry,
        permission_mode: str = "safe",
        vfs_mounted_check: Callable[[], bool] | None = None,
    ) -> None:
        super().__init__()
        self._organizer = organizer
        self._registry = registry
        self._permission_mode = permission_mode
        self._session_approved_all = False
        self._vfs_check = vfs_mounted_check or (lambda: default_vfs_mounted_check(self._organizer))
        self._lock = threading.Lock()
        self._pending_calls: dict[str, dict[str, Any]] = {}
        self._pending_approvals: dict[str, dict[str, Any]] = {}
        self._invoke_signal.connect(self._on_main_thread_invoke)

    def set_permission_mode(self, mode: str) -> None:
        self._permission_mode = mode
        if mode == "safe":
            self._session_approved_all = False

    def reset_session_approvals(self) -> None:
        self._session_approved_all = False

    def execute(self, tool_name: str, args: dict[str, Any]) -> dict[str, Any]:
        spec = self._registry.get(tool_name)
        if spec is None:
            return {"ok": False, "error": f"Unknown tool: {tool_name}"}

        validated_args = spec.args_schema(**(args or {})).model_dump()

        if spec.is_mutating and self._vfs_check():
            return {
                "ok": False,
                "error": "Blocked: Virtual filesystem is currently mounted (game or tool is running).",
            }

        if spec.is_mutating and self._permission_mode == "safe" and not self._session_approved_all:
            decision = self._wait_for_user_approval(spec.name, validated_args, spec.destructive)
            if decision == "reject":
                return {"ok": False, "error": f"Action '{tool_name}' rejected by user."}
            if decision == "approve_session":
                self._session_approved_all = True

        app = QApplication.instance()
        if app is None or QThread.currentThread() == self.thread():
            return self._run_spec_directly(spec, validated_args)

        call_id = str(uuid.uuid4())
        done_event = threading.Event()
        with self._lock:
            self._pending_calls[call_id] = {
                "spec": spec,
                "args": validated_args,
                "event": done_event,
                "result": None,
            }
        self._invoke_signal.emit(call_id)
        done_event.wait(timeout=30.0)
        with self._lock:
            entry = self._pending_calls.pop(call_id, None)
        if entry is None or entry["result"] is None:
            return {"ok": False, "error": f"Timed out executing tool '{tool_name}' on main thread."}
        return entry["result"]

    def _wait_for_user_approval(self, tool_name: str, args: dict[str, Any], destructive: bool) -> str:
        req_id = str(uuid.uuid4())
        event = threading.Event()
        with self._lock:
            self._pending_approvals[req_id] = {"event": event, "decision": "reject"}
        self.approval_requested.emit(req_id, tool_name, args, destructive)
        app = QApplication.instance()
        if app is not None and QThread.currentThread() == self.thread():
            while not event.is_set():
                app.processEvents()
        else:
            event.wait(timeout=300.0)
        with self._lock:
            info = self._pending_approvals.pop(req_id, {"decision": "reject"})
        return info["decision"]

    @pyqtSlot(str, str)
    def resolve_approval(self, request_id: str, decision: str) -> None:
        with self._lock:
            info = self._pending_approvals.get(request_id)
            if info is not None:
                info["decision"] = decision
                info["event"].set()

    @pyqtSlot(str)
    def _on_main_thread_invoke(self, call_id: str) -> None:
        with self._lock:
            entry = self._pending_calls.get(call_id)
        if entry is None:
            return
        entry["result"] = self._run_spec_directly(entry["spec"], entry["args"])
        entry["event"].set()

    def _run_spec_directly(self, spec, validated_args: dict[str, Any]) -> dict[str, Any]:
        try:
            data = spec.func(self._organizer, **validated_args)
            return {"ok": True, "result": data}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests`
Expected: PASS (7 passed).

- [ ] **Step 5: Commit**

```bash
git -C .claude/worktrees/ai-assistant add deps/plugin_ai_assistant/src/tools tests/ai-assistant/test_registry_and_dispatcher.py
git -C .claude/worktrees/ai-assistant commit -m "deps/plugin_ai_assistant: tool registry and main-thread approval dispatcher"
```

---

### Task 3: Catálogo Completo de Ferramentas (`mod_tools.py`, `plugin_tools.py`, `diagnostic_tools.py`)

**Files:**
- Create: `deps/plugin_ai_assistant/src/tools/mod_tools.py`
- Create: `deps/plugin_ai_assistant/src/tools/plugin_tools.py`
- Create: `deps/plugin_ai_assistant/src/tools/diagnostic_tools.py`
- Modify: `deps/plugin_ai_assistant/src/tools/__init__.py`
- Create: `tests/ai-assistant/test_tools_catalog.py`

**Interfaces:**
- Consumes: `ToolRegistry`, `MainThreadDispatcher` (Task 2).
- Produces: `build_default_registry() -> ToolRegistry` registrando as 14 ferramentas da especificação (§4.1 e §4.2):
  - Leitura (`safe_tools`): `get_environment_info`, `list_mods`, `get_mod_details`, `list_load_order`, `find_file_owner`, `list_downloads`, `read_diagnostic_logs`.
  - Escrita (`write_tools`): `set_mods_active`, `set_mod_priority`, `set_plugins_active`, `install_mod_from_downloads`, `create_empty_mod`, `remove_mod`, `set_profile_ini_setting`.

- [ ] **Step 1: Write the failing tests** em `tests/ai-assistant/test_tools_catalog.py`:

```python
from __future__ import annotations

from pathlib import Path
from ai_assistant.tools import build_default_registry, MainThreadDispatcher


def _make_dispatcher(fake_organizer) -> MainThreadDispatcher:
    reg = build_default_registry()
    return MainThreadDispatcher(
        fake_organizer,
        reg,
        permission_mode="autonomous",
        vfs_mounted_check=lambda: False,
    )


def test_all_14_tools_are_registered(fake_organizer):
    reg = build_default_registry()
    names = {s.name for s in reg.all_specs()}
    assert names == {
        "get_environment_info",
        "list_mods",
        "get_mod_details",
        "list_load_order",
        "find_file_owner",
        "list_downloads",
        "read_diagnostic_logs",
        "set_mods_active",
        "set_mod_priority",
        "set_plugins_active",
        "install_mod_from_downloads",
        "create_empty_mod",
        "remove_mod",
        "set_profile_ini_setting",
    }


def test_read_tools_return_accurate_state(fake_organizer):
    disp = _make_dispatcher(fake_organizer)
    # Create a DLL inside SkyUI to test find_file_owner
    dll = fake_organizer.mods_dir / "SkyUI/SKSE/Plugins/skyui_se.dll"
    dll.parent.mkdir(parents=True, exist_ok=True)
    dll.write_text("binary", encoding="utf-8")

    # Create a fake crash log
    crash_log = fake_organizer.logs_dir / "crash-2026-10-31.log"
    crash_log.write_text("Unhandled exception at skyui_se.dll+0x1234\nStack trace line 1\n", encoding="utf-8")

    env = disp.execute("get_environment_info", {})["result"]
    assert env["game_name"] == "Skyrim Special Edition"
    assert env["profile_name"] == "Default"

    mods = disp.execute("list_mods", {"filter_query": "sky", "active_only": True})["result"]["mods"]
    assert len(mods) == 1
    assert mods[0]["name"] == "SkyUI"

    owner = disp.execute("find_file_owner", {"virtual_subpath": "SKSE/Plugins/skyui_se.dll"})["result"]
    assert owner["found"] is True
    assert owner["winning_mod"] == "SkyUI"

    logs = disp.execute("read_diagnostic_logs", {"log_kind": "crash", "tail_lines": 50})["result"]
    assert "skyui_se.dll+0x1234" in logs["content"]


def test_write_tools_crud_operations(fake_organizer):
    disp = _make_dispatcher(fake_organizer)

    # Toggle mods
    res = disp.execute("set_mods_active", {"mod_names": ["Lux"], "active": True})
    assert res["ok"] is True
    assert fake_organizer.modList().state("Lux") == 0x02

    # Change priority
    res = disp.execute("set_mod_priority", {"mod_name": "Lux", "new_priority": 0})
    assert res["ok"] is True
    assert fake_organizer.modList().priority("Lux") == 0

    # Toggle plugin
    res = disp.execute("set_plugins_active", {"plugin_names": ["SkyUI_SE.esp"], "active": False})
    assert res["ok"] is True
    assert fake_organizer.pluginList().state("SkyUI_SE.esp") == 1

    # Install from downloads
    archive = fake_organizer.downloads_dir / "NewMod.7z"
    archive.write_text("archive-bytes", encoding="utf-8")
    res = disp.execute(
        "install_mod_from_downloads",
        {"archive_filename": "NewMod.7z", "suggested_mod_name": "NewModInstalled"},
    )
    assert res["ok"] is True
    assert fake_organizer.modList().getMod("NewModInstalled") is not None

    # Create empty mod & remove it
    res = disp.execute("create_empty_mod", {"mod_name": "TempPatch", "as_separator": False})
    assert res["ok"] is True
    assert (fake_organizer.mods_dir / "TempPatch").is_dir()

    res = disp.execute("remove_mod", {"mod_name": "TempPatch"})
    assert res["ok"] is True
    assert not (fake_organizer.mods_dir / "TempPatch").exists()

    # Profile INI setting
    ini_file = fake_organizer.profile_dir / "SkyrimPrefs.ini"
    ini_file.write_text("[Display]\nbFull Screen=0\n", encoding="utf-8")
    res = disp.execute(
        "set_profile_ini_setting",
        {"ini_filename": "SkyrimPrefs.ini", "section": "Display", "key": "bFull Screen", "value": "1"},
    )
    assert res["ok"] is True
    assert "bFull Screen = 1" in ini_file.read_text(encoding="utf-8") or "bFull Screen=1" in ini_file.read_text(encoding="utf-8")


def test_path_traversal_rejected_in_downloads_and_ini(fake_organizer, tmp_path: Path):
    disp = _make_dispatcher(fake_organizer)
    outside = tmp_path / "secret.ini"
    outside.write_text("[Secret]\nkey=1\n", encoding="utf-8")

    res_dl = disp.execute(
        "install_mod_from_downloads",
        {"archive_filename": "../secret.ini", "suggested_mod_name": "Hack"},
    )
    assert res_dl["ok"] is False

    res_ini = disp.execute(
        "set_profile_ini_setting",
        {"ini_filename": "../../secret.ini", "section": "Secret", "key": "key", "value": "2"},
    )
    assert res_ini["ok"] is False
    assert "key=1" in outside.read_text(encoding="utf-8")
```

- [ ] **Step 2: Run test to verify it fails**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests`
Expected: FAIL (`ImportError: cannot import name 'build_default_registry'`).

- [ ] **Step 3: Implement `mod_tools.py`, `plugin_tools.py`, `diagnostic_tools.py` and `build_default_registry`**

Create `deps/plugin_ai_assistant/src/tools/mod_tools.py`:
```python
from __future__ import annotations

import os
import shutil
from pathlib import Path
from typing import Any
from pydantic import BaseModel, Field
from .registry import ToolRegistry


def _ensure_contained(base_dir: Path, target: Path) -> Path:
    real_base = Path(os.path.realpath(base_dir))
    real_target = Path(os.path.realpath(target))
    if real_target == real_base or real_base not in real_target.parents:
        raise ValueError(f"Path '{target.name}' escapes allowed directory '{base_dir}'.")
    return real_target


class ListModsArgs(BaseModel):
    filter_query: str = Field(default="", description="Case-insensitive substring to filter mod names")
    active_only: bool = Field(default=False, description="If true, return only enabled mods")
    limit: int = Field(default=200, ge=1, le=1000, description="Maximum number of mods to return")


class ModDetailsArgs(BaseModel):
    mod_name: str = Field(..., description="Exact name of the mod")


class ListDownloadsArgs(BaseModel):
    limit: int = Field(default=100, ge=1, le=500, description="Maximum downloads to list")


class SetModsActiveArgs(BaseModel):
    mod_names: list[str] = Field(..., min_length=1, description="List of mod names to enable or disable")
    active: bool = Field(..., description="True to enable, False to disable")


class SetModPriorityArgs(BaseModel):
    mod_name: str = Field(..., description="Exact mod name")
    new_priority: int = Field(..., ge=0, description="New priority index (0 is lowest priority)")


class InstallModArgs(BaseModel):
    archive_filename: str = Field(..., description="File name inside the downloads directory")
    suggested_mod_name: str = Field(default="", description="Optional name for the installed mod")


class CreateEmptyModArgs(BaseModel):
    mod_name: str = Field(..., min_length=1, description="Name for the new empty mod or separator")
    as_separator: bool = Field(default=False, description="If True, creates a visual separator")


class RemoveModArgs(BaseModel):
    mod_name: str = Field(..., min_length=1, description="Exact name of the installed mod to delete")


def register_mod_tools(reg: ToolRegistry) -> None:
    @reg.register(
        name="list_mods",
        description="List installed mods ordered by profile priority, optionally filtered by substring or active state.",
        args_schema=ListModsArgs,
        is_mutating=False,
    )
    def list_mods(organizer, filter_query: str = "", active_only: bool = False, limit: int = 200) -> dict[str, Any]:
        mod_list = organizer.modList()
        names = mod_list.allModsByProfilePriority()
        q = filter_query.strip().lower()
        items = []
        for name in names:
            is_active = bool(mod_list.state(name) & 0x02)
            if active_only and not is_active:
                continue
            if q and q not in name.lower():
                continue
            mod = mod_list.getMod(name)
            items.append(
                {
                    "name": name,
                    "active": is_active,
                    "priority": mod_list.priority(name),
                    "version": str(mod.newestVersion()) if mod else "",
                    "nexus_id": int(mod.nexusId()) if mod else 0,
                    "is_separator": bool(mod.isSeparator()) if mod else name.endswith("_separator"),
                    "is_foreign": bool(mod.isForeign()) if mod else False,
                }
            )
            if len(items) >= limit:
                break
        return {"total_matching": len(items), "mods": items}

    @reg.register(
        name="get_mod_details",
        description="Inspect details and top-level files of a specific mod.",
        args_schema=ModDetailsArgs,
        is_mutating=False,
    )
    def get_mod_details(organizer, mod_name: str) -> dict[str, Any]:
        mod_list = organizer.modList()
        mod = mod_list.getMod(mod_name)
        if mod is None:
            raise ValueError(f"Mod '{mod_name}' not found.")
        mod_path = Path(mod.absolutePath())
        files = []
        if mod_path.is_dir():
            for p in sorted(mod_path.rglob("*")):
                if p.is_file() and p.name != "meta.ini":
                    files.append(str(p.relative_to(mod_path)))
                    if len(files) >= 150:
                        break
        return {
            "name": mod.name(),
            "active": bool(mod_list.state(mod_name) & 0x02),
            "priority": mod_list.priority(mod_name),
            "version": str(mod.newestVersion()),
            "nexus_id": int(mod.nexusId()),
            "path": str(mod_path),
            "sample_files": files,
        }

    @reg.register(
        name="list_downloads",
        description="List archive files in the instance downloads folder available for installation.",
        args_schema=ListDownloadsArgs,
        is_mutating=False,
    )
    def list_downloads(organizer, limit: int = 100) -> dict[str, Any]:
        dl_dir = Path(organizer.downloadsPath())
        archives = []
        if dl_dir.is_dir():
            for p in sorted(dl_dir.iterdir(), key=lambda x: x.stat().st_mtime, reverse=True):
                if p.is_file() and not p.name.endswith(".meta"):
                    archives.append({"filename": p.name, "size_bytes": p.stat().st_size})
                    if len(archives) >= limit:
                        break
        return {"downloads": archives}

    @reg.register(
        name="set_mods_active",
        description="Enable or disable one or more mods in the active profile.",
        args_schema=SetModsActiveArgs,
        is_mutating=True,
    )
    def set_mods_active(organizer, mod_names: list[str], active: bool) -> dict[str, Any]:
        mod_list = organizer.modList()
        valid = []
        skipped = []
        for n in mod_names:
            mod = mod_list.getMod(n)
            if mod is None or mod.isSeparator() or mod.isForeign():
                skipped.append(n)
            else:
                valid.append(n)
        if valid:
            mod_list.setActive(valid, active)
        return {"updated": valid, "active": active, "skipped": skipped}

    @reg.register(
        name="set_mod_priority",
        description="Change the priority order of a mod in the current profile.",
        args_schema=SetModPriorityArgs,
        is_mutating=True,
    )
    def set_mod_priority(organizer, mod_name: str, new_priority: int) -> dict[str, Any]:
        ok = organizer.modList().setPriority(mod_name, new_priority)
        if not ok:
            raise ValueError(f"Could not set priority for mod '{mod_name}'.")
        return {"mod_name": mod_name, "new_priority": new_priority}

    @reg.register(
        name="install_mod_from_downloads",
        description="Install a mod archive located in the instance downloads directory.",
        args_schema=InstallModArgs,
        is_mutating=True,
    )
    def install_mod_from_downloads(organizer, archive_filename: str, suggested_mod_name: str = "") -> dict[str, Any]:
        if "/" in archive_filename or "\\" in archive_filename or ".." in archive_filename:
            raise ValueError("Invalid archive filename: must be a direct filename inside downloads.")
        dl_dir = Path(organizer.downloadsPath())
        target = _ensure_contained(dl_dir, dl_dir / archive_filename)
        if not target.is_file():
            raise FileNotFoundError(f"Archive '{archive_filename}' does not exist in downloads.")
        installed = organizer.installMod(str(target), suggested_mod_name)
        if installed is None:
            raise RuntimeError(f"Installation of '{archive_filename}' failed or was cancelled.")
        return {"installed_mod": installed.name(), "archive": archive_filename}

    @reg.register(
        name="create_empty_mod",
        description="Create a new empty mod folder or separator in the mod list.",
        args_schema=CreateEmptyModArgs,
        is_mutating=True,
    )
    def create_empty_mod(organizer, mod_name: str, as_separator: bool = False) -> dict[str, Any]:
        clean = mod_name.strip().replace("/", "_").replace("\\", "_")
        if not clean or clean in (".", ".."):
            raise ValueError("Invalid mod name.")
        if as_separator and not clean.endswith("_separator"):
            clean = f"{clean}_separator"
        created = organizer.createMod(clean)
        if created is None:
            raise RuntimeError(f"Failed to create mod '{clean}'.")
        organizer.refresh()
        return {"created": created.name(), "is_separator": as_separator}

    @reg.register(
        name="remove_mod",
        description="Delete an installed mod folder from the instance mods directory.",
        args_schema=RemoveModArgs,
        is_mutating=True,
        destructive=True,
    )
    def remove_mod(organizer, mod_name: str) -> dict[str, Any]:
        if "/" in mod_name or "\\" in mod_name or ".." in mod_name:
            raise ValueError("Invalid mod name.")
        mod_list = organizer.modList()
        mod = mod_list.getMod(mod_name)
        if mod is None or mod.isForeign():
            raise ValueError(f"Mod '{mod_name}' not found or cannot be removed.")
        mods_dir = Path(organizer.modsPath())
        target = _ensure_contained(mods_dir, Path(mod.absolutePath()))
        shutil.rmtree(target)
        organizer.refresh()
        return {"removed": mod_name}
```

Create `deps/plugin_ai_assistant/src/tools/plugin_tools.py`:
```python
from __future__ import annotations

import re
from pathlib import Path
from typing import Any
from pydantic import BaseModel, Field
from .registry import ToolRegistry
from .mod_tools import _ensure_contained


class ListLoadOrderArgs(BaseModel):
    active_only: bool = Field(default=False, description="If true, list only active plugins")


class SetPluginsActiveArgs(BaseModel):
    plugin_names: list[str] = Field(..., min_length=1, description="Plugin filenames (.esm/.esp/.esl)")
    active: bool = Field(..., description="True to enable, False to disable")


class SetProfileIniArgs(BaseModel):
    ini_filename: str = Field(..., description="INI filename inside current profile directory (e.g. SkyrimPrefs.ini)")
    section: str = Field(..., min_length=1, description="INI section name without brackets")
    key: str = Field(..., min_length=1, description="Setting key name")
    value: str = Field(..., description="New value to set")


def _update_ini_text(content: str, section: str, key: str, value: str) -> str:
    lines = content.splitlines(keepends=True)
    sec_pattern = re.compile(rf"^\s*\[{re.escape(section)}\]\s*$", re.IGNORECASE)
    key_pattern = re.compile(rf"^(\s*{re.escape(key)}\s*=).*$", re.IGNORECASE)

    in_section = False
    sec_found = False
    key_updated = False
    out: list[str] = []

    for line in lines:
        if line.lstrip().startswith("[") and "]" in line:
            if in_section and not key_updated:
                out.append(f"{key}={value}\n")
                key_updated = True
            in_section = bool(sec_pattern.match(line.strip()))
            if in_section:
                sec_found = True
            out.append(line)
            continue
        if in_section and not key_updated and key_pattern.match(line):
            out.append(f"{key}={value}\n")
            key_updated = True
        else:
            out.append(line)

    if in_section and not key_updated:
        if out and not out[-1].endswith("\n"):
            out[-1] += "\n"
        out.append(f"{key}={value}\n")
    elif not sec_found:
        if out and not out[-1].endswith("\n"):
            out[-1] += "\n"
        out.append(f"[{section}]\n{key}={value}\n")

    return "".join(out)


def register_plugin_tools(reg: ToolRegistry) -> None:
    @reg.register(
        name="list_load_order",
        description="List game plugins (.esm, .esp, .esl) in load order.",
        args_schema=ListLoadOrderArgs,
        is_mutating=False,
    )
    def list_load_order(organizer, active_only: bool = False) -> dict[str, Any]:
        plist = organizer.pluginList()
        names = plist.pluginNames()
        entries = []
        for n in names:
            active = plist.state(n) == 2
            if active_only and not active:
                continue
            entries.append(
                {
                    "name": n,
                    "active": active,
                    "load_order": plist.loadOrder(n),
                    "is_master": plist.isMasterFlagged(n),
                    "is_light": plist.isLightFlagged(n),
                    "origin": plist.origin(n),
                }
            )
        entries.sort(key=lambda x: (x["load_order"] < 0, x["load_order"], x["name"]))
        return {"plugins": entries}

    @reg.register(
        name="set_plugins_active",
        description="Enable or disable one or more game plugins (.esp/.esm/.esl) in the load order.",
        args_schema=SetPluginsActiveArgs,
        is_mutating=True,
    )
    def set_plugins_active(organizer, plugin_names: list[str], active: bool) -> dict[str, Any]:
        plist = organizer.pluginList()
        existing = set(plist.pluginNames())
        target_state = 2 if active else 1
        updated = []
        missing = []
        for n in plugin_names:
            if n in existing:
                plist.setState(n, target_state)
                updated.append(n)
            else:
                missing.append(n)
        return {"updated": updated, "active": active, "missing": missing}

    @reg.register(
        name="set_profile_ini_setting",
        description="Set a key=value inside a profile INI configuration file (such as Skyrim.ini or SkyrimPrefs.ini).",
        args_schema=SetProfileIniArgs,
        is_mutating=True,
    )
    def set_profile_ini_setting(organizer, ini_filename: str, section: str, key: str, value: str) -> dict[str, Any]:
        if "/" in ini_filename or "\\" in ini_filename or ".." in ini_filename or not ini_filename.lower().endswith(".ini"):
            raise ValueError("Invalid INI filename: must be a direct .ini file inside the profile folder.")
        prof_dir = Path(organizer.profilePath())
        target = _ensure_contained(prof_dir, prof_dir / ini_filename)
        if not target.is_file():
            raise FileNotFoundError(f"Profile INI '{ini_filename}' does not exist.")
        old_text = target.read_text(encoding="utf-8", errors="replace")
        new_text = _update_ini_text(old_text, section, key, value)
        target.write_text(new_text, encoding="utf-8")
        return {"ini_file": ini_filename, "section": section, "key": key, "value": value}
```

Create `deps/plugin_ai_assistant/src/tools/diagnostic_tools.py`:
```python
from __future__ import annotations

import os
from pathlib import Path
from typing import Any
from pydantic import BaseModel, Field
from .registry import ToolRegistry


class EmptyArgs(BaseModel):
    pass


class FindFileOwnerArgs(BaseModel):
    virtual_subpath: str = Field(..., min_length=1, description="Relative path inside Data (e.g. 'SKSE/Plugins/po3_Tweaks.dll')")


class ReadDiagnosticLogsArgs(BaseModel):
    log_kind: str = Field(default="auto", description="One of: 'auto', 'crash', 'mo2', 'skse', 'vfs'")
    tail_lines: int = Field(default=150, ge=10, le=500, description="Number of trailing lines to read")


def register_diagnostic_tools(reg: ToolRegistry) -> None:
    @reg.register(
        name="get_environment_info",
        description="Get active game, profile, instance paths, and mod/plugin counts.",
        args_schema=EmptyArgs,
        is_mutating=False,
    )
    def get_environment_info(organizer) -> dict[str, Any]:
        game = organizer.managedGame()
        mod_list = organizer.modList()
        all_mods = mod_list.allModsByProfilePriority()
        active_mods = [m for m in all_mods if (mod_list.state(m) & 0x02)]
        plist = organizer.pluginList()
        all_plugins = plist.pluginNames()
        active_plugins = [p for p in all_plugins if plist.state(p) == 2]
        return {
            "game_name": game.gameName() if game else "",
            "game_short_name": game.gameShortName() if game else "",
            "game_directory": game.gameDirectory().absolutePath() if game else "",
            "profile_name": organizer.profileName(),
            "instance_path": organizer.basePath(),
            "mods_path": organizer.modsPath(),
            "downloads_path": organizer.downloadsPath(),
            "total_mods": len(all_mods),
            "active_mods": len(active_mods),
            "total_plugins": len(all_plugins),
            "active_plugins": len(active_plugins),
        }

    @reg.register(
        name="find_file_owner",
        description="Find which mod provides a specific virtual path inside Data (useful for DLLs or assets in crash logs).",
        args_schema=FindFileOwnerArgs,
        is_mutating=False,
    )
    def find_file_owner(organizer, virtual_subpath: str) -> dict[str, Any]:
        clean = virtual_subpath.lstrip("/\\").replace("\\", "/")
        resolved = organizer.resolvePath(clean)
        if not resolved:
            return {"found": False, "virtual_subpath": clean, "winning_mod": None, "resolved_path": None}
        res_path = Path(os.path.realpath(resolved))
        mods_root = Path(os.path.realpath(organizer.modsPath()))
        overwrite_root = Path(os.path.realpath(organizer.overwritePath()))
        winning_mod = "Base Game / External"
        if mods_root in res_path.parents:
            rel = res_path.relative_to(mods_root)
            winning_mod = rel.parts[0]
        elif overwrite_root in res_path.parents:
            winning_mod = "Overwrite"
        return {
            "found": True,
            "virtual_subpath": clean,
            "winning_mod": winning_mod,
            "resolved_path": str(res_path),
        }

    @reg.register(
        name="read_diagnostic_logs",
        description="Read recent lines from MO2 logs or game crash logs.",
        args_schema=ReadDiagnosticLogsArgs,
        is_mutating=False,
    )
    def read_diagnostic_logs(organizer, log_kind: str = "auto", tail_lines: int = 150) -> dict[str, Any]:
        logs_dir = Path(organizer.basePath()) / "logs"
        candidates: list[Path] = []
        if logs_dir.is_dir():
            for p in logs_dir.glob("*.log"):
                if p.is_file() and p.stat().st_size > 0:
                    candidates.append(p)
        game = organizer.managedGame()
        if game and hasattr(game, "documentsDirectory"):
            try:
                docs_dir = Path(game.documentsDirectory().absolutePath())
                skse_dir = docs_dir / "SKSE"
                if skse_dir.is_dir():
                    for p in skse_dir.glob("*.log"):
                        if p.is_file() and p.stat().st_size > 0:
                            candidates.append(p)
            except Exception:
                pass

        if not candidates:
            return {"found": False, "log_file": None, "content": "No non-empty log files found."}

        kind = log_kind.lower()
        filtered = candidates
        if kind == "crash":
            filtered = [p for p in candidates if "crash" in p.name.lower()] or candidates
        elif kind == "mo2":
            filtered = [p for p in candidates if "mo_" in p.name.lower() or "mo2" in p.name.lower()] or candidates
        elif kind == "skse":
            filtered = [p for p in candidates if "skse" in p.name.lower()] or candidates

        filtered.sort(key=lambda p: p.stat().st_mtime, reverse=True)
        chosen = filtered[0]
        lines = chosen.read_text(encoding="utf-8", errors="replace").splitlines()
        snippet = "\n".join(lines[-min(tail_lines, 500):])
        return {
            "found": True,
            "log_file": str(chosen),
            "total_lines": len(lines),
            "content": snippet,
        }
```

Update `deps/plugin_ai_assistant/src/tools/__init__.py`:
```python
from .registry import ToolRegistry, ToolSpec
from .dispatcher import MainThreadDispatcher
from .mod_tools import register_mod_tools
from .plugin_tools import register_plugin_tools
from .diagnostic_tools import register_diagnostic_tools


def build_default_registry() -> ToolRegistry:
    reg = ToolRegistry()
    register_diagnostic_tools(reg)
    register_mod_tools(reg)
    register_plugin_tools(reg)
    return reg


__all__ = ["ToolRegistry", "ToolSpec", "MainThreadDispatcher", "build_default_registry"]
```

- [ ] **Step 4: Run test to verify it passes**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests`
Expected: PASS (11 passed).

- [ ] **Step 5: Commit**

```bash
git -C .claude/worktrees/ai-assistant add deps/plugin_ai_assistant/src/tools tests/ai-assistant/test_tools_catalog.py
git -C .claude/worktrees/ai-assistant commit -m "deps/plugin_ai_assistant: implement 14 diagnostic and CRUD tools"
```

---

### Task 4: Motor LangChain + LangGraph (`llm_factory.py` e `graph.py`)

**Files:**
- Create: `deps/plugin_ai_assistant/src/agent/__init__.py`
- Create: `deps/plugin_ai_assistant/src/agent/llm_factory.py`
- Create: `deps/plugin_ai_assistant/src/agent/graph.py`
- Create: `tests/ai-assistant/test_langgraph_agent.py`

**Interfaces:**
- Consumes: `AssistantConfig` (Task 1), `ToolRegistry`, `MainThreadDispatcher` (Tasks 2-3).
- Produces:
  - `ai_assistant.agent.llm_factory.create_chat_model(config: AssistantConfig) -> BaseChatModel`
  - `ai_assistant.agent.graph.build_system_prompt(organizer) -> str`
  - `ai_assistant.agent.graph.build_agent_graph(dispatcher: MainThreadDispatcher, model: BaseChatModel, sqlite_path: Path | None = None)`

- [ ] **Step 1: Write the failing tests** em `tests/ai-assistant/test_langgraph_agent.py`:

```python
from __future__ import annotations

from pathlib import Path
from typing import Any
from langchain_core.language_models.chat_models import BaseChatModel
from langchain_core.messages import BaseMessage, AIMessage, HumanMessage
from langchain_core.outputs import ChatResult, ChatGeneration
from ai_assistant.config import AssistantConfig
from ai_assistant.tools import build_default_registry, MainThreadDispatcher
from ai_assistant.agent.llm_factory import create_chat_model
from ai_assistant.agent.graph import build_agent_graph, build_system_prompt


class ScriptedToolChatModel(BaseChatModel):
    responses: list[AIMessage]
    call_idx: int = 0

    @property
    def _llm_type(self) -> str:
        return "scripted-test"

    def bind_tools(self, tools, **kwargs):
        return self

    def _generate(self, messages: list[BaseMessage], stop=None, run_manager=None, **kwargs: Any) -> ChatResult:
        msg = self.responses[min(self.call_idx, len(self.responses) - 1)]
        self.call_idx += 1
        return ChatResult(generations=[ChatGeneration(message=msg)])


def test_llm_factory_instantiates_providers():
    for provider, key in [("ollama", ""), ("openai", "sk-test"), ("gemini", "AIza-test"), ("anthropic", "sk-ant-test")]:
        cfg = AssistantConfig(provider=provider, api_keys={provider: key})
        llm = create_chat_model(cfg)
        assert llm is not None


def test_langgraph_executes_safe_and_write_tools_with_sqlite_checkpoint(fake_organizer, tmp_path: Path):
    reg = build_default_registry()
    disp = MainThreadDispatcher(
        fake_organizer,
        reg,
        permission_mode="autonomous",
        vfs_mounted_check=lambda: False,
    )
    scripted = ScriptedToolChatModel(
        responses=[
            AIMessage(
                content="",
                tool_calls=[{"name": "list_mods", "args": {"filter_query": "Lux"}, "id": "call_1", "type": "tool_call"}],
            ),
            AIMessage(
                content="",
                tool_calls=[
                    {"name": "set_mods_active", "args": {"mod_names": ["Lux"], "active": True}, "id": "call_2", "type": "tool_call"}
                ],
            ),
            AIMessage(content="I have enabled Lux for you."),
        ]
    )
    db_path = tmp_path / "checkpoints.sqlite"
    graph = build_agent_graph(disp, scripted, sqlite_path=db_path)
    sys_prompt = build_system_prompt(fake_organizer)
    out = graph.invoke(
        {"messages": [HumanMessage(content=f"{sys_prompt}\nEnable Lux please")]},
        config={"configurable": {"thread_id": "test-thread-1"}},
    )
    assert out["messages"][-1].content == "I have enabled Lux for you."
    assert fake_organizer.modList().state("Lux") == 0x02
    assert db_path.exists()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests`
Expected: FAIL (`ModuleNotFoundError: No module named 'ai_assistant.agent'`).

- [ ] **Step 3: Implement `llm_factory.py` and `graph.py`**

Create `deps/plugin_ai_assistant/src/agent/__init__.py`:
```python
from .llm_factory import create_chat_model
from .graph import build_agent_graph, build_system_prompt

__all__ = ["create_chat_model", "build_agent_graph", "build_system_prompt"]
```

Create `deps/plugin_ai_assistant/src/agent/llm_factory.py`:
```python
from __future__ import annotations

from langchain_core.language_models.chat_models import BaseChatModel
from langchain_openai import ChatOpenAI
from langchain_anthropic import ChatAnthropic
from ..config import AssistantConfig

DEFAULT_MODELS = {
    "ollama": "qwen2.5-coder:7b",
    "openai": "gpt-4o",
    "gemini": "gemini-2.5-flash",
    "anthropic": "claude-3-7-sonnet-latest",
}


def create_chat_model(config: AssistantConfig) -> BaseChatModel:
    provider = config.provider
    model = config.model_name.strip() or DEFAULT_MODELS.get(provider, "gpt-4o")

    if provider == "ollama":
        return ChatOpenAI(
            model=model,
            base_url=config.ollama_base_url,
            api_key="ollama",
            temperature=0.1,
        )
    if provider == "gemini":
        key = config.api_keys.get("gemini", "") or "missing-key"
        return ChatOpenAI(
            model=model,
            base_url=config.gemini_base_url,
            api_key=key,
            temperature=0.1,
        )
    if provider == "anthropic":
        key = config.api_keys.get("anthropic", "") or "missing-key"
        return ChatAnthropic(
            model_name=model,
            api_key=key,
            temperature=0.1,
        )
    key = config.api_keys.get("openai", "") or "missing-key"
    return ChatOpenAI(
        model=model,
        base_url=config.openai_base_url,
        api_key=key,
        temperature=0.1,
    )
```

Create `deps/plugin_ai_assistant/src/agent/graph.py`:
```python
from __future__ import annotations

import sqlite3
from pathlib import Path
from langchain_core.language_models.chat_models import BaseChatModel
from langchain_core.messages import SystemMessage
from langgraph.graph import StateGraph, MessagesState, START, END
from langgraph.prebuilt import ToolNode
from langgraph.checkpoint.memory import MemorySaver
from langgraph.checkpoint.sqlite import SqliteSaver
from ..tools.dispatcher import MainThreadDispatcher


def build_system_prompt(organizer) -> str:
    game = organizer.managedGame()
    game_name = game.gameName() if game else "Unknown Game"
    profile_name = organizer.profileName()
    mod_list = organizer.modList()
    all_mods = mod_list.allModsByProfilePriority()
    active_mods = sum(1 for m in all_mods if (mod_list.state(m) & 0x02))
    return (
        "You are the Mod Organizer 2 Linux AI Assistant.\n"
        f"Active Game: {game_name} | Active Profile: {profile_name} | "
        f"Mods: {active_mods}/{len(all_mods)} active.\n"
        "Always inspect logs, file owners, or mod lists using read tools before proposing changes.\n"
        "Explain your findings concisely and accurately."
    )


def build_agent_graph(
    dispatcher: MainThreadDispatcher,
    model: BaseChatModel,
    sqlite_path: Path | None = None,
):
    safe_tools, write_tools = dispatcher._registry.to_langchain_tools(dispatcher)
    all_tools = safe_tools + write_tools
    write_names = {t.name for t in write_tools}
    bound_model = model.bind_tools(all_tools)

    def agent_node(state: MessagesState):
        sys_msg = SystemMessage(content=build_system_prompt(dispatcher._organizer))
        msgs = state["messages"]
        if not msgs or not isinstance(msgs[0], SystemMessage):
            msgs = [sys_msg] + list(msgs)
        response = bound_model.invoke(msgs)
        return {"messages": [response]}

    def route_after_agent(state: MessagesState) -> str:
        last = state["messages"][-1]
        tool_calls = getattr(last, "tool_calls", None) or []
        if not tool_calls:
            return END
        if any(tc.get("name") in write_names for tc in tool_calls):
            return "write_tools"
        return "safe_tools"

    builder = StateGraph(MessagesState)
    builder.add_node("agent", agent_node)
    builder.add_node("safe_tools", ToolNode(safe_tools))
    builder.add_node("write_tools", ToolNode(write_tools))

    builder.add_edge(START, "agent")
    builder.add_conditional_edges("agent", route_after_agent, ["safe_tools", "write_tools", END])
    builder.add_edge("safe_tools", "agent")
    builder.add_edge("write_tools", "agent")

    if sqlite_path is not None:
        sqlite_path.parent.mkdir(parents=True, exist_ok=True)
        conn = sqlite3.connect(str(sqlite_path), check_same_thread=False)
        checkpointer = SqliteSaver(conn)
    else:
        checkpointer = MemorySaver()

    return builder.compile(checkpointer=checkpointer)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests`
Expected: PASS (13 passed).

- [ ] **Step 5: Commit**

```bash
git -C .claude/worktrees/ai-assistant add deps/plugin_ai_assistant/src/agent tests/ai-assistant/test_langgraph_agent.py
git -C .claude/worktrees/ai-assistant commit -m "deps/plugin_ai_assistant: LangChain model factory and LangGraph state machine"
```

---

### Task 5: Servidor MCP Local (`mcp_server.py`), Ponte Stdio e Runner de CLIs (`claude`, `codex`, `agy`)

**Files:**
- Create: `deps/plugin_ai_assistant/src/agent/mcp_server.py`
- Create: `deps/plugin_ai_assistant/src/agent/mcp_stdio_bridge.py`
- Create: `deps/plugin_ai_assistant/src/agent/cli_runner.py`
- Create: `deps/plugin_ai_assistant/src/agent/worker.py`
- Create: `tests/ai-assistant/test_mcp_and_cli.py`

**Interfaces:**
- Consumes: `MainThreadDispatcher` (Task 2), `AssistantConfig` (Task 1), `build_agent_graph` (Task 4).
- Produces:
  - `ai_assistant.agent.mcp_server.LocalMcpServer` (`start() -> tuple[int, str]`, `stop() -> None`, `endpoint_url() -> str`, `token: str`)
  - `ai_assistant.agent.cli_runner.build_cli_command(provider: str, prompt: str, mcp_config_path: Path, force_host_exec: bool | None = None) -> list[str]`
  - `ai_assistant.agent.worker.AgentTurnWorker(QThread)` (`response_ready: pyqtSignal(str)`, `error_occurred: pyqtSignal(str)`)

- [ ] **Step 1: Write the failing tests** em `tests/ai-assistant/test_mcp_and_cli.py`:

```python
from __future__ import annotations

import json
import urllib.request
import urllib.error
from pathlib import Path
import pytest
from ai_assistant.tools import build_default_registry, MainThreadDispatcher
from ai_assistant.agent.mcp_server import LocalMcpServer
from ai_assistant.agent.cli_runner import build_cli_command


def _post_json(url: str, payload: dict, token: str | None = None) -> tuple[int, dict]:
    data = json.dumps(payload).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if token is not None:
        headers["X-MO2-Token"] = token
    req = urllib.request.Request(url, data=data, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        return e.code, {}


def test_mcp_rejects_unauthorized_requests(fake_organizer):
    reg = build_default_registry()
    disp = MainThreadDispatcher(fake_organizer, reg, permission_mode="autonomous", vfs_mounted_check=lambda: False)
    srv = LocalMcpServer(disp)
    srv.start()
    try:
        code, _ = _post_json(srv.endpoint_url(), {"jsonrpc": "2.0", "id": 1, "method": "tools/list"}, token="wrong-token")
        assert code == 403
    finally:
        srv.stop()


def test_mcp_lists_and_calls_tools(fake_organizer):
    reg = build_default_registry()
    disp = MainThreadDispatcher(fake_organizer, reg, permission_mode="autonomous", vfs_mounted_check=lambda: False)
    srv = LocalMcpServer(disp)
    srv.start()
    try:
        code, resp = _post_json(
            srv.endpoint_url(),
            {"jsonrpc": "2.0", "id": 1, "method": "tools/list"},
            token=srv.token,
        )
        assert code == 200
        tools = resp["result"]["tools"]
        assert len(tools) == 14

        code, call_resp = _post_json(
            srv.endpoint_url(),
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "tools/call",
                "params": {"name": "set_mods_active", "arguments": {"mod_names": ["Lux"], "active": True}},
            },
            token=srv.token,
        )
        assert code == 200
        assert call_resp["result"]["isError"] is False
        assert fake_organizer.modList().state("Lux") == 0x02
    finally:
        srv.stop()


def test_build_cli_command_supports_claude_codex_and_agy(tmp_path: Path):
    mcp_cfg = tmp_path / "mcp.json"
    cmd_claude = build_cli_command("claude_cli", "Check crash log", mcp_cfg, force_host_exec=True)
    assert cmd_claude[0] == "distrobox-host-exec"
    assert "claude" in cmd_claude

    cmd_codex = build_cli_command("codex_cli", "List mods", mcp_cfg, force_host_exec=False)
    assert cmd_codex[0] == "codex"

    cmd_agy = build_cli_command("agy_cli", "List mods", mcp_cfg, force_host_exec=False)
    assert cmd_agy[0] == "agy"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests`
Expected: FAIL (`ModuleNotFoundError: No module named 'ai_assistant.agent.mcp_server'`).

- [ ] **Step 3: Implement `mcp_server.py`, `mcp_stdio_bridge.py`, `cli_runner.py` and `worker.py`**

Create `deps/plugin_ai_assistant/src/agent/mcp_server.py`:
```python
from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import secrets
import threading
from typing import Any
from ..tools.dispatcher import MainThreadDispatcher


class LocalMcpServer:
    def __init__(self, dispatcher: MainThreadDispatcher) -> None:
        self._dispatcher = dispatcher
        self.token = secrets.token_hex(24)
        self._httpd: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None
        self.port: int = 0

    def endpoint_url(self) -> str:
        return f"http://127.0.0.1:{self.port}/mcp"

    def start(self) -> tuple[int, str]:
        if self._httpd is not None:
            return self.port, self.token

        outer = self

        class _Handler(BaseHTTPRequestHandler):
            def log_message(self, format: str, *args: Any) -> None:
                return

            def do_POST(self) -> None:
                req_token = self.headers.get("X-MO2-Token", "")
                if not secrets.compare_digest(req_token, outer.token):
                    self.send_response(403)
                    self.end_headers()
                    return
                length = int(self.headers.get("Content-Length", "0"))
                raw = self.rfile.read(length)
                try:
                    msg = json.loads(raw.decode("utf-8"))
                except Exception:
                    self.send_response(400)
                    self.end_headers()
                    return

                reply = outer._handle_rpc(msg)
                body = json.dumps(reply, ensure_ascii=False).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self._httpd = ThreadingHTTPServer(("127.0.0.1", 0), _Handler)
        self.port = int(self._httpd.server_address[1])
        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)
        self._thread.start()
        return self.port, self.token

    def stop(self) -> None:
        if self._httpd is not None:
            self._httpd.shutdown()
            self._httpd.server_close()
            self._httpd = None

    def _handle_rpc(self, msg: dict[str, Any]) -> dict[str, Any]:
        rpc_id = msg.get("id")
        method = msg.get("method", "")
        params = msg.get("params") or {}

        if method == "initialize":
            return {
                "jsonrpc": "2.0",
                "id": rpc_id,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "mo2-ai-assistant", "version": "1.0.0"},
                },
            }
        if method == "tools/list":
            return {
                "jsonrpc": "2.0",
                "id": rpc_id,
                "result": {"tools": self._dispatcher._registry.to_mcp_tools()},
            }
        if method == "tools/call":
            tool_name = params.get("name", "")
            tool_args = params.get("arguments") or {}
            exec_res = self._dispatcher.execute(tool_name, tool_args)
            is_err = not exec_res.get("ok", False)
            text = json.dumps(exec_res, ensure_ascii=False)
            return {
                "jsonrpc": "2.0",
                "id": rpc_id,
                "result": {
                    "content": [{"type": "text", "text": text}],
                    "isError": is_err,
                },
            }
        return {
            "jsonrpc": "2.0",
            "id": rpc_id,
            "error": {"code": -32601, "message": f"Method not found: {method}"},
        }
```

Create `deps/plugin_ai_assistant/src/agent/mcp_stdio_bridge.py`:
```python
#!/usr/bin/env python3
"""Stdio-to-HTTP JSON-RPC bridge for CLI MCP clients."""

from __future__ import annotations

import os
import sys
import urllib.request


def main() -> None:
    url = os.environ["MO2_MCP_URL"]
    token = os.environ["MO2_MCP_TOKEN"]
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        req = urllib.request.Request(
            url,
            data=line.encode("utf-8"),
            headers={"Content-Type": "application/json", "X-MO2-Token": token},
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=310) as resp:
                out = resp.read().decode("utf-8")
                sys.stdout.write(out + "\n")
                sys.stdout.flush()
        except Exception as exc:
            sys.stderr.write(f"mo2-mcp-bridge error: {exc}\n")
            sys.stderr.flush()


if __name__ == "__main__":
    main()
```

Create `deps/plugin_ai_assistant/src/agent/cli_runner.py`:
```python
from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from ..config import _in_container
from .graph import build_system_prompt


def write_mcp_config_file(endpoint_url: str, token: str) -> Path:
    bridge_script = str(Path(__file__).with_name("mcp_stdio_bridge.py").resolve())
    cfg = {
        "mcpServers": {
            "mo2": {
                "command": "python3",
                "args": [bridge_script],
                "env": {
                    "MO2_MCP_URL": endpoint_url,
                    "MO2_MCP_TOKEN": token,
                },
            }
        }
    }
    fd, path = tempfile.mkstemp(prefix="mo2-mcp-", suffix=".json")
    os.chmod(path, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
    return Path(path)


def build_cli_command(
    provider: str,
    prompt: str,
    mcp_config_path: Path,
    force_host_exec: bool | None = None,
) -> list[str]:
    use_host = force_host_exec if force_host_exec is not None else (
        _in_container() and shutil.which("distrobox-host-exec") is not None
    )
    prefix = ["distrobox-host-exec"] if use_host else []

    if provider == "claude_cli":
        return prefix + [
            "claude",
            "-p",
            prompt,
            "--mcp-config",
            str(mcp_config_path),
            "--allowedTools",
            "mcp__mo2__*",
        ]
    if provider == "codex_cli":
        return prefix + ["codex", "exec", prompt]
    if provider == "agy_cli":
        return prefix + ["agy", "-p", prompt]
    raise ValueError(f"Unsupported CLI provider: {provider}")


def run_cli_turn(
    organizer,
    provider: str,
    user_message: str,
    endpoint_url: str,
    token: str,
) -> str:
    sys_context = build_system_prompt(organizer)
    full_prompt = (
        f"{sys_context}\n"
        f"An MCP server for Mod Organizer 2 is available at {endpoint_url}.\n\n"
        f"User request: {user_message}"
    )
    cfg_path = write_mcp_config_file(endpoint_url, token)
    try:
        cmd = build_cli_command(provider, full_prompt, cfg_path)
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=300,
            check=False,
        )
        if proc.returncode != 0:
            err = (proc.stderr or proc.stdout or f"CLI exited with code {proc.returncode}").strip()
            raise RuntimeError(err)
        return proc.stdout.strip() or "(Empty response from CLI)"
    finally:
        cfg_path.unlink(missing_ok=True)
```

Create `deps/plugin_ai_assistant/src/agent/worker.py`:
```python
from __future__ import annotations

from pathlib import Path
from PyQt6.QtCore import QThread, pyqtSignal
from langchain_core.messages import HumanMessage
from ..config import AssistantConfig
from ..tools.dispatcher import MainThreadDispatcher
from .llm_factory import create_chat_model
from .graph import build_agent_graph
from .cli_runner import run_cli_turn


class AgentTurnWorker(QThread):
    response_ready = pyqtSignal(str)
    error_occurred = pyqtSignal(str)

    def __init__(
        self,
        dispatcher: MainThreadDispatcher,
        config: AssistantConfig,
        user_text: str,
        thread_id: str,
        mcp_endpoint: str,
        mcp_token: str,
        sqlite_path: Path | None = None,
    ) -> None:
        super().__init__()
        self._dispatcher = dispatcher
        self._config = config
        self._user_text = user_text
        self._thread_id = thread_id
        self._mcp_endpoint = mcp_endpoint
        self._mcp_token = mcp_token
        self._sqlite_path = sqlite_path

    def run(self) -> None:
        try:
            if self._config.provider in ("claude_cli", "codex_cli", "agy_cli"):
                reply = run_cli_turn(
                    self._dispatcher._organizer,
                    self._config.provider,
                    self._user_text,
                    self._mcp_endpoint,
                    self._mcp_token,
                )
                self.response_ready.emit(reply)
                return

            model = create_chat_model(self._config)
            graph = build_agent_graph(self._dispatcher, model, sqlite_path=self._sqlite_path)
            result = graph.invoke(
                {"messages": [HumanMessage(content=self._user_text)]},
                config={"configurable": {"thread_id": self._thread_id}},
            )
            last_msg = result["messages"][-1]
            self.response_ready.emit(str(last_msg.content))
        except Exception as exc:
            self.error_occurred.emit(str(exc))
```

- [ ] **Step 4: Run test to verify it passes**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests`
Expected: PASS (16 passed).

- [ ] **Step 5: Commit**

```bash
git -C .claude/worktrees/ai-assistant add deps/plugin_ai_assistant/src/agent tests/ai-assistant/test_mcp_and_cli.py
git -C .claude/worktrees/ai-assistant commit -m "deps/plugin_ai_assistant: local MCP server, stdio bridge and CLI agent runner"
```

---

### Task 6: Interface PyQt6 (`QDockWidget`, Cards de Aprovação, Diálogo de Configurações) e Smoke Test

**Files:**
- Create: `deps/plugin_ai_assistant/src/ui/__init__.py`
- Create: `deps/plugin_ai_assistant/src/ui/approval_card.py`
- Create: `deps/plugin_ai_assistant/src/ui/settings_dialog.py`
- Create: `deps/plugin_ai_assistant/src/ui/chat_panel.py`
- Create: `deps/plugin_ai_assistant/src/ui/dock_widget.py`
- Modify: `deps/plugin_ai_assistant/src/plugin.py`
- Create: `tests/ai-assistant/test_ui_widgets.py`

**Interfaces:**
- Consumes: `AssistantConfig` (Task 1), `MainThreadDispatcher`, `build_default_registry` (Tasks 2-3), `LocalMcpServer`, `AgentTurnWorker` (Task 5).
- Produces:
  - `ai_assistant.ui.approval_card.ApprovalCard(QFrame)`
  - `ai_assistant.ui.settings_dialog.AssistantSettingsDialog(QDialog)`
  - `ai_assistant.ui.chat_panel.ChatPanel(QWidget)`
  - `ai_assistant.ui.dock_widget.AiAssistantDock(QDockWidget)`
  - Registro completo e idempotente em `AiAssistantPlugin.init()` via `organizer.onUserInterfaceInitialized()`.

- [ ] **Step 1: Write the failing tests** em `tests/ai-assistant/test_ui_widgets.py`:

```python
from __future__ import annotations

from PyQt6.QtWidgets import QMainWindow
from ai_assistant import createPlugin
from ai_assistant.ui.approval_card import ApprovalCard


def test_approval_card_emits_decisions():
    received = []
    card = ApprovalCard("req-1", "remove_mod", {"mod_name": "BadMod"}, destructive=True)
    card.decision_made.connect(lambda req_id, dec: received.append((req_id, dec)))
    card.approve_btn.click()
    assert received == [("req-1", "approve")]


def test_reinit_in_same_process_is_clean(fake_organizer):
    plugin = createPlugin()
    assert plugin.init(fake_organizer) is True
    assert hasattr(fake_organizer, "_ui_callback")

    win1 = QMainWindow()
    fake_organizer._ui_callback(win1)
    assert plugin._dock is not None
    first_port = plugin._mcp_server.port
    assert first_port > 0

    # Simulate MO2_SMOKE_RESTART_ONCE in same process with a new QMainWindow
    win2 = QMainWindow()
    fake_organizer._ui_callback(win2)
    assert plugin._dock.parentWidget() is win2
    assert plugin._mcp_server.port == first_port

    # Check quick action and mode toggle in chat panel
    panel = plugin._dock.chat_panel
    panel.mode_combo.setCurrentIndex(1)  # Autonomous
    assert plugin._dispatcher._permission_mode == "autonomous"
    plugin._mcp_server.stop()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests`
Expected: FAIL (`ModuleNotFoundError: No module named 'ai_assistant.ui'`).

- [ ] **Step 3: Implement `ui/` modules and wire `plugin.py`**

Create `deps/plugin_ai_assistant/src/ui/__init__.py`:
```python
from .dock_widget import AiAssistantDock
from .chat_panel import ChatPanel
from .approval_card import ApprovalCard
from .settings_dialog import AssistantSettingsDialog

__all__ = ["AiAssistantDock", "ChatPanel", "ApprovalCard", "AssistantSettingsDialog"]
```

Create `deps/plugin_ai_assistant/src/ui/approval_card.py`:
```python
from __future__ import annotations

import json
from PyQt6.QtCore import pyqtSignal
from PyQt6.QtWidgets import QFrame, QVBoxLayout, QHBoxLayout, QLabel, QPushButton


class ApprovalCard(QFrame):
    decision_made = pyqtSignal(str, str)

    def __init__(self, request_id: str, tool_name: str, args: dict, destructive: bool = False, parent=None) -> None:
        super().__init__(parent)
        self.request_id = request_id
        self.setFrameShape(QFrame.Shape.StyledPanel)
        border_color = "#d9534f" if destructive else "#f0ad4e"
        self.setStyleSheet(f"QFrame {{ border: 1px solid {border_color}; border-radius: 6px; padding: 6px; }}")

        layout = QVBoxLayout(self)
        title = QLabel(f"<b>{'[Destructive] ' if destructive else ''}Pending Action: {tool_name}</b>")
        args_label = QLabel(f"<code>{json.dumps(args, ensure_ascii=False)}</code>")
        args_label.setWordWrap(True)
        layout.addWidget(title)
        layout.addWidget(args_label)

        btn_row = QHBoxLayout()
        self.approve_btn = QPushButton("Approve")
        self.approve_all_btn = QPushButton("Approve All in Session")
        self.reject_btn = QPushButton("Reject")

        self.approve_btn.clicked.connect(lambda: self._decide("approve"))
        self.approve_all_btn.clicked.connect(lambda: self._decide("approve_session"))
        self.reject_btn.clicked.connect(lambda: self._decide("reject"))

        btn_row.addWidget(self.approve_btn)
        btn_row.addWidget(self.approve_all_btn)
        btn_row.addWidget(self.reject_btn)
        layout.addLayout(btn_row)

    def _decide(self, decision: str) -> None:
        self.approve_btn.setEnabled(False)
        self.approve_all_btn.setEnabled(False)
        self.reject_btn.setEnabled(False)
        self.decision_made.emit(self.request_id, decision)
```

Create `deps/plugin_ai_assistant/src/ui/settings_dialog.py`:
```python
from __future__ import annotations

from pathlib import Path
from PyQt6.QtWidgets import (
    QDialog,
    QVBoxLayout,
    QFormLayout,
    QLineEdit,
    QLabel,
    QDialogButtonBox,
)
from ..config import AssistantConfig, detect_available_clis


class AssistantSettingsDialog(QDialog):
    def __init__(self, config: AssistantConfig, data_dir: Path, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("AI Assistant Settings")
        self._config = config
        self._data_dir = data_dir

        layout = QVBoxLayout(self)
        clis = detect_available_clis()
        cli_status = " | ".join(
            f"{name}: {'Available' if ok else 'Not found'}" for name, ok in clis.items()
        )
        layout.addWidget(QLabel(f"<b>Host CLI Agents:</b> {cli_status}"))

        form = QFormLayout()
        self.model_edit = QLineEdit(self._config.model_name)
        self.ollama_url_edit = QLineEdit(self._config.ollama_base_url)
        self.openai_url_edit = QLineEdit(self._config.openai_base_url)

        self.gemini_key_edit = QLineEdit(self._config.api_keys.get("gemini", ""))
        self.gemini_key_edit.setEchoMode(QLineEdit.EchoMode.Password)

        self.openai_key_edit = QLineEdit(self._config.api_keys.get("openai", ""))
        self.openai_key_edit.setEchoMode(QLineEdit.EchoMode.Password)

        self.anthropic_key_edit = QLineEdit(self._config.api_keys.get("anthropic", ""))
        self.anthropic_key_edit.setEchoMode(QLineEdit.EchoMode.Password)

        form.addRow("Model Override (optional):", self.model_edit)
        form.addRow("Ollama Base URL:", self.ollama_url_edit)
        form.addRow("OpenAI-Compatible URL:", self.openai_url_edit)
        form.addRow("Google Gemini API Key:", self.gemini_key_edit)
        form.addRow("OpenAI / OpenRouter API Key:", self.openai_key_edit)
        form.addRow("Anthropic API Key:", self.anthropic_key_edit)
        layout.addLayout(form)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self._on_save)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def _on_save(self) -> None:
        self._config.model_name = self.model_edit.text().strip()
        self._config.ollama_base_url = self.ollama_url_edit.text().strip() or "http://localhost:11434/v1"
        self._config.openai_base_url = self.openai_url_edit.text().strip() or "https://api.openai.com/v1"
        self._config.api_keys["gemini"] = self.gemini_key_edit.text().strip()
        self._config.api_keys["openai"] = self.openai_key_edit.text().strip()
        self._config.api_keys["anthropic"] = self.anthropic_key_edit.text().strip()
        self._config.save(self._data_dir)
        self.accept()
```

Create `deps/plugin_ai_assistant/src/ui/chat_panel.py`:
```python
from __future__ import annotations

import uuid
from pathlib import Path
from PyQt6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QComboBox,
    QPushButton,
    QTextBrowser,
    QPlainTextEdit,
)
from ..config import AssistantConfig
from ..tools.dispatcher import MainThreadDispatcher
from ..agent.mcp_server import LocalMcpServer
from ..agent.worker import AgentTurnWorker
from .approval_card import ApprovalCard
from .settings_dialog import AssistantSettingsDialog

PROVIDER_LABELS = [
    ("claude_cli", "Claude Code (CLI)"),
    ("codex_cli", "Codex (CLI)"),
    ("agy_cli", "Antigravity (CLI)"),
    ("ollama", "Ollama (Local)"),
    ("gemini", "Google Gemini API"),
    ("openai", "OpenAI / Compatible API"),
    ("anthropic", "Anthropic API"),
]


class ChatPanel(QWidget):
    def __init__(
        self,
        dispatcher: MainThreadDispatcher,
        mcp_server: LocalMcpServer,
        data_dir: Path,
        parent=None,
    ) -> None:
        super().__init__(parent)
        self._dispatcher = dispatcher
        self._mcp_server = mcp_server
        self._data_dir = data_dir
        self._config = AssistantConfig.load(data_dir)
        self._thread_id = str(uuid.uuid4())
        self._worker: AgentTurnWorker | None = None

        self._dispatcher.set_permission_mode(self._config.permission_mode)
        self._dispatcher.approval_requested.connect(self._on_approval_requested)

        root = QVBoxLayout(self)

        # Top bar
        top_bar = QHBoxLayout()
        self.provider_combo = QComboBox()
        for key, label in PROVIDER_LABELS:
            self.provider_combo.addItem(label, key)
        idx = next((i for i, (k, _) in enumerate(PROVIDER_LABELS) if k == self._config.provider), 0)
        self.provider_combo.setCurrentIndex(idx)
        self.provider_combo.currentIndexChanged.connect(self._on_provider_changed)

        self.mode_combo = QComboBox()
        self.mode_combo.addItem("Safe Mode (Confirm Writes)", "safe")
        self.mode_combo.addItem("Autonomous Mode", "autonomous")
        self.mode_combo.setCurrentIndex(0 if self._config.permission_mode == "safe" else 1)
        self.mode_combo.currentIndexChanged.connect(self._on_mode_changed)

        self.clear_btn = QPushButton("New Chat")
        self.clear_btn.clicked.connect(self._on_new_chat)
        self.settings_btn = QPushButton("Settings")
        self.settings_btn.clicked.connect(self._on_open_settings)

        top_bar.addWidget(self.provider_combo, 2)
        top_bar.addWidget(self.mode_combo, 2)
        top_bar.addWidget(self.clear_btn)
        top_bar.addWidget(self.settings_btn)
        root.addLayout(top_bar)

        # Transcript area + approval container
        self.transcript = QTextBrowser()
        self.transcript.setOpenExternalLinks(True)
        root.addWidget(self.transcript, 1)

        self.cards_layout = QVBoxLayout()
        root.addLayout(self.cards_layout)

        # Quick action chips
        chips = QHBoxLayout()
        for label, prompt in [
            ("Diagnose Crash", "Read the latest crash log and identify which mod or file caused the crash."),
            ("Check Mod Conflicts", "Inspect my active mods and report any notable issues or inactive dependencies."),
            ("Review Load Order", "List my active plugin load order and check for issues."),
        ]:
            btn = QPushButton(label)
            btn.clicked.connect(lambda _=False, p=prompt: self.send_prompt(p))
            chips.addWidget(btn)
        root.addLayout(chips)

        # Input row
        input_row = QHBoxLayout()
        self.input_box = QPlainTextEdit()
        self.input_box.setPlaceholderText("Ask the AI Assistant to analyze logs, toggle mods, install archives...")
        self.input_box.setMaximumHeight(75)
        self.send_btn = QPushButton("Send")
        self.send_btn.clicked.connect(self._on_send_clicked)
        input_row.addWidget(self.input_box, 1)
        input_row.addWidget(self.send_btn)
        root.addLayout(input_row)

    def _on_provider_changed(self) -> None:
        self._config.provider = self.provider_combo.currentData()

    def _on_mode_changed(self) -> None:
        mode = self.mode_combo.currentData()
        self._config.permission_mode = mode
        self._dispatcher.set_permission_mode(mode)

    def _on_new_chat(self) -> None:
        self._thread_id = str(uuid.uuid4())
        self._dispatcher.reset_session_approvals()
        self.transcript.clear()

    def _on_open_settings(self) -> None:
        dlg = AssistantSettingsDialog(self._config, self._data_dir, self)
        dlg.exec()

    def _on_send_clicked(self) -> None:
        text = self.input_box.toPlainText().strip()
        if not text:
            return
        self.input_box.clear()
        self.send_prompt(text)

    def send_prompt(self, text: str) -> None:
        if self._worker is not None and self._worker.isRunning():
            return
        self.transcript.append(f"<p><b>You:</b> {text}</p>")
        self.send_btn.setEnabled(False)
        sqlite_path = self._data_dir / "checkpoints.sqlite"
        self._worker = AgentTurnWorker(
            dispatcher=self._dispatcher,
            config=self._config,
            user_text=text,
            thread_id=self._thread_id,
            mcp_endpoint=self._mcp_server.endpoint_url(),
            mcp_token=self._mcp_server.token,
            sqlite_path=sqlite_path,
        )
        self._worker.response_ready.connect(self._on_response_ready)
        self._worker.error_occurred.connect(self._on_error)
        self._worker.start()

    def _on_response_ready(self, reply: str) -> None:
        self.transcript.append(f"<p><b>AI Assistant:</b><br>{reply}</p>")
        self.send_btn.setEnabled(True)

    def _on_error(self, err: str) -> None:
        self.transcript.append(f"<p style='color:#d9534f;'><b>Error:</b> {err}</p>")
        self.send_btn.setEnabled(True)

    def _on_approval_requested(self, req_id: str, tool_name: str, args: dict, destructive: bool) -> None:
        card = ApprovalCard(req_id, tool_name, args, destructive=destructive, parent=self)

        def _handle_decision(r_id: str, decision: str) -> None:
            self._dispatcher.resolve_approval(r_id, decision)
            card.setParent(None)
            card.deleteLater()

        card.decision_made.connect(_handle_decision)
        self.cards_layout.addWidget(card)
```

Create `deps/plugin_ai_assistant/src/ui/dock_widget.py`:
```python
from __future__ import annotations

from pathlib import Path
from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QDockWidget
from ..tools.dispatcher import MainThreadDispatcher
from ..agent.mcp_server import LocalMcpServer
from .chat_panel import ChatPanel


class AiAssistantDock(QDockWidget):
    def __init__(
        self,
        dispatcher: MainThreadDispatcher,
        mcp_server: LocalMcpServer,
        data_dir: Path,
        parent=None,
    ) -> None:
        super().__init__("AI Assistant", parent)
        self.setObjectName("AiAssistantDockWidget")
        self.setAllowedAreas(
            Qt.DockWidgetArea.RightDockWidgetArea
            | Qt.DockWidgetArea.LeftDockWidgetArea
            | Qt.DockWidgetArea.BottomDockWidgetArea
        )
        self.chat_panel = ChatPanel(dispatcher, mcp_server, data_dir, self)
        self.setWidget(self.chat_panel)
```

Update `deps/plugin_ai_assistant/src/plugin.py`:
```python
from __future__ import annotations

from pathlib import Path
import mobase
from PyQt6.QtCore import Qt, QCoreApplication
from PyQt6.QtGui import QIcon
from .tools import build_default_registry, MainThreadDispatcher
from .agent.mcp_server import LocalMcpServer
from .ui.dock_widget import AiAssistantDock


class AiAssistantPlugin(mobase.IPluginTool):
    def __init__(self) -> None:
        super().__init__()
        self._organizer: mobase.IOrganizer | None = None
        self._registry = build_default_registry()
        self._dispatcher: MainThreadDispatcher | None = None
        self._mcp_server: LocalMcpServer | None = None
        self._dock: AiAssistantDock | None = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        organizer.onUserInterfaceInitialized(self._on_ui_initialized)
        return True

    def _on_ui_initialized(self, main_window) -> None:
        if self._organizer is None:
            return
        if self._dispatcher is None:
            self._dispatcher = MainThreadDispatcher(self._organizer, self._registry)
        if self._mcp_server is None:
            self._mcp_server = LocalMcpServer(self._dispatcher)
            self._mcp_server.start()
        data_dir = Path(self._organizer.pluginDataPath()) / "ai_assistant"
        self._dock = AiAssistantDock(self._dispatcher, self._mcp_server, data_dir, main_window)
        main_window.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, self._dock)

    def name(self) -> str:
        return "AI Assistant"

    def localizedName(self) -> str:
        return self._tr("AI Assistant")

    def author(self) -> str:
        return "ModOrganizerLinux"

    def description(self) -> str:
        return self._tr("Integrated AI agent for mod management, log analysis, and debugging.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.FINAL)

    def settings(self) -> list[mobase.PluginSetting]:
        return [
            mobase.PluginSetting("enabled", self._tr("Enable the AI Assistant dock panel"), True),
        ]

    def displayName(self) -> str:
        return self._tr("AI Assistant")

    def tooltip(self) -> str:
        return self._tr("Open or focus the AI Assistant panel")

    def icon(self) -> QIcon:
        return QIcon()

    def display(self) -> None:
        if self._dock is not None:
            self._dock.show()
            self._dock.raise_()

    def _tr(self, text: str) -> str:
        return QCoreApplication.translate("AiAssistantPlugin", text)
```

- [ ] **Step 4: Run tests and smoke test to verify everything passes**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests`
Expected: PASS (18 passed).

- [ ] **Step 5: Commit**

```bash
git -C .claude/worktrees/ai-assistant checkout -- '*.ts' 2>/dev/null || true
git -C .claude/worktrees/ai-assistant add deps/plugin_ai_assistant tests/ai-assistant
git -C .claude/worktrees/ai-assistant commit -m "deps/plugin_ai_assistant: QDockWidget chat UI, approval cards and settings dialog"
```
