# Compatibilidade com plugins Python de Windows — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** um plugin Python de Windows na pasta do usuário (o RootBuilder, sem edição) faz
build, sync e clear sem estragar a pasta do jogo.

**Architecture:** no Linux, `pluginDataPath()` passa a ser gravável
(`~/.local/share/ModOrganizer/plugins/data`). Um módulo Python, `mo2_windows_compat`,
instalado pelo runner, dá aos plugins que não vêm com o app:
- flags `O_*` do Windows;
- `os.chmod` com o significado do Windows;
- caminhos sem diferenciar maiúsculas, como o Wine;
- `\` convertido em `/`.

Quem recebe a camada é decidido pela pilha de chamadas, comparada às raízes registradas pelo
proxy.

**Tech Stack:** C++20/Qt6 (modorganizer, plugin_python), Python 3.14, pytest, gtest. Builds
e testes no distrobox `mo2-run` (`./build.sh`).

**Spec:** `docs/superpowers/specs/2026-10-27-windows-plugin-compat-design.md`

## Global Constraints

- Linux só: todo código novo de C++ fica em `#ifndef _WIN32` (ou em ramo Linux já existente). O
  Windows não muda.
- Escopo da regra das maiúsculas, do `chmod` e de `\`→`/`: só chamadas cuja pilha tem um
  quadro dentro de uma raiz Windows (plugin carregado de fora da pasta de plugins do app).
- Flags `O_BINARY`, `O_TEXT`, `O_NOINHERIT`, `O_SHORT_LIVED`, `O_TEMPORARY`, `O_RANDOM`,
  `O_SEQUENTIAL`: valor `0`, para todo o processo, só se ainda não existirem.
- `chmod`: só age quando `mode & 0o400 == 0`. Com `S_IWRITE` (`0o200`): modo atual `| 0o200`;
  sem: modo atual `& ~0o222`.
- Resolução: nome exato primeiro; senão, exatamente um item com o mesmo `casefold()`; senão,
  o resto do caminho fica como pedido.
- `pluginDataPath()` no Linux: `userPluginsPath() + "/data"`, criada se não existir.
- Comentários e mensagens no estilo do código ao redor (inglês no código, como nos ramos
  Linux existentes).
- Commits terminam com:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01SY1tkBRskRweuQe9nwH5ok
  ```
- `*_en.ts` regenerados pelo build nunca entram em commit (`git checkout -- '*.ts'`).
- Rodar comandos com `distrobox enter mo2-run -- bash -c "cd <repo> && ..."`. O smoke test
  falha se o Mod Organizer do usuário estiver aberto (`pgrep -af ModOrganizer`).

## Review Focus

- Dois nomes que só diferem em maiúsculas (`a.txt` e `A.txt`): nada é resolvido. Teste
  `test_ambiguous_names_are_not_resolved` (Task 3).
- Padrões de glob com `\` (`**\*.exe`, o modo link do RootBuilder): devem casar como `**/*.exe`.
  Teste `test_glob_backslashes` (Task 3).
- `makedirs` de `bin/x64/new` quando existe `Bin/x64`: deve criar dentro da `Bin/x64`
  existente. Teste `test_create_uses_existing_folders` (Task 3).
- Hardlink do modo link (`Path.hardlink_to`, destino com maiúsculas diferentes): o link deve
  substituir o existente na pasta certa. Teste `test_link_destination_is_resolved` (Task 3).
- `shutil.copystat(..., follow_symlinks=False)` continua funcionando depois do embrulho de
  `os.chmod`/`os.utime` (os conjuntos `os.supports_*`). Teste
  `test_supports_sets_keep_wrapped_functions` (Task 3).

---

### Task 1: `pluginDataPath()` gravável no Linux

**Files:**
- Modify: `deps/modorganizer/src/pluginpaths.h`, `deps/modorganizer/src/pluginpaths.cpp`
- Modify: `deps/modorganizer/src/organizercore.cpp:896-900`
- Test: `deps/modorganizer/tests/test_pluginpaths.cpp`

**Interfaces:**
- Produces: `QString userPluginDataPath();`, que é `userPluginsPath() + "/data"`, puro (não
  cria a pasta). `OrganizerCore::pluginDataPath()` no Linux retorna esse valor e cria a pasta
  (`QDir().mkpath`); no Windows fica como está.

- [ ] **Step 1: Write the failing test** em `test_pluginpaths.cpp`:

```cpp
TEST(PluginPaths, UserDataIsInTheUserFolder)
{
  EXPECT_EQ(userPluginDataPath(), userPluginsPath() + "/data");
}
```

- [ ] **Step 2: Run to verify it fails.** Run: `./build.sh --only modorganizer --tests`.
  Expected: erro de compilação, `userPluginDataPath` não declarado.
- [ ] **Step 3: Implement** `userPluginDataPath()` (declaração com comentário no estilo do
  header: "the data folder of the plugins on Linux, writable, beside the user's plugins") e
  o ramo `#ifndef _WIN32` em `OrganizerCore::pluginDataPath()`.
- [ ] **Step 4: Run to verify it passes.** Mesmo comando; esperado: `PluginPaths.*` passam e o
  smoke test passa.
- [ ] **Step 5: Commit** `deps/modorganizer: Linux: the plugin data folder is the user's`.

### Task 2: os dados que o app traz continuam acessíveis

**Files:**
- Modify: `deps/plugin_python/src/proxy/proxypython.cpp` (ramo Linux de `paths`, ~linha 213)
- Modify: `deps/tool_configurator/src/pyCfg.py:391` (`init`)

**Interfaces:**
- Consumes: `IOrganizer::getPluginDataPath()` (Task 1).
- Produces: `sys.path` no Linux = `[plugin_python/libs, <dados do usuário>, <bin>/plugins/data]`.

- [ ] **Step 1: Failing check.** Num script de verificação (scratchpad, não versionado):
  importar `pyCfg` do install e chamar `init()` com um organizer falso cujo
  `pluginDataPath()` é uma pasta temporária vazia. Esperado agora: `init` retorna `False`.
- [ ] **Step 2: Implement.**
  - No proxy: acrescentar `pluginFolder / "data"` (bytes UTF-8, como os outros) depois do
    caminho de dados do usuário.
  - No `pyCfg.init`: escolher a pasta de dados uma vez. No Linux
    (`sys.platform != "win32"`), se `<pluginDataPath>/settings.json` não existe, usar
    `os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")`. Usar essa mesma pasta
    para `settings.json` e para o search path `res/`.
- [ ] **Step 3: Verify.** O script do Step 1 deve retornar `True`.
  `./build.sh --only plugin_python --only tool_configurator --only preview_dds --tests`: o
  "plugin load check" de cada um passa.
- [ ] **Step 4: Commit** `deps/plugin_python, deps/tool_configurator: Linux: read the shipped
  plugin data`.

### Task 3: o módulo `mo2_windows_compat`

**Files:**
- Create: `deps/plugin_python/src/libs/mo2_windows_compat.py`
- Modify: `deps/plugin_python/src/proxy/CMakeLists.txt` (instalar ao lado do `winreg.py`)
- Test: `deps/plugin_python/tests/python/test_windows_compat.py` (e incluir na lista do
  `tests/python/CMakeLists.txt`)

**Interfaces:**
- Produces:
  - `install() -> None`: idempotente. Define os flags `O_*`; embrulha `os.stat`, `os.lstat`,
    `os.open`, `os.remove`, `os.unlink`, `os.rmdir`, `os.mkdir`, `os.listdir`, `os.scandir`,
    `os.chmod`, `os.utime`, `os.rename`, `os.replace`, `os.link`, `os.symlink`,
    `builtins.open`, `io.open`, `glob.glob` e `glob.iglob`. Cada embrulho substitui o original
    nos conjuntos `os.supports_fd`, `os.supports_dir_fd` e `os.supports_follow_symlinks` em
    que o original estava.
  - `add_root(path: str) -> None`: registra uma pasta ou um arquivo `.py` como raiz Windows
    (`os.path.normpath` do caminho absoluto).
  - `from_windows_plugin() -> bool`: algum quadro da pilha (exceto os deste módulo) tem
    `co_filename` igual a uma raiz ou dentro dela.
  - `resolve(path: str) -> str`: `\`→`/` e resolução componente a componente, pela regra das
    Global Constraints, usando só as funções originais.
- Regras dos embrulhos (spec §3.2):
  - **Leitura e remoção:** chamam o original; com `FileNotFoundError`/`NotADirectoryError` e
    `from_windows_plugin()`, tentam de novo com `resolve(path)` se mudou.
  - **Criação** (`mkdir`; `open`/`os.open` com escrita, `"x"`, `"a"` ou `O_CREAT`; destino
    de `rename`/`replace`/`link`/`symlink`): se `from_windows_plugin()`, resolvem antes.
  - **`\`→`/`:** em toda chamada que vem de plugin Windows, antes de chamar.
  - **Argumentos:** só os de tipo `str`/`PathLike` são tocados. Descritores (`int`) e `bytes`
    passam direto.

- [ ] **Step 1: Write the failing tests.** Fixture: árvore temporária com `game/Bin/Vanilla.DLL`
  e `game/Bin/x64/`; uma pasta `winplugin/` com `ops.py` registrada por `add_root`; e uma
  pasta `linuxplugin/` com uma cópia do mesmo `ops.py` sem registro. `ops.py` expõe funções
  que fazem cada operação, para que o chamador seja o arquivo do "plugin". O módulo é
  importado do `src/libs` (PYTHONPATH do teste já tem essa pasta). Testes:
  - `test_flags_exist`: `os.O_BINARY == 0` e `os.O_TEXT == 0`.
  - `test_read_ignores_case`: `win.read("game/bin/vanilla.dll") == "vanilla"`;
    `linux.read(...)` gera `FileNotFoundError`.
  - `test_remove_ignores_case`: `win.remove("game/bin/vanilla.dll")` apaga `Bin/Vanilla.DLL`.
  - `test_exists_and_pathlib`: `win.exists("game/BIN/VANILLA.dll") is True`;
    `win.path_exists(...)` (via `Path.exists`) `is True`; `linux.exists(...) is False`.
  - `test_create_uses_existing_folders`: `win.makedirs("game/bin/x64/new")` cria
    `game/Bin/x64/new` e não cria `game/bin`.
  - `test_overwrite_with_other_case`: `win.write("game/bin/vanilla.dll", "mod")` muda
    `Bin/Vanilla.DLL` e não cria outro arquivo.
  - `test_link_destination_is_resolved`: `win.hardlink(src, "game/bin/vanilla.dll")`, depois
    de `win.remove` do mesmo destino, cria o link em `Bin/Vanilla.DLL` (mesmo inode que `src`).
  - `test_chmod_windows_meaning`: `win.chmod(f, stat.S_IWRITE)` deixa o modo `0o644 | 0o200`
    (legível); `win.chmod(f, stat.S_IREAD)` pede um modo legível e é aplicado como no Linux
    (`0o400`); `linux.chmod(f, stat.S_IWRITE)` deixa `0o200`.
  - `test_backslashes`: `win.read("game\\Bin\\Vanilla.DLL") == "vanilla"`.
  - `test_glob_backslashes`: `win.glob(str(game) + "/**\\*.DLL", recursive=True)` contém
    `Bin/Vanilla.DLL`.
  - `test_ambiguous_names_are_not_resolved`: com `game/a.txt` e `game/A.txt`,
    `win.read("game/a.TXT")` gera `FileNotFoundError`.
  - `test_new_file_keeps_its_name`: `win.write("game/New.TXT", "x")`, quando não existe
    nada parecido, cria `New.TXT`.
  - `test_supports_sets_keep_wrapped_functions`: depois de `install()`, se o `os.chmod`
    original estava em `os.supports_follow_symlinks`, o atual também está, e
    `shutil.copystat(a, b, follow_symlinks=False)` não gera erro.
  - `test_install_is_idempotent`: `install()` duas vezes e `os.stat` ainda funciona; os
    embrulhos não se acumulam (chamar `install()` de novo não muda `os.stat`).
- [ ] **Step 2: Run to verify they fail.** Run (no container):
  `PYTHONPATH=deps/plugin_python/src/libs python3 -m pytest deps/plugin_python/tests/python/test_windows_compat.py -p no:cacheprovider --noconftest -q`.
  Esperado: falha ao importar `mo2_windows_compat`.
- [ ] **Step 3: Implement** o módulo pelas Interfaces acima, com o cabeçalho de docstring no
  estilo do `winreg.py`. Guarde os originais num dict privado, para o `install()` idempotente
  e para o `resolve()` não recursivo. A verificação da pilha só roda nos dois pontos das regras
  (falha de leitura; criação), nunca no caminho normal de leitura.
- [ ] **Step 4: Run to verify they pass.** Mesmo comando: todos passam.
  `./build.sh --only plugin_python --tests`: pytest e testes C++ passam, e o
  `mo2_windows_compat.py` é instalado em `install/bin/plugins/plugin_python/libs/`.
- [ ] **Step 5: Commit** `deps/plugin_python: Linux: compatibility layer for Windows Python
  plugins`.

### Task 4: o runner instala a camada e registra os plugins de fora do app

**Files:**
- Modify: `deps/plugin_python/src/runner/pythonrunner.h` e `pythonrunner.cpp`
- Modify: `deps/plugin_python/src/proxy/proxypython.h` e `proxypython.cpp`
- Modify: `tests/smoke/modorganizer-smoke.sh`

**Interfaces:**
- Consumes: `mo2_windows_compat.install()` e `add_root(path)` (Task 3).
- Produces:
  - `IPythonRunner::addWindowsPluginRoot(const QString& identifier)` (virtual,
    `#ifndef _WIN32`): importa `mo2_windows_compat` e chama `add_root`.
  - `PythonRunner::initialize` no Linux chama `mo2_windows_compat.install()` logo depois de
    importar o `mobase`. Se der erro, registra `log::error` e continua: os plugins do app não
    dependem da camada.
  - `ProxyPython` guarda a pasta de plugins do app (`m_PluginFolder`, de `init`). Em `load()`,
    no Linux, se o `identifier` não está dentro dela, chama
    `m_Runner->addWindowsPluginRoot(identifier)` antes de `m_Runner->load`.

- [ ] **Step 1: Failing check no smoke test.** Em `modorganizer-smoke.sh`, no "start 1" (HOME
  novo), criar um plugin de sonda em
  `$home/.local/share/ModOrganizer/plugins/compat_probe/__init__.py`. Ao ser importado, ele:
  - cria `$HOME/probe/Bin/Vanilla.DLL`;
  - grava em `$HOME/probe.txt` o resultado de
    `os.path.exists(os.path.join(os.environ["HOME"], "probe/bin/vanilla.dll"))`;
  - define `createPlugins()` retornando `[]`.

  Depois da execução, o smoke falha com "the compatibility layer was not applied" se
  `probe.txt` não contiver `True`. Isso exige guardar o arquivo antes do `rm -rf "$home"`.
  Run: `./build.sh --only modorganizer --tests`. Esperado: o smoke falha com essa mensagem.
- [ ] **Step 2: Implement** as Interfaces acima.
- [ ] **Step 3: Verify.** `./build.sh --only plugin_python --only modorganizer --tests`: o smoke
  passa e o log não tem erro do `mo2_windows_compat`.
- [ ] **Step 4: Commit** `deps/plugin_python: Linux: apply the compatibility layer to the
  user's plugins`.

### Task 5: verificação com o RootBuilder, documentação e build completo

**Files:**
- Modify: `deps/README.md` (entradas do `plugin_python` e do `tool_configurator`)
- Modify: `docs/user/*/` (seção de plugins do usuário: localize com
  `grep -rln "plugins" docs/user`)
- Modify: `STATE.md` (pendência do `pluginDataPath` resolvida)

- [ ] **Step 1: Verify with the real RootBuilder.** Use o harness desta sessão
  (`/tmp/claude-1000/.../scratchpad/rbtest/harness.py`; se não existir mais, recrie pelo spec
  §4), acrescentando antes do `import` do RootBuilder:
  `import mo2_windows_compat; mo2_windows_compat.install(); mo2_windows_compat.add_root(<pkg>/rootbuilder)`.
  Para `RB_HASH=1` e `RB_HASH=0`, esperado:
  - depois do build, a pasta do jogo tem os arquivos do mod e o `BuildData.json` foi salvo;
  - depois do sync, `NewFile.TXT` editado voltou ao mod e `made_by_game.log` está em
    `overwrite/Root`;
  - depois do clear, a árvore do jogo (nomes, conteúdo e modos) é igual à de antes do build,
    sem `THREAD ERRORS`.
- [ ] **Step 2: Docs.** No `deps/README.md`, descrever a camada (o que cobre, quem a recebe,
  o limite de código nativo) e o fallback do `settings.json`. Na documentação do usuário,
  descrever a pasta `plugins/data` e o que funciona ou não em plugins de Windows. No
  `STATE.md`, marcar como resolvida a pendência do `pluginDataPath`.
- [ ] **Step 3: Full build.** `MO2_VERSION=2.5.2.4 ./build.sh --tests` e
  `build/appimage/build-appimage.sh $PWD/install $PWD/appimage-out`. Esperado: tudo passa;
  `check-appdir` passa; o AppDir contém
  `usr/bin/plugins/plugin_python/libs/mo2_windows_compat.py`.
- [ ] **Step 4: Commit** `docs: compatibility with Windows Python plugins`.
