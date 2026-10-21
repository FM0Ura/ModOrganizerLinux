# Fase 4c.3 — Ferramentas pelo VFS — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Programas dentro de `mods/` rodam do lugar correspondente no `Data` virtual também no Linux (como o `adjustForVirtualized()` do Windows), e o xEdit dos outros jogos Bethesda da Steam acha INIs e `plugins.txt` no prefixo do seu jogo (coberto por fixtures).

**Architecture:** A regra dos caminhos (`mods/<mod>/resto` → `<data>/resto`) vira uma função pura comum às duas plataformas (`virtualizedpaths.h/.cpp`); o Windows continua montando o seu `launch` com ela, e o `ProcessRunner::run()` do Linux a usa para trocar o binário e a pasta de trabalho do lançamento antes do `spawn`, sem relançar o MO2, só com o VFS e só quando o mod do binário está ativo. No `game_bethesda`, um teste parametrizado carrega dez plugins de jogo pelo `QPluginLoader` numa Steam falsa e confere as pastas no prefixo de cada appid. O resto é documentação.

**Tech Stack:** C++20/Qt 6, gtest (`organizer_tests`, `game_bethesda_tests`), bash (`tests/vfs-e2e`), Markdown.

**Spec:** `docs/superpowers/specs/2026-10-21-phase4c3-ferramentas-design.md` (ler inteira antes de cada tarefa; §N são seções dela).

## Global Constraints

- Worktree `../ModOrganizerLinux-4c3` (`/var/home/fmoura-server/Documentos/Applications/ModOrganizerLinux-4c3`), branch `fase-4c3-ferramentas` (a partir de `fase-4c2-pendencias`; a spec já está nele). **Nunca** compilar, instalar ou rodar testes no checkout principal (`../ModOrganizerLinux`) nem no worktree da 4c.2: o MO2 do usuário roda do `install/` do checkout principal.
- Commits: mudanças dentro de `deps/modorganizer` (código e `tests/` dele) em commits `deps/modorganizer: Linux: …` (a refatoração comum da Task 1 também: ela existe por causa do Linux); dentro de `deps/game_bethesda` em `deps/game_bethesda: Linux: …`; `tests/` do superprojeto em `tests: …`; documentação em `docs: …`. Sem misturar arquivos de fora do subtree num commit `deps/…`. Todo commit termina com as linhas de atribuição da sessão:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL
  ```
- Mensagens de log em inglês; comentários no estilo do arquivo (frases em minúsculas, explicando o porquê); `clang-format` de `deps/modorganizer/.clang-format` só nas linhas alteradas.
- O comportamento do Windows não muda (spec §1.2): `adjustForVirtualized()` monta o mesmo `launch "<cwd>" "<binário>" <argumentos>` a partir dos mesmos caminhos.
- Texto exato do aviso de mod desativado (spec §2.2): `'<mod>' is not active; '<binário>' runs from the mods directory`; do log de debug: `virtualized: '<real>' runs as '<no Data>' in '<cwd>'`.
- Arquivos `*_en.ts` regenerados pelo build não são commitados: `git checkout -- '*_en.ts'` antes de cada commit se aparecerem no `git status`.
- Nada de push nem PR sem autorização explícita do usuário.

## Ambiente de execução

O CI (GitHub Actions, container Fedora 44 de `build/Containerfile`) é a referência. No Bazzite, compile na distrobox `mo2-run`, a partir da raiz do worktree:

```bash
cd /var/home/fmoura-server/Documentos/Applications/ModOrganizerLinux-4c3
mkdir -p /tmp/mo2t
distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --tests
```

O worktree ainda não tem `build-out/`: o primeiro `./build.sh --tests` é o build completo, longo (rode em segundo plano e espere terminar). O `install/` do worktree tem hoje uma cópia do install da 4c.2 usada no spike; a instância portátil do spike já foi tirada de lá (`.superpowers/spike-4c3/portable-instance`), e **não** pode voltar para `install/bin` (o `portable.txt` forçaria o modo portátil nos testes e2e). O `TMPDIR` curto e próprio evita a memória compartilhada de instância única que o MO2 aberto do usuário mantém em `/tmp`.

Depois do primeiro build:
- só o app: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only modorganizer --tests`;
- um teste unitário do app: `distrobox enter mo2-run -- bash -c 'cd build-out/modorganizer && cmake --build . --target organizer_tests && TMPDIR=/tmp/mo2t ctest --output-on-failure -R "<regex>"'`;
- o `game_bethesda`: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only game_bethesda --tests`;
- o e2e do VFS (depois do app instalado em `install/`): `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t MO2_REQUIRE_FUSE=1 tests/vfs-e2e/run.sh install`.

Nunca `pkill -f`/`pgrep -f` com padrão que aparece na própria linha de comando.

## Review Focus

- Nome de mod com espaços e subpastas (`mods/Nemesis Unlimited Behavior Engine/Nemesis_Engine/x.exe`): o caminho no `Data` mantém os espaços e o nome do mod sai inteiro (teste na Task 1).
- Executável sem pasta de trabalho configurada (o caso do Nemesis): a pasta de trabalho do lançamento é a pasta do binário **reescrito**, nunca a do binário real (cenário `tool-exe` da Task 2).
- `mods/` com barra final ou com outra caixa, e um prefixo parecido (`mods2/`): só `mods/` conta (testes na Task 1).
- Execução sem VFS (`m_sp.hooked` falso) e documentos abertos pela aplicação associada: nada é reescrito (o código da Task 2 só reescreve dentro de `if (m_sp.hooked)`; o revisor confere).
- `run -p <outro perfil>`: o mod precisa estar ativo no perfil **da execução** (`m_profileName`), não no perfil aberto (o código da Task 2 resolve o nome do perfil antes de checar; o revisor confere).

---

### Task 1: Regra dos caminhos comum (`virtualizedPaths`) e o Windows usando-a

**Files:**
- Create: `deps/modorganizer/src/virtualizedpaths.h`
- Create: `deps/modorganizer/src/virtualizedpaths.cpp`
- Modify: `deps/modorganizer/src/CMakeLists.txt` (lista `_organizer_launch_sources`)
- Modify: `deps/modorganizer/src/processrunner.cpp:13-54` (`adjustForVirtualized()`)
- Test: `deps/modorganizer/tests/test_virtualizedpaths.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct VirtualizedPaths { QString binary; QString workingDirectory; QString mod; };
  std::optional<VirtualizedPaths> virtualizedPaths(const QString& modsPath,
      const QString& dataPath, const QString& binary, const QString& workingDirectory);
  ```
  (`#include "virtualizedpaths.h"`; usado pela Task 2).

- [ ] **Step 1: Escrever os testes que falham**

`deps/modorganizer/tests/test_virtualizedpaths.cpp` (o `mo2_configure_tests` pega todo `.cpp` da pasta, não é preciso registrá-lo):

```cpp
#include <gtest/gtest.h>

#include <QString>

#include <ostream>

#include "virtualizedpaths.h"

// readable QString values in the failure messages
QT_BEGIN_NAMESPACE
inline void PrintTo(const QString& s, std::ostream* os)
{
  *os << '"' << s.toStdString() << '"';
}
QT_END_NAMESPACE

namespace
{
const QString mods = "/home/u/.local/share/ModOrganizer/SSE/mods";
const QString data = "/home/u/Steam/steamapps/common/Skyrim Special Edition/Data";
}  // namespace

TEST(VirtualizedPaths, BinaryAndDirectoryInAModGoToTheDataDirectory)
{
  const auto p = virtualizedPaths(mods, data, mods + "/FNIS/tools/GenerateFNIS.exe",
                                  mods + "/FNIS/tools");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->binary, data + "/tools/GenerateFNIS.exe");
  EXPECT_EQ(p->workingDirectory, data + "/tools");
  EXPECT_EQ(p->mod, "FNIS");
}

TEST(VirtualizedPaths, KeepsSpacesOfTheModAndOfItsFolders)
{
  const QString mod = "Nemesis Unlimited Behavior Engine";
  const auto p      = virtualizedPaths(
      mods, data, mods + "/" + mod + "/Nemesis_Engine/Nemesis Unlimited Behavior Engine.exe",
      mods + "/" + mod + "/Nemesis_Engine");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->binary, data + "/Nemesis_Engine/Nemesis Unlimited Behavior Engine.exe");
  EXPECT_EQ(p->workingDirectory, data + "/Nemesis_Engine");
  EXPECT_EQ(p->mod, mod);
}

TEST(VirtualizedPaths, OnlyTheBinaryInAMod)
{
  const auto p = virtualizedPaths(mods, data, mods + "/Tool/tool.exe", "/home/u/work");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->binary, data + "/tool.exe");
  EXPECT_EQ(p->workingDirectory, "/home/u/work");
  EXPECT_EQ(p->mod, "Tool");
}

TEST(VirtualizedPaths, OnlyTheDirectoryInAMod)
{
  const auto p =
      virtualizedPaths(mods, data, "/usr/bin/python3", mods + "/Scripts/run");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->binary, "/usr/bin/python3");
  EXPECT_EQ(p->workingDirectory, data + "/run");
  EXPECT_TRUE(p->mod.isEmpty());
}

TEST(VirtualizedPaths, TheFolderOfAModIsTheDataDirectory)
{
  const auto p = virtualizedPaths(mods, data, mods + "/Tool/tool.exe", mods + "/Tool");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->workingDirectory, data);
}

TEST(VirtualizedPaths, NothingInTheModsDirectory)
{
  EXPECT_FALSE(virtualizedPaths(mods, data, "/home/u/SSEEdit/SSEEdit.exe", "/home/u/SSEEdit")
                   .has_value());
  EXPECT_FALSE(virtualizedPaths(mods, data, "/home/u/SSEEdit/SSEEdit.exe", "").has_value());
}

TEST(VirtualizedPaths, ModsDirectoryWithATrailingSlash)
{
  const auto p = virtualizedPaths(mods + "/", data, mods + "/Tool/tool.exe", "");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->binary, data + "/tool.exe");
  EXPECT_TRUE(p->workingDirectory.isEmpty());
}

// like the comparison of Windows, where it comes from
TEST(VirtualizedPaths, ModsDirectoryIgnoringCase)
{
  QString other = mods;
  other.replace("/mods", "/MODS");
  const auto p = virtualizedPaths(mods, data, other + "/Tool/tool.exe", "");
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->binary, data + "/tool.exe");
}

TEST(VirtualizedPaths, AnotherDirectoryStartingLikeMods)
{
  EXPECT_FALSE(
      virtualizedPaths(mods, data, mods + "2/Tool/tool.exe", mods + "2/Tool").has_value());
  EXPECT_FALSE(virtualizedPaths(mods, data, mods, "").has_value());
}
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --tests` (primeiro build do worktree; depois, `--only modorganizer --tests`).
Expected: FAIL de compilação, `virtualizedpaths.h: No such file or directory`.

- [ ] **Step 3: Implementar**

`deps/modorganizer/src/virtualizedpaths.h`:

```cpp
#ifndef MODORGANIZER_VIRTUALIZEDPATHS_H
#define MODORGANIZER_VIRTUALIZEDPATHS_H

#include <QString>

#include <optional>

// a program whose binary or working directory is inside the mods directory runs from
// the matching place of the data directory, where the virtual file system shows the
// files of its mod (mods/FNIS/tools/x.exe => <game>/Data/tools/x.exe); on Windows
// through the "launch" command of another instance, hooked by usvfs, on Linux directly
// (every process sees the mount)
struct VirtualizedPaths
{
  // in the data directory if they were in the mods directory, unchanged otherwise
  QString binary;
  QString workingDirectory;

  // the mod of the binary, empty if the binary is not in the mods directory
  QString mod;
};

// absolute paths with '/' separators; the mods directory is compared ignoring case, as
// Windows always did; std::nullopt if neither the binary nor the working directory is
// inside the mods directory
std::optional<VirtualizedPaths> virtualizedPaths(const QString& modsPath,
                                                 const QString& dataPath,
                                                 const QString& binary,
                                                 const QString& workingDirectory);

#endif  // MODORGANIZER_VIRTUALIZEDPATHS_H
```

`deps/modorganizer/src/virtualizedpaths.cpp`:

```cpp
#include "virtualizedpaths.h"

#include <utility>

namespace
{

// "<mods>/<mod>/rest" => {"<data>/rest", "<mod>"}, "<mods>/<mod>" => {"<data>", "<mod>"};
// trailedMods ends with '/'
std::optional<std::pair<QString, QString>>
inDataDirectory(const QString& trailedMods, const QString& dataPath, const QString& path)
{
  if (!path.startsWith(trailedMods, Qt::CaseInsensitive)) {
    return std::nullopt;
  }

  const qsizetype start = trailedMods.length();
  const qsizetype slash = path.indexOf('/', start);
  if (slash < 0) {
    return std::make_pair(dataPath, path.mid(start));
  }

  return std::make_pair(dataPath + path.mid(slash), path.mid(start, slash - start));
}

}  // namespace

std::optional<VirtualizedPaths> virtualizedPaths(const QString& modsPath,
                                                 const QString& dataPath,
                                                 const QString& binary,
                                                 const QString& workingDirectory)
{
  QString trailedMods = modsPath;
  if (!trailedMods.endsWith('/')) {
    trailedMods += '/';
  }

  const auto bin = inDataDirectory(trailedMods, dataPath, binary);
  const auto cwd = inDataDirectory(trailedMods, dataPath, workingDirectory);
  if (!bin && !cwd) {
    return std::nullopt;
  }

  return VirtualizedPaths{bin ? bin->first : binary,
                          cwd ? cwd->first : workingDirectory,
                          bin ? bin->second : QString()};
}
```

`deps/modorganizer/src/CMakeLists.txt`, em `_organizer_launch_sources` (o arquivo fica na biblioteca estática que o app e os testes ligam; no Windows ele entra pelo glob do `mo2_configure_target`, como os demais `.cpp` de `src/`):

```cmake
set(_organizer_launch_sources
	filemappingbuilder.cpp
	virtualizedpaths.cpp
	shared/appconfig.cpp
```

E acrescente ao comentário acima da lista ("the Linux parts of running programs (...)") a menção a `virtualizedpaths.cpp` só se o comentário enumerar arquivos; ele não enumera, então não mude o comentário.

`deps/modorganizer/src/processrunner.cpp`: inclua `#include "virtualizedpaths.h"` junto aos includes do arquivo e troque o corpo de `adjustForVirtualized()` (linhas 13–54) por:

```cpp
void adjustForVirtualized(const IPluginGame* game, spawn::SpawnParameters& sp,
                          const Settings& settings)
{
  // if the binary or the working directory is under our mods folder, the process is
  // started in a virtualized "environment" with the appropriate paths fixed
  // (i.e. mods\FNIS\path\exe => game\data\path\exe, see virtualizedpaths.h)
  const auto paths = virtualizedPaths(
      settings.paths().mods(), game->dataDirectory().absolutePath(),
      sp.binary.absoluteFilePath(), sp.currentDirectory.absolutePath());
  if (!paths) {
    return;
  }

  QString cmdline = QString("launch \"%1\" \"%2\" %3")
                        .arg(QDir::toNativeSeparators(paths->workingDirectory),
                             QDir::toNativeSeparators(paths->binary), sp.arguments);

  sp.binary    = QFileInfo(QCoreApplication::applicationFilePath());
  sp.arguments = cmdline;
  sp.currentDirectory.setPath(QCoreApplication::applicationDirPath());
}
```

Conferência de equivalência (não é código): antes, `cwdPath`/`binPath` eram `absolutePath()`/`absoluteFilePath()`, testados com `startsWith(trailedModsPath, Qt::CaseInsensitive)` e reescritos para `dataDirectory().absolutePath() + resto` (resto a partir da primeira `/` depois do prefixo, ou nada); é o que `virtualizedPaths()` faz.

- [ ] **Step 4: Rodar e ver passar**

Run: `distrobox enter mo2-run -- bash -c 'cd build-out/modorganizer && cmake --build . --target organizer_tests && TMPDIR=/tmp/mo2t ctest --output-on-failure -R VirtualizedPaths'`
Expected: os 9 testes `VirtualizedPaths.*` PASS. Depois `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only modorganizer --tests`: tudo verde (o app compila com o `processrunner.cpp` alterado).

- [ ] **Step 5: Commit**

```bash
git add deps/modorganizer/src/virtualizedpaths.h deps/modorganizer/src/virtualizedpaths.cpp \
    deps/modorganizer/src/CMakeLists.txt deps/modorganizer/src/processrunner.cpp \
    deps/modorganizer/tests/test_virtualizedpaths.cpp
git commit -m "deps/modorganizer: Linux: the paths of a program inside the mods directory in a shared function" \
    -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
```

---

### Task 2: O Linux roda programas de `mods/` pelo `Data` virtual

**Files:**
- Modify: `deps/modorganizer/src/linux/processrunner_linux.cpp` (comentário do topo, includes, `ProcessRunner::run()`)
- Modify: `tests/fixtures/vfs-e2e/instance/ModOrganizer.ini` (executável `tool-exe`)
- Modify: `tests/fixtures/vfs-e2e/instance/profiles/Default/modlist.txt` (`-ModTool`)
- Create: `tests/fixtures/vfs-e2e/instance/mods/ModTool/Tool/tool.exe` (vazio)
- Create: `tests/fixtures/vfs-e2e/instance/mods/ModTool/Tool/sibling.txt`
- Modify: `tests/lib/instance.sh` (placeholder `@INSTANCE@`)
- Modify: `tests/vfs-e2e/umu-run` (modo ferramenta)
- Modify: `tests/vfs-e2e/run.sh` (dois cenários e o cabeçalho)

**Interfaces:**
- Consumes: `virtualizedPaths()` / `VirtualizedPaths` da Task 1 (`#include "../virtualizedpaths.h"`).
- Produces: nada usado por outras tarefas.

- [ ] **Step 1: Escrever o teste e2e que falha**

`tests/fixtures/vfs-e2e/instance/mods/ModTool/Tool/tool.exe`: arquivo vazio (como o `game/probe.exe`). `tests/fixtures/vfs-e2e/instance/mods/ModTool/Tool/sibling.txt`, conteúdo exato (com quebra de linha final):

```
from ModTool
```

`tests/fixtures/vfs-e2e/instance/profiles/Default/modlist.txt` (o mod começa desativado: os cenários anteriores listam o `Data` e não podem ver a pasta `Tool`):

```
+ModB
+ModA
-ModTool
```

`tests/fixtures/vfs-e2e/instance/ModOrganizer.ini`: `size=3` em `[customExecutables]` e, depois das linhas do executável 2:

```ini
3\title=tool-exe
3\binary=@INSTANCE@/mods/ModTool/Tool/tool.exe
3\arguments=
3\workingDirectory=
3\toolbar=false
3\hide=false
3\ownicon=false
3\steamAppID=
3\proton=GE-Proton11-7
```

`tests/lib/instance.sh`: no comentário do topo, acrescente `@INSTANCE@ = <instance dir>` à lista de placeholders, e no `sed`:

```bash
    sed -i -e "s|@GAME_PATH@|$game|g" -e "s|@PROBE@|$probe|g" \
        -e "s|@PROBE_EXE@|$game/probe.exe|g" -e "s|@WORK@|$work|g" \
        -e "s|@INSTANCE@|$instance|g" \
        "$instance/ModOrganizer.ini"
```

`tests/vfs-e2e/umu-run`: o modo ferramenta vem antes do `exec` do probe (o arquivo inteiro fica assim):

```bash
#!/usr/bin/env bash
# Fake umu-run of the end-to-end test: records how Mod Organizer (through mo2-vfs)
# started it, then runs probe.sh on the data directory given as the first argument of
# the "Windows program", like the game would see it.
# With UMU_TOOL=1 it stands for a tool installed as a mod (the executable tool-exe):
# after recording, it reads sibling.txt and creates tool-created.txt in its working
# directory, the folder of the tool, and exits.
set -u
OUT="${UMU_OUT:?UMU_OUT is not set}"
{
    printf 'arg %s\n' "$@"
    for v in WINEPREFIX GAMEID STORE SteamAppId SteamGameId PROTONPATH \
        STEAM_COMPAT_CLIENT_INSTALL_PATH STEAM_COMPAT_INSTALL_PATH \
        STEAM_COMPAT_LIBRARY_PATHS MO2_E2E_VAR; do
        printf 'env %s=%s\n' "$v" "${!v-<unset>}"
    done
    printf 'cwd %s\n' "$PWD"
} > "$OUT"
if [[ ${UMU_TOOL:-} == 1 ]]; then
    printf 'sibling %s\n' "$(cat sibling.txt 2>&1)" >> "$OUT"
    echo "created by the tool" > tool-created.txt && echo "created" >> "$OUT"
    exit 0
fi
# $1 is the .exe, $2 the data directory, $3 "two words"
exec "$PROBE" "$2"
```

`tests/vfs-e2e/run.sh`:

1. No comentário do topo, depois do parágrafo do `probe-exe` e antes de "Then the same run with MO2_VFS_BINARY ...", acrescente:

```bash
# Then the executable "tool-exe", a tool installed as a mod (mods/ModTool/Tool/tool.exe,
# no working directory configured), with the fake umu-run in tool mode: with ModTool
# active it must run from the data directory (argv and working directory in
# <game>/Data/Tool, sibling.txt read through the mount, tool-created.txt in
# overwrite/Tool, the real data directory unchanged); with ModTool inactive from the
# mods directory, with the warning "'ModTool' is not active" in the log.
```

2. Depois do `check_probe_run` do cenário `probe-exe` e antes de `# --- a helper whose mount fails`, acrescente:

```bash
# --- a tool inside the mods directory ----------------------------------------------

# set_tool_mod +|-: enables or disables ModTool (the application writes modlist.txt
# with CRLF line ends)
set_tool_mod() {
    sed -i -E "s/^[+-]ModTool(\r?)$/$1ModTool\1/" "$INSTANCE/profiles/Default/modlist.txt"
}

# run_tool: runs tool-exe with the fake umu-run in tool mode, output in $WORK/umu.out
run_tool() {
    rm -f "$WORK/umu.out"
    local rc=0
    run_app tool-exe PATH="$WORK/umu-bin:$PATH" UMU_OUT="$WORK/umu.out" UMU_TOOL=1 ||
        rc=$?
    ((rc == 0)) || die "the application exited with $rc (124: timeout)"
    [[ -s $WORK/umu.out ]] || die "umu-run was not started"
    cat "$WORK/umu.out"
}

echo "--- ModOrganizer -i vfs-e2e run -e tool-exe, ModTool active (${TIMEOUT}s)"
set_tool_mod +
run_tool
expect_line "arg $GAME/Data/Tool/tool.exe"
expect_line "cwd $GAME/Data/Tool"
expect_line "sibling from ModTool"
expect_line "created"
[[ $(grep -c '^arg ' "$WORK/umu.out") == 1 ]] || die "umu-run: not exactly 1 argument"
[[ $(cat "$INSTANCE/overwrite/Tool/tool-created.txt" 2>/dev/null) == "created by the tool" ]] ||
    die "overwrite/Tool/tool-created.txt is missing or wrong"
after="$(cd "$GAME/Data" && LC_ALL=C find . -type f -exec md5sum {} + | sort)"
[[ $after == "$original" ]] || die "the real data directory changed"
[[ -z $(mounts_under "$WORK") ]] || die "still mounted after the run: $(mounts_under "$WORK")"
echo "ok tool-exe ran from the data directory"

echo "--- ModOrganizer -i vfs-e2e run -e tool-exe, ModTool inactive (${TIMEOUT}s)"
set_tool_mod -
run_tool
expect_line "arg $INSTANCE/mods/ModTool/Tool/tool.exe"
expect_line "cwd $INSTANCE/mods/ModTool/Tool"
expect_line "sibling from ModTool"
grep -qF "'ModTool' is not active" "$APP_OUT" || die "no warning about the inactive ModTool"
[[ -z $(mounts_under "$WORK") ]] || die "still mounted after the run: $(mounts_under "$WORK")"
echo "ok tool-exe ran from the mods directory with ModTool inactive"
```

(`expect_line`, `original`, `mounts_under`, `run_app` e `die` já existem no script; `expect_line` lê `$WORK/umu.out`.)

- [ ] **Step 2: Rodar e ver falhar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t MO2_REQUIRE_FUSE=1 tests/vfs-e2e/run.sh install`
Expected: os cenários anteriores passam; o `tool-exe` com ModTool ativo falha com `vfs e2e test FAILED: umu-run: missing 'arg <...>/Data/Tool/tool.exe'` (hoje o argv é o caminho em `mods/`).

- [ ] **Step 3: Implementar no `ProcessRunner::run()` do Linux**

`deps/modorganizer/src/linux/processrunner_linux.cpp`:

(a) Comentário do topo: troque o trecho

```
//   (no-ops on Linux, see spawn_linux.cpp), and a program inside the mods directory is
//   not redirected to its place in the data directory through the "launch" command of
//   another instance (adjustForVirtualized(), which the Linux "launch" doesn't
//   support): it runs from where it is. Documents (setFromFile() with a file that is
//   not a program) are opened by their application, unhooked.
```

por

```
//   (no-ops on Linux, see spawn_linux.cpp). A program whose binary or working
//   directory is inside the mods directory runs from its place in the data directory
//   (virtualizedpaths.h), like on Windows, but without the "launch" command of another
//   instance (adjustForVirtualized()): every process sees the mount, so the paths of
//   the launch are changed directly; only with the virtual file system, and only when
//   the mod of the binary is active in the profile of the run (otherwise the file
//   isn't in the data directory and the program runs from the mods directory, with a
//   warning). Plugins (beforeRun()) see the paths of the executable, as on Windows.
//   Documents (setFromFile() with a file that is not a program) are opened by their
//   application, unhooked.
```

(b) Includes: acrescente, junto aos de `..`:

```cpp
#include "../modinfo.h"
#include "../settings.h"
#include "../virtualizedpaths.h"
```

e `#include <climits>` junto aos da biblioteca padrão. (Se `../settings.h` já vier por `organizercore.h`, o include explícito continua correto.)

(c) No `namespace` anônimo do arquivo (o que tem `toPid()`), acrescente:

```cpp
// whether the mod is active in the profile of a run; false for a mod that doesn't
// exist (OrganizerCore::fileMapping() reads the profile the same way)
bool modActive(OrganizerCore& core, const QString& profileName, const QString& mod)
{
  const unsigned int index = ModInfo::getIndex(mod);
  if (index == UINT_MAX) {
    return false;
  }

  const Profile profile(QDir(core.settings().paths().profiles() + "/" + profileName),
                        core.managedGame(), core.gameFeatures());
  return profile.modEnabled(index);
}
```

(d) Em `ProcessRunner::run()`, logo depois do bloco `if (shouldRunShell()) { showError(...); return Error; }` e antes de `// the output of the program, ...`, acrescente:

```cpp
  // the binary and the working directory of the launch; plugins (beforeRun()) get
  // those of the executable, as on Windows
  QFileInfo launchBinary  = m_sp.binary;
  QString launchDirectory = m_sp.currentDirectory.path();

  if (m_sp.hooked) {
    if (m_profileName.isEmpty()) {
      // get the current profile name if it wasn't overridden
      const auto profile = m_core.currentProfile();
      if (!profile) {
        throw MyException(QObject::tr("No profile set"));
      }

      m_profileName = profile->name();
    }

    // a program inside the mods directory runs from its place in the data directory,
    // where the virtual file system shows it (see the top of this file)
    const auto* game = m_core.managedGame();
    if (const auto paths = virtualizedPaths(
            m_core.settings().paths().mods(), game->dataDirectory().absolutePath(),
            m_sp.binary.absoluteFilePath(), m_sp.currentDirectory.path())) {
      if (paths->mod.isEmpty()) {
        // only the working directory is in a mod
        launchDirectory = paths->workingDirectory;
      } else if (modActive(m_core, m_profileName, paths->mod)) {
        launchBinary    = QFileInfo(paths->binary);
        launchDirectory = paths->workingDirectory;
      } else {
        log::warn("'{}' is not active; '{}' runs from the mods directory", paths->mod,
                  m_sp.binary.absoluteFilePath());
      }

      if (launchBinary != m_sp.binary ||
          launchDirectory != m_sp.currentDirectory.path()) {
        log::debug("virtualized: '{}' runs as '{}' in '{}'",
                   m_sp.binary.absoluteFilePath(), launchBinary.absoluteFilePath(),
                   launchDirectory);
      }
    }
  }
```

Notas para quem implementa:
- `m_sp.currentDirectory.path()` (e não `absolutePath()`): vazio ou `.` quer dizer "a pasta do binário" para o `makeLaunch()`; com o binário reescrito, essa pasta passa a ser a do `Data` (o caso do Nemesis). `setFromExecutable()` normalmente já preenche a pasta com a do binário real (`processrunner.cpp:571`); ela está em `mods/` e é reescrita junto.
- Mod inativo: o binário fica o real e a pasta de trabalho também (não se junta binário real com pasta virtual). Só a pasta em `mods/` com binário fora (`paths->mod` vazio) é reescrita, como no Windows.

(e) No `proton::makeLaunch(...)` logo abaixo, troque `m_sp.binary` e `m_sp.currentDirectory.path()` pelos do lançamento:

```cpp
    request          = proton::makeLaunch(
        {launchBinary, m_sp.arguments, launchDirectory, m_sp.steamAppID,
         game ? game->steamAPPId() : QString(), m_sp.proton, m_sp.environment, output});
```

(f) No bloco `if (m_sp.hooked) { ... beforeRun ... }` que vem depois, apague o trecho `if (m_profileName.isEmpty()) { ... m_profileName = profile->name(); }` (agora resolvido acima, antes da reescrita), deixando só o comentário e a chamada de `m_core.beforeRun(m_sp.binary, m_sp.currentDirectory, ...)`, que continua com os valores **originais** de `m_sp`.

O nome do log da saída (`output`, `run-<completeBaseName>.log`) continua vindo de `m_sp.binary`: é o mesmo nome de arquivo.

- [ ] **Step 4: Rodar e ver passar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only modorganizer --tests` e depois `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t MO2_REQUIRE_FUSE=1 tests/vfs-e2e/run.sh install`
Expected: unitários verdes; o e2e imprime `ok tool-exe ran from the data directory` e `ok tool-exe ran from the mods directory with ModTool inactive` e termina sem `FAILED`. Rode também o smoke (`tests/smoke/modorganizer-smoke.sh install`, ou o `./build.sh --tests` completo), que usa a mesma fixture.

- [ ] **Step 5: Commit (dois commits)**

```bash
git add deps/modorganizer/src/linux/processrunner_linux.cpp
git commit -m "deps/modorganizer: Linux: a program inside the mods directory runs from the data directory" \
    -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
git add tests/fixtures/vfs-e2e tests/lib/instance.sh tests/vfs-e2e/umu-run tests/vfs-e2e/run.sh
git commit -m "tests: vfs-e2e: a tool inside the mods directory, active and inactive" \
    -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
```

---

### Task 3: xEdit de outros jogos: pastas no prefixo de cada jogo (fixtures)

**Files:**
- Create: `deps/game_bethesda/tests/test_steam_games_linux.cpp`
- Modify: `deps/game_bethesda/tests/CMakeLists.txt`
- Modify (só se um teste falhar, ver Step 4): `deps/game_bethesda/src/gamebryo/gamegamebryo.cpp` e/ou `deps/game_bethesda/src/games/<jogo>/game<jogo>.cpp`

**Interfaces:**
- Consumes: os plugins de jogo como o MO2 os carrega (`QPluginLoader` + `qobject_cast<MOBase::IPluginGame*>`); `IPluginGame::detectGame()`, `isInstalled()`, `gameDirectory()`, `binaryName()`, `documentsDirectory()`, `savesDirectory()`, `initializeProfile(QDir, IPluginGame::MODS)`.
- Produces: nada usado por outras tarefas.

Por que `initializeProfile(..., MODS)`: ela copia o `plugins.txt` da pasta `AppData/Local/<jogo>` (`GameGamebryo::localAppFolder()`, a mesma do mapeamento do VFS) para o perfil, sem precisar de um `IOrganizer`; o teste do Skyrim SE já usa esse caminho (`test_skyrimse_linux.cpp`, último teste). `IPluginFileMapper::mappings()` exigiria um `IOrganizer` (`profilePath()`), e não há mock dele.

- [ ] **Step 1: Escrever os testes**

`deps/game_bethesda/tests/test_steam_games_linux.cpp` (entra no executável `game_bethesda_tests`, cujo `main()` está em `test_skyrimse_linux.cpp`, com a `QCoreApplication` e o log):

```cpp
// The Steam games that xEdit supports, on Linux: each plugin (loaded like Mod Organizer
// loads it, with QPluginLoader) finds its game in a Steam library of a temporary HOME
// and puts My Games (the INIs xEdit reads) and AppData/Local (plugins.txt) in the Proton
// prefix of that game's app id (spec of Phase 4c.3, §3).

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QTemporaryDir>

#include <uibase/iplugingame.h>

#include <memory>
#include <ostream>

using namespace MOBase;

namespace
{

struct SteamGame
{
  const char* name;        // test name
  const char* plugin;      // the plugin file
  const char* appId;       // Steam app id
  const char* installDir;  // steamapps/common/<installDir>
  const char* myGames;     // Documents/My Games/<myGames>, as on Windows
  const char* localApp;    // AppData/Local/<localApp>, the folder the plugin reads
};

void PrintTo(const SteamGame& g, std::ostream* os)
{
  *os << g.name;
}

const SteamGame steamGames[] = {
    {"Skyrim", GAME_PLUGIN_SKYRIM, "72850", "Skyrim", "Skyrim", "Skyrim"},
    {"SkyrimVR", GAME_PLUGIN_SKYRIMVR, "611670", "SkyrimVR", "Skyrim VR", "Skyrim VR"},
    {"Enderal", GAME_PLUGIN_ENDERAL, "933480", "Enderal", "Enderal", "enderal"},
    {"EnderalSE", GAME_PLUGIN_ENDERALSE, "976620", "Enderal Special Edition",
     "Enderal Special Edition", "Enderal Special Edition"},
    {"Oblivion", GAME_PLUGIN_OBLIVION, "22330", "Oblivion", "Oblivion", "Oblivion"},
    {"Fallout3", GAME_PLUGIN_FALLOUT3, "22300", "Fallout 3", "Fallout3", "Fallout3"},
    {"FalloutNV", GAME_PLUGIN_FALLOUTNV, "22380", "Fallout New Vegas", "FalloutNV",
     "FalloutNV"},
    {"Fallout4", GAME_PLUGIN_FALLOUT4, "377160", "Fallout 4", "Fallout4", "Fallout4"},
    {"Fallout4VR", GAME_PLUGIN_FALLOUT4VR, "611660", "Fallout 4 VR", "Fallout4VR",
     "Fallout4VR"},
    {"Starfield", GAME_PLUGIN_STARFIELD, "1716740", "Starfield", "Starfield", "Starfield"},
};

bool write(const QString& path, const QByteArray& content)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

QByteArray read(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// a HOME with the native Steam client, whose library has the game and its Proton
// prefix (compatdata/<app id>/pfx), and a second prefix of another app id that must
// never be used
class SteamGames : public testing::TestWithParam<SteamGame>
{
protected:
  void SetUp() override
  {
    const SteamGame& g = GetParam();
    ASSERT_TRUE(m_home.isValid());
    m_oldHome = qgetenv("HOME");
    qputenv("HOME", QFile::encodeName(m_home.path()));
    m_oldConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_home.path() + "/.config"));

    const QString steam = m_home.path() + "/.local/share/Steam";
    m_game              = steam + "/steamapps/common/" + g.installDir;
    m_user = steam + "/steamapps/compatdata/" + g.appId + "/pfx/drive_c/users/steamuser";
    m_profile = m_home.path() + "/profile";

    ASSERT_TRUE(write(steam + "/steamapps/libraryfolders.vdf",
                      "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" +
                          QFile::encodeName(steam) +
                          "\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"" + QByteArray(g.appId) +
                          "\"\t\t\"0\"\n\t\t}\n\t}\n}\n"));
    ASSERT_TRUE(write(steam + "/steamapps/appmanifest_" + g.appId + ".acf",
                      "\"AppState\"\n{\n\t\"appid\"\t\t\"" + QByteArray(g.appId) +
                          "\"\n\t\"installdir\"\t\t\"" + QByteArray(g.installDir) +
                          "\"\n}\n"));
    ASSERT_TRUE(QDir().mkpath(m_user));
    ASSERT_TRUE(QDir().mkpath(
        steam + "/steamapps/compatdata/489830/pfx/drive_c/users/steamuser"));
    ASSERT_TRUE(QDir().mkpath(m_game + "/Data"));
    ASSERT_TRUE(QDir().mkpath(m_profile));

    m_loader          = std::make_unique<QPluginLoader>(g.plugin);
    QObject* instance = m_loader->instance();
    ASSERT_NE(instance, nullptr) << m_loader->errorString().toStdString();
    m_plugin = qobject_cast<IPluginGame*>(instance);
    ASSERT_NE(m_plugin, nullptr);

    // the plugin only finds a game whose binary is there
    ASSERT_TRUE(write(m_game + "/" + m_plugin->binaryName(), ""));
  }

  void TearDown() override
  {
    qputenv("HOME", m_oldHome);
    if (m_oldConfigHome.isNull()) {
      qunsetenv("XDG_CONFIG_HOME");
    } else {
      qputenv("XDG_CONFIG_HOME", m_oldConfigHome);
    }
  }

  QTemporaryDir m_home;
  QByteArray m_oldHome;
  QByteArray m_oldConfigHome;
  QString m_game;
  QString m_user;
  QString m_profile;
  std::unique_ptr<QPluginLoader> m_loader;
  IPluginGame* m_plugin = nullptr;
};

}  // namespace

TEST_P(SteamGames, DetectsTheGame)
{
  m_plugin->detectGame();

  EXPECT_TRUE(m_plugin->isInstalled());
  EXPECT_EQ(m_plugin->gameDirectory().absolutePath().toStdString(),
            QDir(m_game).absolutePath().toStdString());
}

TEST_P(SteamGames, MyGamesInThePrefixOfTheGame)
{
  m_plugin->detectGame();

  const QString documents = m_user + "/Documents/My Games/" + GetParam().myGames;
  EXPECT_EQ(m_plugin->documentsDirectory().absolutePath().toStdString(),
            documents.toStdString());
  EXPECT_TRUE(m_plugin->savesDirectory().absolutePath().startsWith(documents + "/"))
      << m_plugin->savesDirectory().absolutePath().toStdString();
}

TEST_P(SteamGames, PluginsTxtFromThePrefixOfTheGame)
{
  ASSERT_TRUE(write(m_user + "/AppData/Local/" + GetParam().localApp + "/plugins.txt",
                    "*FromThePrefix.esp\r\n"));
  m_plugin->detectGame();

  m_plugin->initializeProfile(QDir(m_profile), IPluginGame::MODS);

  EXPECT_EQ(read(m_profile + "/plugins.txt"), QByteArray("*FromThePrefix.esp\r\n"));
}

INSTANTIATE_TEST_SUITE_P(Linux, SteamGames, testing::ValuesIn(steamGames),
                         [](const testing::TestParamInfo<SteamGame>& info) {
                           return std::string(info.param.name);
                         });
```

`deps/game_bethesda/tests/CMakeLists.txt`, troque o bloco do `game_bethesda_tests` por:

```cmake
# Skyrim SE, and the Steam games that xEdit supports (test_steam_games_linux.cpp): loads
# the plugins like Mod Organizer does (QPluginLoader)
add_executable(game_bethesda_tests test_skyrimse_linux.cpp test_steam_games_linux.cpp)
mo2_configure_tests(game_bethesda_tests NO_SOURCES NO_MAIN WARNINGS OFF)
target_compile_definitions(game_bethesda_tests PRIVATE
	SKYRIMSE_PLUGIN="$<TARGET_FILE:game_skyrimse>"
	GAME_PLUGIN_SKYRIM="$<TARGET_FILE:game_skyrim>"
	GAME_PLUGIN_SKYRIMVR="$<TARGET_FILE:game_skyrimvr>"
	GAME_PLUGIN_ENDERAL="$<TARGET_FILE:game_enderal>"
	GAME_PLUGIN_ENDERALSE="$<TARGET_FILE:game_enderalse>"
	GAME_PLUGIN_OBLIVION="$<TARGET_FILE:game_oblivion>"
	GAME_PLUGIN_FALLOUT3="$<TARGET_FILE:game_fallout3>"
	GAME_PLUGIN_FALLOUTNV="$<TARGET_FILE:game_falloutnv>"
	GAME_PLUGIN_FALLOUT4="$<TARGET_FILE:game_fallout4>"
	GAME_PLUGIN_FALLOUT4VR="$<TARGET_FILE:game_fallout4vr>"
	GAME_PLUGIN_STARFIELD="$<TARGET_FILE:game_starfield>")
target_link_libraries(game_bethesda_tests PRIVATE mo2::uibase)
add_dependencies(game_bethesda_tests game_skyrimse game_skyrim game_skyrimvr
	game_enderal game_enderalse game_oblivion game_fallout3 game_falloutnv
	game_fallout4 game_fallout4vr game_starfield)
```

- [ ] **Step 2: Rodar**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only game_bethesda --tests`
Expected: compila; os 30 testes `Linux/SteamGames.*` rodam. Os plugins hoje só compilam no Linux, então falhas são possíveis e são o objetivo do teste: anote quais (`ctest --output-on-failure` mostra o caminho obtido e o esperado).

- [ ] **Step 3: Conferir as falhas contra o Windows antes de corrigir**

Para cada falha, decida se é defeito do plugin no Linux ou expectativa errada da tabela, lendo o plugin (`deps/game_bethesda/src/games/<jogo>/`):
- **Caminho fora do prefixo do appid** (ex.: `documentsDirectory()` nas pastas XDG, ou no prefixo `489830`): defeito. Causas prováveis, em ordem: (1) o `detectGame()` do jogo calcula `m_MyGamesPath` antes de `setGameVariant()`, e `steamAPPId()` depende da variante (Fallout 3, Fallout NV, Enderal SE): a pasta é calculada com appid vazio; corrigir a ordem no `detectGame()` do jogo; (2) o jogo calcula "My Games" sem passar por `GameGamebryo::protonUserFolder()` (`gamegamebryo_linux.cpp`) — usar o mesmo caminho que o `determineMyGamesPath()` comum usa.
- **Jogo não detectado** (`DetectsTheGame` falha): conferir no `identifyGamePath()` do jogo qual `installdir`/appid ele procura (ex.: o Fallout 3 procura `22300` com `Fallout 3` e `22370` com `Fallout 3 goty`); se a tabela divergir do nome real da Steam, corrija a tabela, não o plugin.
- **Nome de pasta diferente** do da tabela, mas dentro do prefixo certo: a tabela segue o Windows (spec §3); se o plugin usa outro nome que o Windows também usa (o Windows não diferencia maiúsculas, ex.: `enderal`), ajuste a tabela e escreva no commit por quê.

- [ ] **Step 4: Corrigir os defeitos (se houver) e rodar de novo**

Correções no ponto comum (`GameGamebryo`) quando o defeito se repete em vários jogos; no `game<jogo>.cpp` quando é de um só; código Windows idêntico (só ordem de chamadas ou código em `#ifndef _WIN32`). Depois de cada correção:

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --only game_bethesda --tests`
Expected: todos os testes do `game_bethesda_tests` (os 30 novos e os do Skyrim SE) e do `game_gamebryo_tests` PASS.

- [ ] **Step 5: Commit**

```bash
git add deps/game_bethesda/tests/test_steam_games_linux.cpp deps/game_bethesda/tests/CMakeLists.txt
git commit -m "deps/game_bethesda: Linux: tests: My Games and plugins.txt in the prefix of each Steam game" \
    -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
```

Cada correção de plugin do Step 4 vai num commit próprio, antes deste, `deps/game_bethesda: Linux: <jogo>: <o que muda>`, dizendo no corpo qual teste falhava e por quê.

---

### Task 4: Documentação, checklist e ponto de controle de CI

**Files:**
- Modify: `README.md` (seção "Rodando no Bazzite (pt-BR)", subseção nova antes de `## Building`)
- Create: `docs/testing/phase4c3-manual-checklist.md`
- Modify: `ROADMAP.md`
- Modify: `STATE.md`

**Interfaces:**
- Consumes: o comportamento das Tasks 1–3 e os resultados do Step 4 da Task 3 (quais plugins foram corrigidos).

- [ ] **Step 1: README**

Antes de `## Building`, no fim da seção "Rodando no Bazzite (pt-BR)", acrescente:

```markdown
### Ferramentas (BodySlide, Nemesis, Pandora, xEdit)

Instale a ferramenta como mod (pelo MO2, a partir do arquivo baixado) e configure o executável pelo `.exe` **dentro da pasta do mod** (botão de executáveis → binário em `mods/<mod>/...`). Como no Windows, o MO2 roda um programa de dentro de `mods/` a partir do lugar correspondente no `Data` virtual (`mods/Nemesis/Nemesis_Engine/x.exe` → `<jogo>/Data/Nemesis_Engine/x.exe`), então ferramentas que precisam estar no `Data`, como o Nemesis, funcionam. O mod precisa estar ativo no perfil; desativado, o programa roda da pasta do mod (aviso no log).

- **Nemesis e BodySlide:** sem argumentos. O que eles geram vai para o `overwrite` (mova para um mod depois, como no Windows).
- **Pandora:** use o build do Nexus (no GitHub, o `..._win-x64_net.zip`), que traz o .NET; o zip que depende do .NET instalado não abre num prefixo sem o .NET 10. Na 5.0.0-beta o argumento `-o` é ignorado pelo próprio Pandora: a saída vai para o `overwrite`.
- **xEdit de outros jogos** (FO4Edit, TES5Edit, FNVEdit...): o executável roda no prefixo do jogo da instância (ou no do campo "Steam AppID" do executável). O jogo precisa ter rodado uma vez pela Steam, para o prefixo existir e o registro `Installed Path`, onde o xEdit procura o jogo, ser gravado.
```

- [ ] **Step 2: Checklist manual**

`docs/testing/phase4c3-manual-checklist.md`:

```markdown
# Fase 4c.3 — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-21-phase4c3-ferramentas-design.md`](../superpowers/specs/2026-10-21-phase4c3-ferramentas-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 iniciado **pelo box**: `distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores. Na pasta de downloads da instância: BodySlide and Outfit Studio (Nexus 201), Nemesis Unlimited Behavior Engine (Nexus 60033) e Pandora Behaviour Engine (Nexus 133232).

**Build:** o `install/` do worktree `ModOrganizerLinux-4c3` (`./build.sh` na distrobox `mo2-run`) ou, depois de um run Linux verde do PR, o artefato `mo2-linux-install` desse run (restaure a permissão de execução: `chmod +x <pasta do build>/bin/ModOrganizer <pasta do build>/bin/mo2-vfs`). O `mo2-vfs` precisa ficar na mesma pasta que o `ModOrganizer`.

## Preparação

- [ ] Instale os três arquivos como mods (aba Downloads → instalar) e ative-os. Crie um mod vazio "Tool Output" (opcional).
- [ ] Configure três executáveis com o binário **dentro da pasta do mod** (botão "..." do binário, navegando até `mods/`):
  - Nemesis: `mods/<Nemesis>/Nemesis_Engine/Nemesis Unlimited Behavior Engine.exe`;
  - BodySlide: `mods/<BodySlide>/CalienteTools/BodySlide/BodySlide.exe` (ou `BodySlide x64.exe`, conforme o arquivo instalado);
  - Pandora: `mods/<Pandora>/.../Pandora Behaviour Engine+.exe`.
- [ ] Anote o conteúdo do `overwrite` (de preferência, vazio).

## 1. Nemesis

- [ ] Executar o Nemesis. Esperado: abre sem o "ERROR(1008)"; no log do MO2 (`logs/mo_interface.log`, em debug) há `virtualized: '<...mods...>' runs as '<...>/Data/Nemesis_Engine/...'`.
- [ ] "Update Engine" e depois "Launch Nemesis Behavior Engine" terminam sem erro.
- [ ] Fechar o Nemesis. Esperado: o MO2 destrava; o `overwrite` tem `meshes/actors/character/behaviors/*.hkx`, `meshes/animationdatasinglefile.txt`, `scripts/FNIS.pex` e `Nemesis_Engine/...`; `findmnt -t fuse.mo2-vfs` no host não lista mais nada.

## 2. BodySlide

- [ ] Executar o BodySlide. Esperado: abre, a pré-visualização aparece; em `overwrite/CalienteTools/BodySlide/Log_BS.txt`, "Game data path in config" é `S:\steamapps\common\Skyrim Special Edition\Data\` e "Executable directory" está em `...\Data\CalienteTools\BodySlide`.
- [ ] (Opcional, com o CBBE instalado) Batch Build: as malhas vão para o `overwrite/meshes`.

## 3. Pandora

- [ ] Executar o Pandora, marcar os patches e clicar em Launch. Esperado: termina; o `overwrite` tem `meshes/actors/character/behaviors/0_master.hkx` (e outros `.hkx`) e `Engine.log`.

## 4. Mod desativado

- [ ] Desativar o mod do Nemesis e executá-lo. Esperado: o log do MO2 tem `'<mod>' is not active; '<binário>' runs from the mods directory`; o Nemesis abre e mostra o "ERROR(1008)" (roda da pasta do mod, como antes desta fase). Reativar o mod.

## 5. Regressão

- [ ] SSEEdit (configurado fora de `mods/`) abre pelo VFS e lista os plugins dos mods, como na Fase 3.
- [ ] SKSE inicia o jogo com os mods, como na Fase 3.
```

- [ ] **Step 3: ROADMAP e STATE**

`ROADMAP.md`:
- linha da 4c.3 na tabela: status `🟡 Em andamento`;
- em "Fase 4 — Ecossistema", depois da linha da Fase 4c.1, acrescente: `Fase 4c.3 — Spec: [\`docs/superpowers/specs/2026-10-21-phase4c3-ferramentas-design.md\`](docs/superpowers/specs/2026-10-21-phase4c3-ferramentas-design.md) · Plano: [\`docs/superpowers/plans/2026-10-21-phase4c3-ferramentas.md\`](docs/superpowers/plans/2026-10-21-phase4c3-ferramentas.md) · Checklist manual: [\`docs/testing/phase4c3-manual-checklist.md\`](docs/testing/phase4c3-manual-checklist.md)` (se a 4c.2 também não tiver essa linha, acrescente a dela no mesmo formato).

`STATE.md`, seguindo o formato das seções da 4c.2:
- cabeçalho: **Fase atual:** 4c.3 — Ferramentas pelo VFS · **Status:** 🟡 implementação concluída; push, CI e teste manual pendentes · **Anterior:** 4c.2 (🟡 PR #5; teste manual pendente); acrescente o branch da 4c.3 (`fase-4c3-ferramentas`, a partir de `fase-4c2-pendencias`; worktree `../ModOrganizerLinux-4c3`) à linha de branches;
- seção `## Fase 4c.3 — progresso` com spec, plano, checklist e a tabela de tarefas (1–5) com os hashes dos commits;
- seção `## Limitações conhecidas da Fase 4c.3`: com o mod do binário desativado, o programa roda da pasta do mod (o Nemesis então mostra o erro 1008); Pandora 5.0.0-beta ignora `-o`; o xEdit de outros jogos só tem cobertura por fixtures (nenhum outro jogo Bethesda testado de verdade); o jogo precisa ter rodado uma vez pela Steam para o registro `Installed Path` existir;
- em "Pendências por fase", uma subseção da Fase 4c.3 com: o comando `run` às vezes para de esperar o programa antes de ele terminar (visto duas vezes com o log em nível normal no spike de 2026-10-21, nunca em debug; o MO2 sai com "mo2-vfs is still running, left to end by itself" e o mount fica até o programa sair);
- na tabela de decisões: `2026-10-21 | Fase 4c.3: escopo (decisão do usuário): ferramentas reais testadas no Skyrim SE; xEdit de outros jogos só com fixtures; spike antes da spec | ` e `2026-10-21 | Fase 4c.3 (abordagem A): programa com binário ou pasta de trabalho em mods/ roda do Data virtual também no Linux, por reescrita dos caminhos antes do spawn (sem relançar o MO2), só com VFS e com o mod do binário ativo; regra comum ao Windows em virtualizedpaths.h | `;
- em "Próximos passos": a 4c.3 (revisão final, push de `fase-4c3-ferramentas` e PR contra `fase-4c2-pendencias` com a autorização do usuário, CIs e teste manual com o checklist).

- [ ] **Step 4: Build completo e commit da documentação**

Run: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t ./build.sh --tests`
Expected: verde (unitários, `game_bethesda`, smoke, vfs-e2e, loot-e2e).

```bash
git add README.md docs/testing/phase4c3-manual-checklist.md ROADMAP.md STATE.md
git commit -m "docs: Phase 4c.3: tools in the README, manual checklist, roadmap and state" \
    -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
```

- [ ] **Step 5: Ponto de controle do CI Windows**

Commit vazio que pede o CI do Windows (como nas fases anteriores):

```bash
git commit --allow-empty -m "ci: Phase 4c.3 checkpoint [windows-ci]" \
    -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01QHa2t4wnuh2a3ozke1drnL"
```

O push e o PR (`fase-4c3-ferramentas` → `fase-4c2-pendencias`) só com a autorização do usuário.

---

### Task 5: Teste manual com o usuário e fechamento

- [ ] Com o push autorizado e os CIs verdes (Linux e Windows), o usuário roda `docs/testing/phase4c3-manual-checklist.md` no Bazzite, com o `install/` do worktree (MO2 pelo box). Falhas viram correções com teste antes de marcar a fase como concluída.
- [ ] Atualizar `STATE.md` (Task 5 da tabela, status) e `ROADMAP.md` com o resultado.

## Observações

- A instância portátil do spike está em `.superpowers/spike-4c3/portable-instance` (ignorado pelo git); não a devolva para `install/bin`.
- O teste manual usa a instância do próprio usuário, instalando os três arquivos pelo MO2 (o que também exercita os instaladores).
