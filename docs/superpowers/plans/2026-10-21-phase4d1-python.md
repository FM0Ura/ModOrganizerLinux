# Fase 4d.1 — Plugins Python (runtime e plugins pequenos) — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** O MO2 do Linux carrega plugins Python como no Windows: `plugin_python` (Python 3.14 do sistema embutido, `mobase`, PyQt6) com o `sys.path` isolado, e os seis plugins Python pequenos do MO2 2.5 (preview_dds, form43_checker, script_extender_plugin_checker, tool_configurator, installer_wizard, fnistool).

**Architecture:** Os repositórios do upstream entram como `git subtree` em `deps/`, com patches Linux no superprojeto. O `cmake_common` ganha ramos Linux para o Python (versão do sistema, PyQt6 e `pyuic6`/`pylupdate6` do sistema). No `plugin_python`, o proxy acha a própria pasta por `dladdr` e abre o `libpython` com `RTLD_GLOBAL`; o runner usa a configuração isolada do Python e filtra o `sys.path` (função pura testável). O `build.sh` ganha um alvo por repositório e o smoke confere os plugins Python carregados.

**Tech Stack:** C++20/Qt 6, pybind11, Python 3.14 (Fedora), PyQt6 (Fedora), CMake (`cmake_common`), gtest, pytest, bash.

**Spec:** `docs/superpowers/specs/2026-10-21-phase4d1-python-design.md` (ler inteira antes de cada tarefa; §N são seções dela).

## Global Constraints

- Worktree `../ModOrganizerLinux-4d1` (`/var/home/fmoura-server/Documentos/Applications/ModOrganizerLinux-4d1`), branch `fase-4d1-python` (a partir de `fase-4c3-ferramentas`; a spec já está nele). **Nunca** compilar, instalar ou rodar testes no checkout principal (`../ModOrganizerLinux`) nem nos worktrees da 4c (`-4c1`, `-4c2`, `-4c3`): o MO2 do usuário roda do `install/` do checkout principal.
- Python 3.14, pybind11 e PyQt6 **do Fedora** (abordagem A, spec §1.2). Nunca `pip install` do PyQt6 nem do Qt; o `pip` só instala dependências próprias de plugin (`bain-wizard-interpreter==1.0.4`) na pasta do plugin.
- Isolamento (spec §1.2, §3.2): configuração isolada (`PyConfig_InitIsolatedConfig`) e `sys.path` filtrado: só a biblioteca padrão do Python do sistema, `<base_prefix>/lib64/python3.X/site-packages`, `<base_prefix>/lib/python3.X/site-packages` e as pastas do MO2. No Linux o runner nunca lê `PYTHONPATH`.
- Layout instalado (spec §3.4): `bin/plugins/plugin_python.so` (proxy), `bin/plugins/plugin_python/dlls/` (runner), `bin/plugins/plugin_python/libs/` (`mobase*.so`); o PyQt6 não é copiado. Plugins Python: `bin/plugins/<arquivo>.py` (simples) ou `bin/plugins/<nome>/` (módulo); pastas extras dos simples em `bin/plugins/data/`.
- Importação: `git subtree add --prefix deps/<nome> https://github.com/ModOrganizer2/modorganizer-<repo> master --squash` (como na 4c.1); registre o hash do `git-subtree-split:` em `deps/README.md` (tabela existente).
- Commits: o `git subtree add` gera os seus; patches dentro de `deps/<nome>` em commits próprios `deps/<nome>: Linux: …`, sem misturar arquivos de fora; `build: …` (build.sh, Containerfile, workflows), `tests: …` (tests/ do superprojeto), `docs: …`. Todo commit termina com:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL
  ```
- Código Windows idêntico ou em `#ifdef _WIN32` (C++) / `if sys.platform == "win32":` (Python). O CI do Windows só compila `deps/modorganizer`, então o código Windows destes subtrees não é compilado no CI: não mude nada fora de ramos Linux.
- Mensagens de log em inglês; comentários no estilo do arquivo; `clang-format` (`.clang-format` do subtree) só nas linhas alteradas. `*_en.ts` regenerados pelo build não são commitados (`git checkout -- '*_en.ts'`).
- Nada de push nem PR sem autorização explícita do usuário.

## Ambiente de execução

Referência: o CI (container Fedora 44 de `build/Containerfile`). No Bazzite, compile na distrobox `mo2-run`, da raiz do worktree:

```bash
cd /var/home/fmoura-server/Documentos/Applications/ModOrganizerLinux-4d1
mkdir -p /tmp/mo2t
distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --tests
```

**Pré-requisito (feito pelo controlador antes da Task 1, com o OK do usuário):** os pacotes novos no box `mo2-run`: `distrobox enter mo2-run -- sudo dnf install -y pybind11-devel python3-pyqt6 python3-pyqt6-devel python3-pip python3-pytest`.

O worktree não tem `build-out/`: o primeiro `./build.sh --tests` é o build completo (longo; rode em segundo plano). Depois: `./build.sh --only <alvo> --tests`. Nunca `pkill -f`/`pgrep -f` com padrão da própria linha de comando.

## Review Focus

- Pacote Python instalado pelo usuário em `~/.local/lib/python3.14/site-packages` (o `$HOME` da distrobox é o do host) ou apontado por `PYTHONPATH`: nunca importável pelos plugins (teste de integração da Task 3).
- Box sem os pacotes novos (Python/PyQt6): o MO2 abre, o proxy registra a falha com a lista de pacotes a instalar, nenhum crash (Task 3, mensagem; verificado no teste manual).
- Plugin Python que lança exceção ao carregar: os outros continuam carregando e o log diz qual falhou (comportamento do upstream; o smoke da Task 5 exige zero falhas com os seis).
- Arquivo `.dds` aberto na pré-visualização numa sessão Wayland: abre pelo `preview_dds` (o `preview_base` sempre pula `.dds`, por desenho do upstream) — teste manual.
- `fnistool` com o caminho do mod de saída: o `RedirectFiles` precisa ir em formato Windows (`Z:\…`) para o FNIS sob Proton (Task 4).

---

### Task 1: Container, box e `cmake_common` com o Python do sistema

**Files:**
- Modify: `build/Containerfile` (pacotes)
- Modify: `deps/cmake_common/mo2_versions.cmake:34`
- Modify: `deps/cmake_common/mo2_python.cmake` (`mo2_python_install_pyqt`, `mo2_python_uifiles`)
- Modify: `deps/cmake_common/mo2_utils.cmake:~365-375` (`pylupdate`)
- Modify: `tests/cmake-smoke/` (caso Python novo) e `build.sh` (`build_cmake_smoke`)

**Interfaces:**
- Produces: no Linux, `MO2_PYTHON_VERSION` = `<major>.<minor>` do `python3` do sistema; alvo `PyQt6` existe (vazio) para `add_dependencies`; `mo2_python_uifiles()` gera `.py` com `pyuic6` do `PATH`; `mo2_add_translations` usa `pylupdate6` do `PATH` para fontes Python. Usado pelas Tasks 2 e 4.

- [ ] **Step 1: Containerfile**

Em `build/Containerfile`, na lista do `dnf -y install`, depois de `python3 python3-devel`, acrescente `pybind11-devel python3-pyqt6 python3-pyqt6-devel python3-pip python3-pytest` (mesma linha ou linha nova com a mesma indentação e `\`).

- [ ] **Step 2: Escrever o teste que falha (cmake-smoke)**

Crie `tests/cmake-smoke/python/form.ui`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>Form</class>
 <widget class="QWidget" name="Form">
  <widget class="QLabel" name="label">
   <property name="text">
    <string>ok</string>
   </property>
  </widget>
 </widget>
 <resources/>
 <connections/>
</ui>
```

No `tests/cmake-smoke/CMakeLists.txt`, depois do que já existe, acrescente:

```cmake
# Python (Phase 4d.1): on Linux, cmake_common uses the system's Python and pyuic6
if (NOT WIN32)
	include(mo2_python OPTIONAL RESULT_VARIABLE _mo2_python)
	add_custom_target(smoke_python ALL)
	mo2_python_uifiles(smoke_python FILES ${CMAKE_CURRENT_SOURCE_DIR}/python/form.ui)
	find_package(Python REQUIRED COMPONENTS Interpreter)
	if (NOT MO2_PYTHON_VERSION STREQUAL "${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}")
		message(FATAL_ERROR "MO2_PYTHON_VERSION is ${MO2_PYTHON_VERSION}, the system has "
			"${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}")
	endif()
endif()
```

(Se o `CMakeLists.txt` do cmake-smoke já tiver `find_package(mo2-cmake CONFIG REQUIRED)`, as funções `mo2_*` já estão disponíveis e a linha `include(mo2_python …)` pode ser omitida; confira no arquivo.)

No `build.sh`, em `build_cmake_smoke()`, depois da checagem de `smoke_main`, acrescente:

```bash
    [[ -f "$BUILD_OUT/cmake-smoke/form.py" ]] ||
        die "cmake-smoke: pyuic6 did not generate form.py"
    grep -q "class Ui_Form" "$BUILD_OUT/cmake-smoke/form.py" ||
        die "cmake-smoke: form.py is not a pyuic6 output"
    log "cmake-smoke: python ok"
```

- [ ] **Step 3: Rodar e ver falhar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only cmake_common --only cmake-smoke --tests` (o primeiro build do worktree; se faltar `uibase` etc. para o cmake-smoke, rode antes o build completo em segundo plano).
Expected: FAIL — `find_package(Python 3.13 EXACT …)` não acha o 3.14, ou o comando `…/pylibs/bin/pyuic6.exe` não existe.

- [ ] **Step 4: Implementar os ramos Linux no `cmake_common`**

`deps/cmake_common/mo2_versions.cmake`, troque a linha `mo2_set_if_not_defined(MO2_PYTHON_VERSION "3.13")` por:

```cmake
if (WIN32)
	mo2_set_if_not_defined(MO2_PYTHON_VERSION "3.13")
else()
	# Linux: the Python of the system (Fedora), with the PyQt6 built against the same Qt
	# as Mod Organizer (spec of Phase 4d.1, section 1.2)
	if (NOT DEFINED MO2_PYTHON_VERSION)
		find_package(Python REQUIRED COMPONENTS Interpreter)
		set(MO2_PYTHON_VERSION "${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}")
	endif()
endif()
```

`deps/cmake_common/mo2_python.cmake`, `mo2_python_install_pyqt()`: depois de `add_custom_target(PyQt6)` / `set_target_properties(...)`, envolva o `mo2_python_pip_install(...)` em `if (WIN32)` … `endif()` com o comentário `# Linux: the PyQt6 of the system, never from pip (it brings a Qt of its own)`.

`mo2_python_uifiles()`: troque o `COMMAND` do `add_custom_command` por um comando escolhido antes do `foreach`:

```cmake
	if (WIN32)
		set(_pyuic ${CMAKE_COMMAND} -E env PYTHONPATH=${CMAKE_BINARY_DIR}/pylibs
			${MO2_PYLIBS_DIR}/bin/pyuic${MO2_QT_VERSION_MAJOR}.exe)
	else()
		find_program(MO2_PYUIC pyuic${MO2_QT_VERSION_MAJOR} REQUIRED)
		set(_pyuic ${MO2_PYUIC})
	endif()
```

e no `add_custom_command`: `COMMAND ${_pyuic} -o "${output}" "${UI_FILE}"`.

`deps/cmake_common/mo2_utils.cmake`, no ponto que chama `${MO2_PYLIBS_DIR}/bin/pylupdate${MO2_QT_VERSION_MAJOR}.exe` (≈ linha 370): mesmo padrão — `if (WIN32)` mantém o comando atual (`-E env PYTHONPATH=… …pylupdate6.exe`), `else()` usa `find_program(MO2_PYLUPDATE pylupdate${MO2_QT_VERSION_MAJOR} REQUIRED)` e o comando sem o `env`.

- [ ] **Step 5: Rodar e ver passar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only cmake_common --only cmake-smoke --tests`
Expected: `cmake-smoke: ok` e `cmake-smoke: python ok`.

- [ ] **Step 6: Commits**

```bash
git add build/Containerfile
git commit -m "build: Python, pybind11, PyQt6, pip and pytest in the container" -m "<attribution>"
git add deps/cmake_common
git commit -m "deps/cmake_common: Linux: the Python, PyQt6 and pyuic6 of the system" -m "<attribution>"
git add tests/cmake-smoke build.sh
git commit -m "tests: cmake-smoke: pyuic6 and the Python version on Linux" -m "<attribution>"
```

(`<attribution>` = as duas linhas das Global Constraints.)

---

### Task 2: `plugin_python` importado e compilando no Linux

**Files:**
- Create (subtree): `deps/plugin_python/`
- Modify: `deps/plugin_python/src/proxy/CMakeLists.txt`, `deps/plugin_python/src/mobase/wrappers/known_folders.h`, e o que a compilação exigir dentro de `deps/plugin_python/src/` (regras no Step 4)
- Modify: `build.sh` (alvo `plugin_python`), `deps/README.md`
- Test: `tests/python-import/check_mobase.py` (novo)

**Interfaces:**
- Consumes: Task 1 (`MO2_PYTHON_VERSION` do sistema, `PyQt6` alvo).
- Produces: instalados `bin/plugins/plugin_python.so`, `bin/plugins/plugin_python/dlls/librunner.so`, `bin/plugins/plugin_python/libs/mobase.cpython-314-x86_64-linux-gnu.so`; função de build `build_plugin_python` no `build.sh`.

- [ ] **Step 1: Importar**

```bash
git subtree add --prefix deps/plugin_python https://github.com/ModOrganizer2/modorganizer-plugin_python master --squash
```

Registre o hash do `git-subtree-split:` na tabela de `deps/README.md`.

- [ ] **Step 2: Teste que falha (o `mobase` importa num Python do sistema)**

`tests/python-import/check_mobase.py`:

```python
# Imports the installed mobase module with the system's Python (Phase 4d.1): mobase is a
# pybind11 extension that needs uibase and Qt; PyQt6 must be the system's.
#   python3 -I check_mobase.py <install prefix>
import pathlib
import sys

install = pathlib.Path(sys.argv[1])
sys.path.insert(0, str(install / "bin" / "plugins" / "plugin_python" / "libs"))

import PyQt6.QtCore  # noqa: E402  (the system's PyQt6, loaded before mobase)
import mobase  # noqa: E402

assert hasattr(mobase, "IPlugin"), "mobase has no IPlugin"
assert hasattr(mobase, "getKnownFolder"), "mobase has no getKnownFolder"
assert hasattr(mobase.KnownFolder, "Documents"), "KnownFolder has no Documents"
print("mobase ok")
```

No `build.sh`, no array `ALL_TARGETS`, depois de `tool_inibakery`, acrescente `plugin_python`, e defina (antes de `# --- driver`):

```bash
# plugin_python (Phase 4d.1): the Python proxy (bin/plugins/plugin_python.so), its runner
# and the mobase module (bin/plugins/plugin_python/), with the system's Python 3.14 and
# PyQt6; with --tests its GoogleTest and pytest suites run, the proxy must load and
# mobase must import in the system's Python
build_plugin_python() {
    cmake_build plugin_python "$ROOT/deps/plugin_python"
    check_plugins plugin "$INSTALL/bin/plugins/plugin_python.so"
    if [[ $TESTS == 1 ]]; then
        LD_LIBRARY_PATH="$INSTALL/lib" python3 -I "$ROOT/tests/python-import/check_mobase.py" \
            "$INSTALL" || die "mobase does not import"
    fi
}
```

(Confira em `cmake_build` como os testes de um alvo rodam: se ele já chama `ctest` com `--tests`, não duplique. Se `check_plugins plugin` não aceitar um `IPluginProxy`, use o tipo que o `tests/plugin-load` tiver para proxies ou acrescente `proxy` a ele, seguindo o padrão dos tipos existentes.)

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only plugin_python --tests`
Expected: FAIL (configuração ou compilação: `Windows.h`, `/DELAYLOAD`, `KnownFolders.h`, `Python_HOME/dlls`…).

- [ ] **Step 3: CMake do proxy no Linux**

`deps/plugin_python/src/proxy/CMakeLists.txt`: mantenha o que é comum (o alvo, `mo2_configure_plugin`, `target_link_libraries`, `OUTPUT_NAME`, `mo2_install_plugin`, as traduções) e coloque o resto em ramos:

```cmake
set(PLUGIN_PYTHON_DIR bin/plugins/${PLUGIN_NAME})
install(FILES $<TARGET_FILE:runner> DESTINATION ${PLUGIN_PYTHON_DIR}/dlls)
install(TARGETS mobase DESTINATION ${PLUGIN_PYTHON_DIR}/libs)

if (WIN32)
	target_link_options(proxy PRIVATE "/DELAYLOAD:runner.dll")
	# ... (o bloco atual inteiro: DLLs do Python, .pyd, pythoncore.zip, PyQt6) ...
else()
	# Linux: the runner is found next to the proxy, in plugin_python/dlls; Python and
	# PyQt6 are the system's (spec of Phase 4d.1, section 3), nothing else is installed
	set_target_properties(proxy PROPERTIES
		INSTALL_RPATH "$ORIGIN/plugin_python/dlls"
		BUILD_WITH_INSTALL_RPATH OFF)
	target_compile_definitions(proxy PRIVATE
		MO2_PYTHON_LIBRARY="$<TARGET_LINKER_FILE_NAME:Python::Python>")
endif()
```

(O `install(TARGETS mobase …)` e o `install(FILES $<TARGET_FILE:runner> …)` saem dos blocos Windows e ficam comuns; no Windows o resultado é o mesmo. Se `Python::Python` não existir como alvo neste ponto, use `Python_LIBRARIES`/`Python_LIBRARY` com `get_filename_component(... NAME)` e o soname real — no Fedora `libpython3.14.so.1.0`; confira com `ls /usr/lib64/libpython3.14*`. O `dlopen` da Task 3 precisa do nome do soname, não do link `.so`.) O runner (`src/runner/CMakeLists.txt`) também precisa de `INSTALL_RPATH` para achar o `uibase` e o Qt do `install/lib` (padrão dos outros alvos: confira `mo2_configure_target`/`mo2_install_plugin` do `cmake_common`, que já definem o RPATH dos plugins; aplique o mesmo ao runner e ao `mobase`).

- [ ] **Step 4: Compilar no Linux**

`deps/plugin_python/src/mobase/wrappers/known_folders.h`: o arquivo inteiro passa a:

```cpp
#ifdef _WIN32
#include <KnownFolders.h>
#endif

namespace mo2::python {

    struct KnownFolder {
        const char* name;
        KNOWNFOLDERID guid;
    };

#ifdef _WIN32
    // ... (a lista atual de 142 entradas, sem mudança) ...
#else
    // the folders that getKnownFolder() of uibase supports on Linux (win32compat.h)
    const std::array<KnownFolder, 10> KNOWN_FOLDERS{{
        {"Desktop", FOLDERID_Desktop},
        {"Documents", FOLDERID_Documents},
        {"Downloads", FOLDERID_Downloads},
        {"LocalAppData", FOLDERID_LocalAppData},
        {"OneDrive", FOLDERID_OneDrive},
        {"ProgramFiles", FOLDERID_ProgramFiles},
        {"ProgramFilesX86", FOLDERID_ProgramFilesX86},
        {"RoamingAppData", FOLDERID_RoamingAppData},
        {"StartMenu", FOLDERID_StartMenu},
        {"Windows", FOLDERID_Windows},
    }};
#endif

}  // namespace mo2::python
```

Depois, compile e corrija cada erro restante seguindo estas regras (e só elas):
- `#include <Windows.h>` → `#ifdef _WIN32` em volta (o `uibase` traz `win32compat.h` para `HANDLE`, `DWORD`, `INVALID_HANDLE_VALUE` no Linux).
- Código do proxy que usa Win32 (`getPluginFolder`, `SetDllDirectoryW`, `windowsErrorString`): coloque o corpo Windows atual em `#ifdef _WIN32` e, no `#else`, o mínimo para compilar **sem comportamento novo** (a Task 3 implementa o Linux): `getPluginFolder()` devolve `{}` com o comentário `// Linux: see Task 3 of the Phase 4d.1 plan`, e as chamadas a `SetDllDirectoryW` ficam só no ramo Windows. Nenhum `TODO` no código: o comentário diz o que o Linux faz hoje.
- Avisos de compilação novos tratados como no resto do subtree (sem silenciar com pragmas).

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only plugin_python`
Expected: compila e instala; `ls install/bin/plugins/plugin_python.so install/bin/plugins/plugin_python/dlls install/bin/plugins/plugin_python/libs` mostra os três.

- [ ] **Step 5: Rodar os testes desta tarefa**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only plugin_python --tests`
Expected: `plugin load check (plugin)` passa e `mobase ok`. Os testes do upstream (`tests/`) só entram na Task 3: se o `BUILD_TESTING` deles quebrar a configuração aqui, desligue-os nesta tarefa passando `-DBUILD_TESTING=OFF` **só para o `plugin_python`** em `cmake_build` e anote no commit que a Task 3 religa.

- [ ] **Step 6: Commits**

`deps/plugin_python: Linux: build the proxy, the runner and mobase` (arquivos dentro de `deps/plugin_python`); `build: plugin_python target` (build.sh, deps/README.md); `tests: mobase imports in the system's Python` (tests/python-import).

---

### Task 3: Python iniciado no Linux, isolado

**Files:**
- Create: `deps/plugin_python/src/runner/syspath.h`, `deps/plugin_python/src/runner/syspath.cpp`
- Modify: `deps/plugin_python/src/runner/pythonrunner.cpp`, `deps/plugin_python/src/runner/CMakeLists.txt`
- Modify: `deps/plugin_python/src/proxy/proxypython.cpp`
- Modify: `deps/plugin_python/tests/` (CMake Linux; helper de caminhos; testes novos)
- Modify: `build.sh` (religar os testes se a Task 2 os desligou)

**Interfaces:**
- Consumes: Task 2 (`MO2_PYTHON_LIBRARY` definido no proxy; layout instalado).
- Produces:
  ```cpp
  namespace mo2::python {
  // the entries of sys.path under one of the allowed roots, in their order, then the
  // extra paths (the folders of Mod Organizer); a root matches itself and what is under it
  std::vector<std::string> filterSysPath(const std::vector<std::string>& sysPath,
                                         const std::vector<std::string>& allowedRoots,
                                         const std::vector<std::string>& extraPaths);
  }
  ```

- [ ] **Step 1: Teste unitário do filtro (falha)**

Crie `deps/plugin_python/tests/runner/test_syspath.cpp` e inclua-o na lista de fontes do `runner-tests` (`tests/runner/CMakeLists.txt`):

```cpp
#include <gtest/gtest.h>

#include "syspath.h"

using mo2::python::filterSysPath;

namespace {
const std::vector<std::string> roots{
    "/usr/lib64/python3.14", "/usr/lib64/python3.14/site-packages",
    "/usr/lib/python3.14/site-packages"};
}

TEST(SysPath, KeepsTheSystemAndAddsModOrganizer)
{
  const std::vector<std::string> sysPath{
      "/usr/lib64/python314.zip",
      "/usr/lib64/python3.14",
      "/usr/lib64/python3.14/lib-dynload",
      "/home/u/.local/lib/python3.14/site-packages",
      "/usr/local/lib64/python3.14/site-packages",
      "/usr/local/lib/python3.14/site-packages",
      "/usr/lib64/python3.14/site-packages",
      "/usr/lib/python3.14/site-packages",
      "/home/u/pythonpath"};

  EXPECT_EQ(filterSysPath(sysPath, roots, {"/mo2/plugins/plugin_python/libs", "/mo2/plugins/data"}),
            (std::vector<std::string>{
                "/usr/lib64/python3.14", "/usr/lib64/python3.14/lib-dynload",
                "/usr/lib64/python3.14/site-packages", "/usr/lib/python3.14/site-packages",
                "/mo2/plugins/plugin_python/libs", "/mo2/plugins/data"}));
}

TEST(SysPath, APrefixOfARootIsNotUnderIt)
{
  EXPECT_TRUE(filterSysPath({"/usr/lib64/python3.14-extra"}, roots, {}).empty());
  EXPECT_TRUE(filterSysPath({"/usr/lib64"}, roots, {}).empty());
}

TEST(SysPath, NoDuplicateOfAnExtraPath)
{
  EXPECT_EQ(filterSysPath({"/usr/lib64/python3.14"}, roots,
                          {"/mo2/libs", "/mo2/libs"}),
            (std::vector<std::string>{"/usr/lib64/python3.14", "/mo2/libs"}));
}

TEST(SysPath, TrailingSlashOfARoot)
{
  EXPECT_EQ(filterSysPath({"/usr/lib/python3.14/site-packages/foo"},
                          {"/usr/lib/python3.14/site-packages/"}, {}),
            (std::vector<std::string>{"/usr/lib/python3.14/site-packages/foo"}));
}
```

Run (depois de religar os testes, Step 5): ver falhar por `syspath.h` inexistente.

- [ ] **Step 2: Implementar `filterSysPath`**

`deps/plugin_python/src/runner/syspath.h`:

```cpp
#ifndef PYTHONRUNNER_SYSPATH_H
#define PYTHONRUNNER_SYSPATH_H

#include <string>
#include <vector>

namespace mo2::python {

    // the entries of sys.path under one of the allowed roots, in their order, then the
    // extra paths (the folders of Mod Organizer) not already there; a root matches
    // itself and what is under it (Linux: the Python of Mod Organizer only sees the
    // system's standard library and packages, never the user's, spec of Phase 4d.1)
    std::vector<std::string> filterSysPath(const std::vector<std::string>& sysPath,
                                           const std::vector<std::string>& allowedRoots,
                                           const std::vector<std::string>& extraPaths);

}  // namespace mo2::python

#endif
```

`deps/plugin_python/src/runner/syspath.cpp`:

```cpp
#include "syspath.h"

#include <algorithm>

namespace mo2::python {

    namespace {
        bool isUnder(const std::string& path, std::string root)
        {
            while (root.size() > 1 && root.back() == '/') {
                root.pop_back();
            }
            return path == root ||
                   (path.size() > root.size() && path.compare(0, root.size(), root) == 0 &&
                    path[root.size()] == '/');
        }
    }  // namespace

    std::vector<std::string> filterSysPath(const std::vector<std::string>& sysPath,
                                           const std::vector<std::string>& allowedRoots,
                                           const std::vector<std::string>& extraPaths)
    {
        std::vector<std::string> result;
        for (const auto& path : sysPath) {
            if (std::any_of(allowedRoots.begin(), allowedRoots.end(),
                            [&](const auto& root) {
                                return isUnder(path, root);
                            })) {
                result.push_back(path);
            }
        }
        for (const auto& path : extraPaths) {
            if (std::find(result.begin(), result.end(), path) == result.end()) {
                result.push_back(path);
            }
        }
        return result;
    }

}  // namespace mo2::python
```

Acrescente `syspath.h` e `syspath.cpp` ao `add_library(runner SHARED …)` de `src/runner/CMakeLists.txt` (compila nas duas plataformas; só o Linux usa).

- [ ] **Step 3: Runner no Linux**

Em `pythonrunner.cpp`, `PythonRunner::initialize(pythonPaths)`:
- O bloco que lê `PYTHONPATH` quando `paths` está vazio fica em `#ifdef _WIN32`.
- `config.module_search_paths_set`/`PyWideStringList_Append` ficam em `#ifdef _WIN32` (no Linux o Python calcula a biblioteca padrão do sistema).
- Logo depois de `py::initialize_interpreter(...)` e da checagem de `Py_IsInitialized()`, no `#ifndef _WIN32`:

```cpp
#ifndef _WIN32
            {
                // Linux: only the system's standard library and packages, never the
                // user's (~/.local, /usr/local), then the folders of Mod Organizer
                // (spec of Phase 4d.1, section 3.2)
                py::module_ sys       = py::module_::import("sys");
                py::module_ sysconfig = py::module_::import("sysconfig");
                const auto version    = "python" + py::str(sys.attr("version_info").attr("major")).cast<std::string>() +
                                     "." + py::str(sys.attr("version_info").attr("minor")).cast<std::string>();
                const auto base = sys.attr("base_prefix").cast<std::string>();
                const std::vector<std::string> roots{
                    sysconfig.attr("get_path")("stdlib").cast<std::string>(),
                    sysconfig.attr("get_path")("platstdlib").cast<std::string>(),
                    base + "/lib64/" + version + "/site-packages",
                    base + "/lib/" + version + "/site-packages"};
                std::vector<std::string> extra;
                for (const auto& path : paths) {
                    extra.push_back(absolute(path).string());
                }
                sys.attr("path") = py::cast(
                    filterSysPath(sys.attr("path").cast<std::vector<std::string>>(), roots, extra));
            }
#endif
```

(`#include "syspath.h"` e `#include <pybind11/stl.h>` se o arquivo ainda não tiver; formate com o `.clang-format` do subtree.)

- [ ] **Step 4: Proxy no Linux**

Em `proxypython.cpp`, no `#else` (Linux) da Task 2:
- `getPluginFolder()`:

```cpp
#else
// the folder of the proxy (plugins/), from the address of this function
fs::path getPluginFolder()
{
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&getPluginFolder), &info) == 0 ||
        info.dli_fname == nullptr) {
        return {};
    }
    return fs::path(info.dli_fname).parent_path();
}
#endif
```

(`#include <dlfcn.h>` no `#else` dos includes.)
- Em `init()`, no lugar do passo do `SetDllDirectoryW` (que fica no `#ifdef _WIN32`), o ramo Linux:

```cpp
#else
    // the extension modules of the system's Python (PyQt6, mobase) are not linked to
    // libpython and need its symbols in the global scope, but Mod Organizer loads
    // plugins with RTLD_LOCAL (spec of Phase 4d.1, section 3.1)
    if (dlopen(MO2_PYTHON_LIBRARY, RTLD_NOW | RTLD_GLOBAL) == nullptr) {
        m_LoadFailure = FailureType::DLL_NOT_FOUND;
        log::error("failed to load {}: {}", MO2_PYTHON_LIBRARY, dlerror());
        return true;
    }
#endif
```

- A lista de caminhos passada a `m_Runner->initialize(...)`: no Linux `{pluginFolder / "plugin_python" / "libs", IOrganizer::getPluginDataPath()}` (sem `pythoncore.zip`); no Windows a atual. Confira o valor real de `pluginFolder` no Windows (a pasta do proxy é `plugins/`, e `libs` é `plugins/plugin_python/libs`? leia `getPluginFolder` e o `PLUGIN_PYTHON_DIR` do CMake: use no Linux o mesmo diretório em que a Task 2 instalou o `mobase`).
- `m_Runner->addDllSearchPath(...)`: só no Windows.
- O `SEMICOLON` (`applicationDirPath().contains(';')`) só no Windows.
- As mensagens dos tipos de falha (`FailureType::DLL_NOT_FOUND` etc., em `shortDescription`/`fullDescription` ou equivalentes do arquivo): no Linux, `DLL_NOT_FOUND` diz: `Python 3.14 or PyQt6 was not found. In the mo2-run distrobox, install them with: sudo dnf install python3 python3-pyqt6` (com a versão de `MO2_PYTHON_VERSION`, passada ao C++ por `target_compile_definitions(proxy PRIVATE MO2_PYTHON_VERSION_STRING="${MO2_PYTHON_VERSION}")` no ramo Linux do CMake). Ponha o texto em `tr()` como os demais.

- [ ] **Step 5: Testes do upstream no Linux e integração do isolamento**

`deps/plugin_python/tests/runner/CMakeLists.txt` e `tests/python/CMakeLists.txt`: no Linux, sem `mo2_python_pip_install` (pytest e PyQt6 do sistema) — envolva as chamadas em `if (WIN32)`; o `add_test(NAME pytest …)` no Linux usa `COMMAND ${Python_EXECUTABLE} -m pytest ${CMAKE_CURRENT_SOURCE_DIR} -s` e o `PYTHONPATH` com `:` (`$<TARGET_FILE_DIR:mobase>:${CMAKE_CURRENT_BINARY_DIR}/pylibs`), `UIBASE_PATH` igual. No `runner-tests`, o ambiente passa `PYTHONPATH` com `:`.

Como no Linux o runner não lê mais `PYTHONPATH`, os testes do runner passam os caminhos explicitamente: crie `deps/plugin_python/tests/mocks/TestPythonPaths.h`:

```cpp
#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// the module paths of the tests (mobase, the test plugins), from PYTHONPATH as the test
// environment sets it; the runner itself never reads PYTHONPATH on Linux
inline std::vector<std::filesystem::path> testPythonPaths()
{
    std::vector<std::filesystem::path> paths;
    const char* env = std::getenv("PYTHONPATH");
    if (env == nullptr) {
        return paths;
    }
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::string value(env);
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(separator, start);
        const auto item = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!item.empty()) {
            paths.emplace_back(item);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return paths;
}
```

e troque, nos `tests/runner/test_*.cpp`, `runner->initialize()` por `runner->initialize(testPythonPaths())` (com `#include "TestPythonPaths.h"`). No Windows o efeito é o mesmo de antes (o runner lia o mesmo `PYTHONPATH`).

Teste de integração do isolamento, `deps/plugin_python/tests/runner/test_isolation.cpp` (na lista do `runner-tests`, só no Linux: `if (NOT WIN32)` no CMake):

```cpp
#include <gtest/gtest.h>

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <QTemporaryDir>

#include <cstdlib>
#include <fstream>

#include "TestPythonPaths.h"
#include "pythonrunner.h"

namespace py = pybind11;

namespace {
void makePackage(const std::filesystem::path& dir, const std::string& name)
{
    std::filesystem::create_directories(dir / name);
    std::ofstream(dir / name / "__init__.py") << "VALUE = 1\n";
}
}  // namespace

// a package of the user (~/.local) or of PYTHONPATH is never visible to the plugins
TEST(Isolation, UserPackagesAreInvisible)
{
    QTemporaryDir home;
    ASSERT_TRUE(home.isValid());
    const std::filesystem::path root = home.path().toStdString();
    makePackage(root / (".local/lib/python" MO2_PYTHON_VERSION_STRING "/site-packages"),
                "mo2_leak_user");
    makePackage(root / "pythonpath", "mo2_leak_env");

    const auto paths = testPythonPaths();
    setenv("HOME", root.c_str(), 1);
    setenv("PYTHONPATH", (root / "pythonpath").c_str(), 1);

    auto runner = mo2::python::createPythonRunner();
    ASSERT_TRUE(runner->initialize(paths));

    py::gil_scoped_acquire gil;
    py::module_ util = py::module_::import("importlib.util");
    EXPECT_TRUE(util.attr("find_spec")("mo2_leak_user").is_none());
    EXPECT_TRUE(util.attr("find_spec")("mo2_leak_env").is_none());
    for (const auto& entry : py::module_::import("sys").attr("path").cast<std::vector<std::string>>()) {
        EXPECT_EQ(entry.find(root.string()), std::string::npos) << entry;
        EXPECT_NE(entry.rfind("/usr/local", 0), 0u) << entry;
    }
    EXPECT_FALSE(util.attr("find_spec")("mobase").is_none());
}
```

(`MO2_PYTHON_VERSION_STRING` vem do CMake: `target_compile_definitions(<executável do teste> PRIVATE MO2_PYTHON_VERSION_STRING="${MO2_PYTHON_VERSION}")`. Se o runner já tiver sido iniciado por outro teste no mesmo processo, este teste precisa de processo próprio: registre-o como executável separado `runner-isolation-tests` com o mesmo CMake do `runner-tests`.)

Religue os testes (`BUILD_TESTING`) do `plugin_python` no `build.sh` se a Task 2 os desligou. Confira que o `ctest` do alvo roda `runner-tests`, `pytest` e o de isolamento (os alvos `EXCLUDE_FROM_ALL` do upstream precisam ser construídos: siga o que o `tests/CMakeLists.txt` do upstream faz para o `ctest` encontrá-los, ou acrescente-os a um alvo `ALL` no ramo Linux).

- [ ] **Step 6: Rodar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only plugin_python --tests`
Expected: `SysPath.*` (4), os testes do runner do upstream, `Isolation.UserPackagesAreInvisible` e o `pytest` passam; `mobase ok`; plugin load check passa. Falhas dos testes do upstream que venham do Python 3.14 (e não do Linux) são corrigidas no código do `plugin_python` com o menor patch possível, em commit próprio, dizendo no corpo qual teste falhava e por quê.

- [ ] **Step 7: Commits**

`deps/plugin_python: Linux: the system's Python, isolated` (runner, syspath, proxy, CMake do runner/proxy); `deps/plugin_python: Linux: run the upstream tests and the isolation test` (tests/); `build: …` se o `build.sh` mudou.

---

### Task 4: Os seis plugins Python

**Files:**
- Create (subtrees): `deps/preview_dds`, `deps/form43_checker`, `deps/script_extender_plugin_checker`, `deps/tool_configurator`, `deps/installer_wizard`, `deps/fnistool`
- Modify: `build.sh` (seis alvos), `deps/README.md`
- Modify (se a revisão do Step 3 achar): arquivos `.py` desses subtrees

**Interfaces:**
- Consumes: Task 1 (`mo2_configure_python`, `pyuic6`), Task 2/3 (`plugin_python` instalado).
- Produces: instalados `bin/plugins/DDSPreview.py` (+ `bin/plugins/data/DDS/`), `bin/plugins/Form43Checker.py`, `bin/plugins/ScriptExtenderPluginChecker.py`, `bin/plugins/pyCfg.py` (+ o que o upstream instalar), `bin/plugins/installer_wizard/` (com `lib/` do `pip`), `bin/plugins/FNISTool.py`, `FNISPatches.py`, `FNISToolReset.py`. A Task 5 confere estes nomes no log; confirme os nomes reais olhando o `install/bin/plugins` depois do build e use-os no relatório.

- [ ] **Step 1: Importar**

```bash
for r in preview_dds form43_checker script_extender_plugin_checker tool_configurator installer_wizard fnistool; do
  git subtree add --prefix deps/$r https://github.com/ModOrganizer2/modorganizer-$r master --squash
done
```

Registre os seis hashes em `deps/README.md`.

- [ ] **Step 2: Alvos no `build.sh` (teste que falha)**

Em `ALL_TARGETS`, depois de `plugin_python`: `preview_dds form43_checker script_extender_plugin_checker tool_configurator installer_wizard fnistool`. Defina:

```bash
# the Python plugins of Mod Organizer 2.5 (Phase 4d.1), installed in bin/plugins: the DDS
# preview, the Form 43 and script extender plugin checkers, the INI configurator, the
# installer of BAIN archives with wizard scripts and the FNIS tool; with --tests each
# must compile in the system's Python (python3 -m py_compile) after installation
python_plugin() {
    local target="$1"; shift
    cmake_build "$target" "$ROOT/deps/$target"
    [[ $TESTS == 1 ]] || return 0
    local f
    for f in "$@"; do
        [[ -e $INSTALL/bin/plugins/$f ]] || die "$target: $f is not installed"
        if [[ -d $INSTALL/bin/plugins/$f ]]; then
            python3 -I -m compileall -q "$INSTALL/bin/plugins/$f" >/dev/null ||
                die "$target: $f does not compile"
        else
            python3 -I -m py_compile "$INSTALL/bin/plugins/$f" ||
                die "$target: $f does not compile"
        fi
    done
}
build_preview_dds() { python_plugin preview_dds DDSPreview.py; }
build_form43_checker() { python_plugin form43_checker Form43Checker.py; }
build_script_extender_plugin_checker() {
    python_plugin script_extender_plugin_checker ScriptExtenderPluginChecker.py
}
build_tool_configurator() { python_plugin tool_configurator pyCfg.py; }
build_installer_wizard() { python_plugin installer_wizard installer_wizard; }
build_fnistool() { python_plugin fnistool FNISTool.py FNISPatches.py FNISToolReset.py; }
```

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only preview_dds --only form43_checker --only script_extender_plugin_checker --only tool_configurator --only installer_wizard --only fnistool --tests`
Expected: falha onde o `cmake_common`/plugin ainda tiver algo do Windows (ex.: `.exe`), ou nome instalado diferente do esperado — ajuste a lista de arquivos ao que o upstream realmente instala (é a lista que a Task 5 vai usar), nunca o contrário.

- [ ] **Step 3: Revisão Linux de cada plugin**

Para cada um dos seis, procure e corrija só o que impede o funcionamento no Linux, em `if sys.platform == "win32":` / `else:`:

```bash
grep -rn -E 'winreg|startfile|windll|ctypes\.wintypes|\\\\|\.exe["'"'"']|os\.sep|\bcmd\b|subprocess' deps/<plugin>/src --include=*.py
```

Pontos conhecidos:
- `fnistool` (`FNISTool.py`): o executável `GenerateFNISforUsers.exe` roda por `self.__organizer.startApplication(...)` — mantenha (no Linux vai por Proton com o VFS). O argumento `RedirectFiles="<caminho do mod>"` vai para um programa Windows: no Linux, converta o caminho para o formato do Wine antes de montar o argumento — `"Z:" + path.replace("/", "\\")` — numa função pequena `_windowsPath(path)` com `if sys.platform == "win32": return path`.
- Caminhos montados com `"\\"` ou `os.sep` para arquivos do jogo: troque por `os.path.join`/`pathlib` quando o caminho é do Linux; mantenha `\` quando o texto vai para um programa Windows.
- Nomes de arquivo comparados com o disco sem ignorar maiúsculas (ex.: `plugins.txt`, `Data`): use a API do `mobase` (`organizer.resolvePath`, `IFileTree`) quando o plugin já a usa; senão, documente no relatório (não reescreva a lógica do plugin).

Se um plugin não precisar de patch, registre isso no relatório da tarefa.

- [ ] **Step 4: Rodar**

Run: o comando do Step 2.
Expected: os seis alvos instalam e compilam (`py_compile`/`compileall`); o `installer_wizard/lib` tem `bain_wizard_interpreter` (ou o nome do pacote instalado pelo `pip`).

- [ ] **Step 5: Commits**

Um commit por plugin com patch: `deps/<plugin>: Linux: …`; `build: the Python plugins of Mod Organizer 2.5` (build.sh, deps/README.md).

---

### Task 5: Smoke com os plugins Python e fixture BAIN

**Files:**
- Modify: `tests/smoke/modorganizer-smoke.sh`
- Create: `tests/fixtures/bain-wizard/` (fonte de um arquivo BAIN com `wizard.txt`)

**Interfaces:**
- Consumes: Tasks 2–4 instaladas.

- [ ] **Step 1: Smoke (teste que falha antes da Task 3 estar instalada; agora deve passar)**

Em `tests/smoke/modorganizer-smoke.sh`, no trecho que confere os `.so` carregados (≈ linhas 200–212), depois do laço dos `.so`:

```bash
    # the Python plugins (Phase 4d.1): Python started, and every .py plugin of
    # bin/plugins (and every plugin package with an __init__.py) logged as loaded through
    # the Python proxy ("loaded plugin '<name>@<version>' from '<file>'")
    if [[ -e "$INSTALL/bin/plugins/plugin_python.so" ]]; then
        ! grep -q "failed to init python" "$log" || fail "Python did not start"
        local py missing_py=()
        for py in "$INSTALL"/bin/plugins/*.py "$INSTALL"/bin/plugins/*/__init__.py; do
            [[ -e $py ]] || continue
            [[ $py == */plugin_python/* || $py == */data/* ]] && continue
            local name="${py%/__init__.py}"
            grep -qF "from '$name" "$log" || missing_py+=("${name##*/}")
        done
        ((${#missing_py[@]} == 0)) ||
            fail "Python plugins not loaded by the application: ${missing_py[*]}"
        echo "all Python plugins loaded"
    fi
```

(Adapte `$log` ao nome da variável que o script usa para o log do MO2 nesse trecho, e o formato exato do caminho logado: leia a linha `loaded plugin` que o `PluginContainer` imprime para um plugin com proxy, `plugincontainer.cpp:817`, e o caminho que o proxy passa — pode ser o `.py` ou a pasta; o grep deve aceitar o que o MO2 realmente imprime, conferido num run.)

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --tests` (completo, em segundo plano)
Expected: `all Python plugins loaded` e o smoke passa; se algum plugin falhar, a causa é corrigida no plugin (Task 4) ou no `plugin_python` (Task 3), não no teste.

- [ ] **Step 2: Fixture BAIN com wizard**

`tests/fixtures/bain-wizard/` (o checklist manual compacta com `7z a bain-wizard.7z ./*` de dentro dela):

- `wizard.txt`:
```
; a minimal BAIN wizard script for the manual test of Phase 4d.1
SelectOne "Choose the option", \
    "Option A", "Installs the A file", "", \
    "Option B", "Installs the B file", ""
    Case "Option A"
        SelectSubPackage "10 Option A"
        Break
    Case "Option B"
        SelectSubPackage "20 Option B"
        Break
EndSelect
SelectSubPackage "00 Core"
```
- `00 Core/core.txt` com `core`; `10 Option A/a.txt` com `a`; `20 Option B/b.txt` com `b`.

- [ ] **Step 3: Commit**

`tests: smoke: the Python plugins load; BAIN wizard fixture`.

---

### Task 6: Documentação, checklist e ponto de controle de CI

**Files:**
- Modify: `README.md`, `ROADMAP.md`, `STATE.md`
- Create: `docs/testing/phase4d1-manual-checklist.md`
- Modify: `docs/superpowers/specs/2026-10-21-phase4d1-python-design.md` (§4, uma frase)

- [ ] **Step 1: README**

Na seção "Rodando no Bazzite (pt-BR)", acrescente uma subseção:

```markdown
### Plugins Python

O MO2 usa o Python 3.14 e o PyQt6 do Fedora do box `mo2-run`. Num box criado antes da Fase 4d.1, instale os pacotes uma vez:

    distrobox enter mo2-run -- sudo dnf install -y python3 python3-pyqt6

O Python do MO2 é isolado: não lê `PYTHONPATH` nem as variáveis `PYTHON*`, e não enxerga pacotes do `~/.local` nem de `/usr/local`; só a biblioteca padrão e os pacotes do sistema do box, mais as pastas do próprio MO2. Plugins Python incluídos: pré-visualização de DDS, verificador do formato 43, verificador de plugins do script extender, Configurator (INIs), instalador de BAIN com wizard e FNIS. O `basic_games` vem na Fase 4d.2.
```

(e, na seção "Building", acrescente os pacotes de build `pybind11-devel python3-pyqt6-devel python3-pip python3-pytest` onde a lista de pacotes do box local estiver documentada, se estiver.)

- [ ] **Step 2: Spec**

Em `docs/superpowers/specs/2026-10-21-phase4d1-python-design.md` §4, troque "Com ele instalado, o `preview_base` deixa de pular `.dds` (a pendência registrada no STATE.md da 4c.1)." por "O `preview_base` sempre pula `.dds` (por desenho do upstream, para o plugin de DDS tratar): com o `preview_dds` instalado, `.dds` passa a ter pré-visualização (a pendência registrada no STATE.md da 4c.1)."

- [ ] **Step 3: Checklist manual**

`docs/testing/phase4d1-manual-checklist.md`:

```markdown
# Fase 4d.1 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-21-phase4d1-python-design.md`](../superpowers/specs/2026-10-21-phase4d1-python-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores. Pacotes do box: `python3 python3-pyqt6` (README, "Plugins Python").

**Build:** o `install/` do worktree `ModOrganizerLinux-4d1` (`./build.sh` na distrobox `mo2-run`) ou, depois de um run Linux verde do PR, o artefato `mo2-linux-install` desse run (restaure a permissão de execução de `bin/ModOrganizer` e `bin/mo2-vfs`).

## 1. Python carregado

- [ ] Abrir o MO2. Esperado: nenhum aviso do "Python Proxy" no ícone de notificações; em **Configurações → Plugins**, o "Python Proxy" e os plugins Python (DDS Preview, Form 43 Checker, Script Extender Plugin Checker, Configurator, Wizard Installer, FNIS Tool/Patches/Reset) aparecem habilitados.
- [ ] No log (`logs/mo_interface.log`): nenhuma linha `failed to init python`.

## 2. Pré-visualização de DDS

- [ ] Na aba **Data**, abrir a pré-visualização de um `.dds` do mod `Mod2_Textures` (duplo clique ou botão direito → Preview). Esperado: a textura aparece (canais, zoom), sem erro de OpenGL.

## 3. Verificadores

- [ ] Com um plugin `.esp` de formato 43 (Oldrim) ativo, se houver: o ícone de notificações mostra o aviso do Form 43 Checker. Sem um, conferir só que não há erro do plugin no log.
- [ ] Script Extender Plugin Checker: depois de rodar o SKSE uma vez (Fase 3), sem erro no log; com uma DLL do SKSE que não carregou (opcional), o aviso aparece.

## 4. Configurator

- [ ] **Ferramentas → Configurator** (ou o nome que o menu mostrar). Esperado: abre com os INIs do perfil (`Skyrim.ini`, `SkyrimPrefs.ini`); alterar um valor, salvar, e o INI do perfil muda.

## 5. Instalador BAIN com wizard

- [ ] Compactar a fixture: `cd tests/fixtures/bain-wizard && 7z a /tmp/bain-wizard.7z ./*`. No MO2, **Instalar mod** com `/tmp/bain-wizard.7z`. Esperado: o diálogo do wizard pergunta "Choose the option"; escolher "Option B"; o mod instalado tem `core.txt` e `b.txt` (não `a.txt`).

## 6. Isolamento

- [ ] No host, `pip install --user requests` (ou qualquer pacote ausente do box); abrir o MO2. Esperado: nada muda (o MO2 não usa o pacote). Opcional: um plugin de teste com `import requests` falha com `ModuleNotFoundError` no log. Desinstale depois (`pip uninstall requests`).

## 7. FNIS (opcional)

- [ ] Com o FNIS instalado como mod: **FNIS Tool** no menu de ferramentas pede a pasta de saída (um mod), roda o `GenerateFNISforUsers.exe` por Proton com o VFS, e a saída vai para o mod escolhido.

## 8. Regressão

- [ ] SKSE inicia o jogo com os mods (Fase 3); LOOT ordena (4b); pré-visualização de `.txt`/imagens (4c.1).
```

- [ ] **Step 4: ROADMAP e STATE**

`ROADMAP.md`: a linha 4d da tabela vira duas, 4d.1 (`Plugins Python — runtime`, entrega `plugin_python` + seis plugins, `🟡 Em andamento`) e 4d.2 (`basic_games`, `⚪ Não iniciada`); em "Fase 4 — Ecossistema", a linha `Fase 4d.1 — Spec: … · Plano: … · Checklist manual: …` no formato das outras.

`STATE.md` (formato das seções da 4c.3): cabeçalho com a fase atual 4d.1 e o branch `fase-4d1-python` (a partir de `fase-4c3-ferramentas`; worktree `../ModOrganizerLinux-4d1`); `## Fase 4d.1 — progresso` com a tabela das tarefas 1–7 e os commits; `## Limitações conhecidas da Fase 4d.1` (plugins Python só no box com os pacotes do Fedora; AppImage na Fase 5; `KnownFolder` com 10 pastas no Linux; o código Windows dos subtrees não é compilado no CI); decisões de 2026-10-21 (divisão 4d.1/4d.2; abordagem A; isolamento com `sys.path` filtrado); a linha do STATE que diz que a pré-visualização de DDS espera a 4d passa a ~~riscada~~ com "resolvido na Fase 4d.1 (`preview_dds`)".

- [ ] **Step 5: Build completo e commits**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --tests` — verde.

```bash
git add README.md ROADMAP.md STATE.md docs/testing/phase4d1-manual-checklist.md docs/superpowers/specs/2026-10-21-phase4d1-python-design.md
git commit -m "docs: Phase 4d.1: Python plugins in the README, manual checklist, roadmap and state" -m "<attribution>"
git commit --allow-empty -m "ci: Phase 4d.1 checkpoint [windows-ci]" -m "<attribution>"
```

O workflow Linux usa o `build/Containerfile`: confira em `.github/workflows/linux.yml` se a imagem é reconstruída a cada run (se ela vier de um cache/registro, a imagem precisa ser reconstruída com os pacotes novos — siga o que o workflow faz e anote no relatório).

---

### Task 7: Teste manual com o usuário e fechamento

- [ ] Com o push autorizado e os CIs verdes, o usuário roda `docs/testing/phase4d1-manual-checklist.md` com o `install/` do worktree (MO2 pelo box). Falhas viram correções com teste antes de concluir.
- [ ] Atualizar `STATE.md` e `ROADMAP.md` com o resultado.
