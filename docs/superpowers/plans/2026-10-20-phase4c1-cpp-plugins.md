# Fase 4c.1 — Plugins C++ restantes — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Os nove plugins C++ do upstream que faltam no Linux (`installer_bain`, `installer_bundle`, `preview_base`, `preview_bsa`, `bsa_extractor`, `diagnose_basic`, `check_fnis`, `tool_inieditor`, `tool_inibakery`) compilam, carregam no MO2 nativo e funcionam.

**Architecture:** Cada plugin entra como `git subtree` em `deps/<plugin>` (master do upstream) e ganha um alvo no `build.sh`, instalado em `bin/plugins`. Patches só onde há Win32, em `#ifdef _WIN32`/`#else`, com a visão Windows idêntica ao upstream. A checagem de carga (`tests/plugin-load`) ganha os tipos de plugin novos e lista as configurações de cada plugin; o smoke passa a exigir que todo `.so` de `bin/plugins` seja carregado pelo MO2.

**Tech Stack:** C++/Qt 6, CMake (`cmake_common`: `mo2_configure_plugin`, `mo2_install_plugin`), `uibase` (`win32compat`, `shell::Open`), `bsatk`, Boost; bash (`build.sh`, smoke); `git subtree`; `unifdef`.

**Spec:** `docs/superpowers/specs/2026-10-20-phase4c1-cpp-plugins-design.md` (ler inteira antes de cada tarefa; §N são seções dela).

## Global Constraints

- Branch `fase-4c1-plugins` (já criado a partir de `fase-4b-loot`; a spec está nele).
- Commits: o `git subtree add` gera os seus; patches dentro de `deps/<plugin>` em commit próprio `deps/<plugin>: Linux: …`, sem misturar arquivos de fora; o resto `build: …`, `tests: …`, `docs: …`. Todo commit termina com as linhas de atribuição da sessão; sem identificador de modelo no assunto.
- Pins (master em 2026-10-20; `git subtree add … master --squash` e conferir que o `git-subtree-split:` do commit é este hash):

  | Plugin | Repositório | Commit |
  |---|---|---|
  | `installer_bain` | `https://github.com/ModOrganizer2/modorganizer-installer_bain` | `0e60c19b2d64eef37d299754ff3c29497eccc478` |
  | `installer_bundle` | `https://github.com/ModOrganizer2/modorganizer-installer_bundle` | `1d7f9dc09a3283f8d59b7d8669a7018ed7840fe0` |
  | `preview_base` | `https://github.com/ModOrganizer2/modorganizer-preview_base` | `a8b2c5137d6b011275ea6f113a930b3481331cde` |
  | `preview_bsa` | `https://github.com/ModOrganizer2/modorganizer-preview_bsa` | `fb477c0850de24cebfb2922c437a97fbd014e1e3` |
  | `bsa_extractor` | `https://github.com/ModOrganizer2/modorganizer-bsa_extractor` | `d6b32b9d9d894bb71734843d62528748b4f06f03` |
  | `diagnose_basic` | `https://github.com/ModOrganizer2/modorganizer-diagnose_basic` | `558e3583bd97905126c88bb1a3b3219def3bd394` |
  | `check_fnis` | `https://github.com/ModOrganizer2/modorganizer-check_fnis` | `53522c3157607bd1c8d6a9639b3bbda9795f27fe` |
  | `tool_inieditor` | `https://github.com/ModOrganizer2/modorganizer-tool_inieditor` | `18b0e1c5869c1f6b6c129b7a3758142b582fea8c` |
  | `tool_inibakery` | `https://github.com/ModOrganizer2/modorganizer-tool_inibakery` | `355cac9619175016d9cee45589ecb01bba408a07` |

  Se o master tiver andado (o split não bater), use o hash novo e registre em `deps/README.md`, salvo se o upstream mudou algo que quebre a compilação — aí use o hash da tabela (`git subtree add … <hash> --squash`).
- Tipo de cada plugin na checagem de carga: `installer_bain`, `installer_bundle` → `installer`; `preview_base`, `preview_bsa` → `preview`; `bsa_extractor`, `check_fnis` → `plugin`; `diagnose_basic` → `diagnose`; `tool_inieditor` (alvo `inieditor`) → `tool`; `tool_inibakery` (alvo `inibakery`) → `filemapper`. Os `.so` instalados: `lib<alvo>.so` em `install/bin/plugins` (`libinieditor.so`, `libinibakery.so`, os demais `lib<plugin>.so`).
- Princípio dos patches (spec §1.2): a lógica não muda; só Win32 vai para `#ifdef _WIN32`/`#else`; a visão Windows de cada arquivo alterado tem de ser igual à do upstream: `unifdef -t -D_WIN32 -DWIN32 <nosso arquivo> | diff - <(unifdef -t -D_WIN32 -DWIN32 <(git show <commit do subtree add>:<arquivo>))` sem diferenças (`unifdef` sai com 1 quando mudou algo; use `|| true` e confira o `diff`). Correção grande (mais de ~30 linhas num plugin) → parar e reportar.
- `diagnose_basic`: no Linux a configuração `check_fileattributes` não existe e a checagem de atributos NTFS não roda. `tool_inieditor`: o modo externo usa `MOBase::shell::Open(<arquivo>)` no Linux.
- Arquivos `*_en.ts` regenerados pelo build (lupdate) não são commitados: `git checkout -- '*_en.ts'` antes de cada commit se aparecerem no `git status`.
- Mensagens ao usuário em inglês; `clang-format` do subtree (se não houver `.clang-format` no plugin, o de `deps/modorganizer/.clang-format`) só nas linhas alteradas; respeite o fim de linha de cada arquivo (alguns subtrees usam CRLF na árvore de trabalho; confira com `file`).

## Ambiente de execução

O CI (GitHub Actions, container Fedora 44 de `build/Containerfile`) é a referência. No Bazzite, compile na distrobox `mo2-run`, a partir da raiz: `distrobox enter mo2-run -- ./build.sh --only <alvo> --tests`. Builds longos: timeout generoso ou em segundo plano. `unifdef`: `distrobox enter mo2-run -- sudo dnf -y install unifdef` (só para a conferência; não entra no Containerfile). Smoke: TMPDIR padrão (um TMPDIR privado longo quebra o nome do `QLocalSocket` do passo `nxm`). Nunca `pkill -f`/`pgrep -f` com padrão que aparece na própria linha de comando.

## Review Focus

1. **Plugin que carrega no `QPluginLoader` mas falha no `init()` do MO2** (ex.: `inibakery`/`bsa_extractor` dependem do `IOrganizer`): a checagem de carga não pega; o smoke exige "loaded plugin … from '<arquivo>'" para cada `.so` → Task 8.
2. **Opção `check_fileattributes` aparecendo no Linux** (o usuário ligaria uma checagem que não existe): `plugin_load` imprime as configurações e o `build.sh` recusa essa chave → Tasks 1 e 5.
3. **Visão Windows alterada sem querer** (um patch fora do `#ifdef`): conferência `unifdef` por arquivo alterado em cada tarefa de patch → Tasks 3, 4, 5, 6, 7.
4. **`find_package(Boost … interprocess)` falhando só no Fedora**: patch Linux-only no CMake → Tasks 3 e 4.
5. **Caminho de INI com espaços/acentos no editor externo**: `shell::Open(QString)` (arquivo local) e não uma linha de comando montada à mão → Task 7 (e teste manual 6).

---

### Task 1: `plugin-load` — tipos novos e configurações

**Files:**
- Modify: `tests/plugin-load/main.cpp`
- Modify: `build.sh` (comentário de `check_plugins`)

**Interfaces:**
- Produces: `plugin_load <game|installer|preview|tool|diagnose|filemapper|plugin> <plugin.so>...`; uma linha por plugin: `ok   <arquivo>: '<nome>' [<interfaces>] settings: <chave1>, <chave2>` (ou `FAIL …`); sai com 1 se algum falha, 2 com uso errado. `plugin` aceita qualquer `IPlugin`.

- [ ] **Step 1: Código**

Em `tests/plugin-load/main.cpp`:

1. Cabeçalho: troque a primeira linha por `// plugin_load <game|installer|preview|tool|diagnose|filemapper|plugin> <plugin.so>...` e o parágrafo por:

```cpp
// Loads each plugin with QPluginLoader, like PluginContainer::loadQtPlugin(), and checks
// that its instance is an IPlugin that implements the interface of the kind (IPluginGame,
// IPluginInstaller, IPluginPreview, IPluginTool, IPluginDiagnose or IPluginFileMapper;
// "plugin" accepts any IPlugin). Prints one line per plugin, with its interfaces and the
// keys of its settings; exits with 1 if any plugin fails, 2 on a usage error.
```

2. Includes, depois de `#include <uibase/iplugin.h>`:

```cpp
#include <uibase/iplugindiagnose.h>
#include <uibase/ipluginfilemapper.h>
```

e depois de `#include <uibase/iplugininstallersimple.h>`:

```cpp
#include <uibase/ipluginpreview.h>
#include <uibase/iplugintool.h>
#include <uibase/pluginsetting.h>
```

3. Em `interfaces()`, antes do `return list;`:

```cpp
  if (qobject_cast<IPluginPreview*>(object)) {
    list.append("IPluginPreview");
  }
  if (qobject_cast<IPluginTool*>(object)) {
    list.append("IPluginTool");
  }
  if (qobject_cast<IPluginDiagnose*>(object)) {
    list.append("IPluginDiagnose");
  }
  if (qobject_cast<IPluginFileMapper*>(object)) {
    list.append("IPluginFileMapper");
  }
```

4. Depois de `interfaces()`:

```cpp
const QStringList Kinds = {"game",     "installer",  "preview", "tool",
                           "diagnose", "filemapper", "plugin"};

// whether the object implements the interface of the kind
bool implements(const QString& kind, QObject* object)
{
  if (kind == "game") {
    return qobject_cast<IPluginGame*>(object) != nullptr;
  }
  if (kind == "installer") {
    return qobject_cast<IPluginInstaller*>(object) != nullptr;
  }
  if (kind == "preview") {
    return qobject_cast<IPluginPreview*>(object) != nullptr;
  }
  if (kind == "tool") {
    return qobject_cast<IPluginTool*>(object) != nullptr;
  }
  if (kind == "diagnose") {
    return qobject_cast<IPluginDiagnose*>(object) != nullptr;
  }
  if (kind == "filemapper") {
    return qobject_cast<IPluginFileMapper*>(object) != nullptr;
  }
  return kind == "plugin";  // any IPlugin
}

QStringList settingKeys(const IPlugin* plugin)
{
  QStringList keys;
  for (const auto& setting : plugin->settings()) {
    keys.append(setting.key);
  }
  return keys;
}
```

5. Em `check()`, troque

```cpp
  const bool ok = kind == "game" ? qobject_cast<IPluginGame*>(object) != nullptr
                                 : qobject_cast<IPluginInstaller*>(object) != nullptr;
```

por `  const bool ok = implements(kind, object);` e o `std::printf` final por:

```cpp
  std::printf("%s %s: '%s' [%s] settings: %s\n", ok ? "ok  " : "FAIL",
              qUtf8Printable(file), qUtf8Printable(what),
              qUtf8Printable(interfaces(object).join(", ")),
              qUtf8Printable(settingKeys(plugin).join(", ")));
```

6. Em `main()`, troque o teste de uso e a mensagem:

```cpp
  if (args.size() < 3 || !Kinds.contains(args[1])) {
    std::fprintf(stderr, "usage: plugin_load <%s> <plugin.so>...\n",
                 qUtf8Printable(Kinds.join('|')));
    return 2;
  }
```

(`PluginSetting::key` é o campo da chave em `uibase/pluginsetting.h`; confira o nome lá antes de compilar.)

Em `build.sh`, no comentário de `check_plugins`, troque `# check_plugins <game|installer> <plugin>...` por `# check_plugins <kind> <plugin>...: kind is a type of tests/plugin-load (game, installer, preview, tool, diagnose, filemapper or plugin);` mantendo o resto do comentário.

- [ ] **Step 2: Conferir**

Run (na distrobox, depois de um `./build.sh --only installer_quick --tests`, que compila o `plugin_load`):

```bash
P=build-out/plugin-load/plugin_load
$P installer install/bin/plugins/libinstaller_quick.so; echo "rc $?"
$P plugin install/bin/plugins/libinstaller_quick.so; echo "rc $?"
$P preview install/bin/plugins/libinstaller_quick.so; echo "rc $?"
$P bogus install/bin/plugins/libinstaller_quick.so; echo "rc $?"
```

Expected: `ok   libinstaller_quick.so: '…' [IPluginInstaller, IPluginInstallerSimple] settings: …` e `rc 0` nos dois primeiros; `FAIL …` e `rc 1` no terceiro; a linha de uso e `rc 2` no último. `./build.sh --only game_bethesda --tests` continua passando (16 jogos `ok`).

- [ ] **Step 3: Commit**

```bash
git add tests/plugin-load/main.cpp build.sh
git commit -m "tests: plugin-load: preview, tool, diagnose, filemapper and plugin kinds; settings"
```

---

### Task 2: `installer_bain` e `installer_bundle`

**Files:**
- Create (subtree): `deps/installer_bain/`, `deps/installer_bundle/`
- Modify: `build.sh` (`ALL_TARGETS`, `build_installer_bain`, `build_installer_bundle`)
- Modify: `scripts/update-dep.sh` (`DEP_URLS`), `deps/README.md` (tabela)

**Interfaces:**
- Consumes: `check_plugins installer …` (Task 1).
- Produces: `install/bin/plugins/libinstaller_bain.so`, `libinstaller_bundle.so`; alvos `installer_bain`, `installer_bundle` do `build.sh`.

- [ ] **Step 1: Subtrees**

```bash
git subtree add --prefix deps/installer_bain https://github.com/ModOrganizer2/modorganizer-installer_bain master --squash
git subtree add --prefix deps/installer_bundle https://github.com/ModOrganizer2/modorganizer-installer_bundle master --squash
git log -1 --grep 'git-subtree-dir: deps/installer_bain$' --format=%B | grep split
git log -1 --grep 'git-subtree-dir: deps/installer_bundle$' --format=%B | grep split
```

Expected: os `git-subtree-split:` batem com a tabela das Global Constraints.

- [ ] **Step 2: Alvos**

Em `build.sh`, `ALL_TARGETS`: depois de `installer_fomod`, acrescente `installer_bain installer_bundle preview_base preview_bsa bsa_extractor diagnose_basic check_fnis tool_inieditor tool_inibakery` (as funções das Tasks 3–7 entram nelas; até lá o alvo sem função é pulado, "not implemented yet"). Depois de `build_installer_fomod() { … }`:

```bash
# the other installer plugins of upstream (Phase 4c.1): BAIN packages and archives
# that contain other archives
build_installer_bain() {
    cmake_build installer_bain "$ROOT/deps/installer_bain"
    check_plugins installer "$INSTALL/bin/plugins/libinstaller_bain.so"
}

build_installer_bundle() {
    cmake_build installer_bundle "$ROOT/deps/installer_bundle"
    check_plugins installer "$INSTALL/bin/plugins/libinstaller_bundle.so"
}
```

Em `scripts/update-dep.sh`, `DEP_URLS`: uma linha para cada um dos **nove** plugins (todas agora, para as próximas tarefas), depois de `[installer_fomod]=…`, no formato `[installer_bain]="https://github.com/ModOrganizer2/modorganizer-installer_bain"`.

Em `deps/README.md`, tabela: uma linha por plugin importado nesta tarefa, depois de `installer_fomod`, no formato das existentes: `` | `installer_bain` | https://github.com/ModOrganizer2/modorganizer-installer_bain | `master` (`<hash completo>`) | <data do commit upstream> | `` (data: `git log -1 --format=%cs <hash>` depois do subtree add, que traz o commit; ou a da spec §1.1).

- [ ] **Step 3: Compilar e carregar**

Run: `distrobox enter mo2-run -- ./build.sh --only installer_bain --only installer_bundle --tests`
Expected: as duas linhas `ok   libinstaller_bain.so: …` e `ok   libinstaller_bundle.so: …`, e `2 plugin(s) loaded, 0 failed` (uma vez por plugin). Se a compilação falhar por Win32, aplique o princípio dos patches (commit `deps/<plugin>: Linux: …`, conferência `unifdef`) e registre no relatório.

- [ ] **Step 4: Commit**

```bash
git checkout -- '*_en.ts' 2>/dev/null || true
git add build.sh scripts/update-dep.sh deps/README.md
git commit -m "build: installer_bain and installer_bundle plugins"
```

---

### Task 3: `preview_base` e `preview_bsa`

**Files:**
- Create (subtree): `deps/preview_base/`, `deps/preview_bsa/`
- Modify: `deps/preview_bsa/src/CMakeLists.txt` (Boost no Linux)
- Modify: `build.sh`, `deps/README.md`

**Interfaces:**
- Consumes: `check_plugins preview …` (Task 1); `mo2::bsatk`, `mo2-dds-header`, `lz4`, `ZLIB` já instalados pelos alvos anteriores.
- Produces: `libpreview_base.so`, `libpreview_bsa.so`.

- [ ] **Step 1: Subtrees**

```bash
git subtree add --prefix deps/preview_base https://github.com/ModOrganizer2/modorganizer-preview_base master --squash
git subtree add --prefix deps/preview_bsa https://github.com/ModOrganizer2/modorganizer-preview_bsa master --squash
```

Confira os `git-subtree-split:` como na Task 2.

- [ ] **Step 2: Alvos**

Depois de `build_installer_bundle`:

```bash
# the preview plugins (Phase 4c.1): text, images and INI files, and the contents of
# BSA/BA2 archives (bsatk)
build_preview_base() {
    cmake_build preview_base "$ROOT/deps/preview_base"
    check_plugins preview "$INSTALL/bin/plugins/libpreview_base.so"
}

build_preview_bsa() {
    cmake_build preview_bsa "$ROOT/deps/preview_bsa"
    check_plugins preview "$INSTALL/bin/plugins/libpreview_bsa.so"
}
```

E as duas linhas em `deps/README.md`.

- [ ] **Step 3: Ver o `preview_bsa` falhar**

Run: `distrobox enter mo2-run -- ./build.sh --only preview_base --only preview_bsa --tests`
Expected: `preview_base` passa (`ok   libpreview_base.so`); `preview_bsa` FALHA na configuração com `Could not find a package configuration file provided by "boost_interprocess"` (ou equivalente).

- [ ] **Step 4: Patch do Boost**

Em `deps/preview_bsa/src/CMakeLists.txt`, troque `find_package(Boost CONFIG REQUIRED COMPONENTS thread interprocess)` por:

```cmake
if (WIN32)
	find_package(Boost CONFIG REQUIRED COMPONENTS thread interprocess)
else()
	# interprocess is header-only, and Fedora installs no CMake package for it
	find_package(Boost CONFIG REQUIRED COMPONENTS thread)
endif()
```

(use a indentação do arquivo). Se depois disso a compilação reclamar de cabeçalhos do Boost, acrescente no Linux `target_link_libraries(preview_bsa PRIVATE Boost::headers)` dentro de um `if (NOT WIN32)` depois do `target_link_libraries` existente.

Run de novo o comando do Step 3. Expected: `ok   libpreview_bsa.so: '…' [IPluginPreview]`. Outras falhas de Win32: princípio dos patches.

- [ ] **Step 5: Commits**

```bash
git checkout -- '*_en.ts' 2>/dev/null || true
git add deps/preview_bsa/src/CMakeLists.txt
git commit -m "deps/preview_bsa: Linux: Boost interprocess has no CMake package on Fedora"
git add build.sh deps/README.md
git commit -m "build: preview_base and preview_bsa plugins"
```

---

### Task 4: `bsa_extractor`

**Files:**
- Create (subtree): `deps/bsa_extractor/`
- Modify: `deps/bsa_extractor/src/CMakeLists.txt` (Boost no Linux)
- Modify: `build.sh`, `deps/README.md`

**Interfaces:**
- Consumes: `check_plugins plugin …` (Task 1).
- Produces: `libbsa_extractor.so`.

- [ ] **Step 1: Subtree**

```bash
git subtree add --prefix deps/bsa_extractor https://github.com/ModOrganizer2/modorganizer-bsa_extractor master --squash
```

- [ ] **Step 2: Alvo**

Depois de `build_preview_bsa`:

```bash
# bsa_extractor (Phase 4c.1): offers to extract the BSA archives of a mod when it is
# installed (bsatk)
build_bsa_extractor() {
    cmake_build bsa_extractor "$ROOT/deps/bsa_extractor"
    check_plugins plugin "$INSTALL/bin/plugins/libbsa_extractor.so"
}
```

E a linha em `deps/README.md`.

- [ ] **Step 3: Ver falhar e corrigir**

Run: `distrobox enter mo2-run -- ./build.sh --only bsa_extractor --tests`
Expected: FALHA na configuração (Boost `interprocess`).

Em `deps/bsa_extractor/src/CMakeLists.txt`, o mesmo patch da Task 3 Step 4 (com `bsa_extractor` no `target_link_libraries` se for preciso). Run de novo. Expected: `ok   libbsa_extractor.so: '…' [] settings: …` (sem interface extra: é um `IPlugin`).

- [ ] **Step 4: Commits**

```bash
git checkout -- '*_en.ts' 2>/dev/null || true
git add deps/bsa_extractor/src/CMakeLists.txt
git commit -m "deps/bsa_extractor: Linux: Boost interprocess has no CMake package on Fedora"
git add build.sh deps/README.md
git commit -m "build: bsa_extractor plugin"
```

---

### Task 5: `diagnose_basic`

**Files:**
- Create (subtree): `deps/diagnose_basic/`
- Modify: `deps/diagnose_basic/src/diagnosebasic.cpp`
- Modify: `build.sh`, `deps/README.md`

**Interfaces:**
- Consumes: `check_plugins diagnose …` e a coluna `settings:` do `plugin_load` (Task 1).
- Produces: `libdiagnose_basic.so` sem a configuração `check_fileattributes` no Linux.

- [ ] **Step 1: Subtree e alvo com a checagem (teste que falha)**

```bash
git subtree add --prefix deps/diagnose_basic https://github.com/ModOrganizer2/modorganizer-diagnose_basic master --squash
```

Depois de `build_bsa_extractor`:

```bash
# diagnose_basic (Phase 4c.1): the basic checks of Mod Organizer (overwrite, error log,
# fonts, missing masters, ...); its check of NTFS file attributes is Windows-only (spec
# of Phase 4c.1, section 1.2), so its setting must not be offered on Linux
build_diagnose_basic() {
    cmake_build diagnose_basic "$ROOT/deps/diagnose_basic"
    check_plugins diagnose "$INSTALL/bin/plugins/libdiagnose_basic.so"
    if [[ $TESTS == 1 ]]; then
        local out
        out="$("$BUILD_OUT/plugin-load/plugin_load" diagnose \
            "$INSTALL/bin/plugins/libdiagnose_basic.so")"
        [[ $out != *check_fileattributes* ]] ||
            die "diagnose_basic offers check_fileattributes on Linux"
    fi
}
```

E a linha em `deps/README.md`.

Run: `distrobox enter mo2-run -- ./build.sh --only diagnose_basic --tests`
Expected: FALHA — na compilação (`GetFileAttributes`, `FILE_ATTRIBUTE_*`, `DeviceIoControl` não declarados) ou, se o `win32compat` cobrir os tipos, no `die` da checagem.

- [ ] **Step 2: Patch**

Em `deps/diagnose_basic/src/diagnosebasic.cpp`:

1. Em `settings()`, envolva só o item de atributos:

```cpp
#ifdef _WIN32
         << PluginSetting("check_fileattributes",
                          tr("Warn when files have unwanted attributes"), false)
#endif
```

(o `<<` seguinte continua a cadeia; mantenha o resto idêntico).

2. Envolva as duas funções estáticas `checkFileAttributes()` e `fixFileAttributes()` inteiras em `#ifdef _WIN32` … `#endif`.

3. Em `bool DiagnoseBasic::fileAttributes(const QString& executable) const`, logo depois da `{`:

```cpp
#ifndef _WIN32
  // NTFS file attributes don't exist on Linux file systems and Wine doesn't write them
  // (spec of Phase 4c.1, section 1.2): nothing to check
  Q_UNUSED(executable);
  return true;
#else
```

e um `#endif` logo antes da `}` final da função.

4. Se ainda houver chamadas a `checkFileAttributes`/`fixFileAttributes` fora dessa função (ex.: um "conserto guiado" — procure com `grep -n "ileAttributes" deps/diagnose_basic/src/*`), envolva o trecho que as usa em `#ifdef _WIN32` da mesma forma, sem mudar a lógica do Windows.

5. Outras falhas de compilação por Win32: princípio dos patches.

Run de novo. Expected: `ok   libdiagnose_basic.so: '…' [IPluginDiagnose] settings: check_errorlog, check_overwrite, check_font, check_conflict, check_missingmasters, check_alternategames, ow_ignore_empty, ow_ignore_log` (sem `check_fileattributes`) e nenhum `die`.

- [ ] **Step 3: Visão Windows**

```bash
B=$(git log -1 --grep 'git-subtree-dir: deps/diagnose_basic$' --format=%H)
f=deps/diagnose_basic/src/diagnosebasic.cpp
diff <(unifdef -t -D_WIN32 -DWIN32 "$f" || true) <(git show "$B:$f" | unifdef -t -D_WIN32 -DWIN32 || true)
```

(na distrobox, com `unifdef` instalado; `B` é o commit do squash, cujo conteúdo é o upstream — se o `git show` não achar o arquivo nesse commit, use o commit de merge do subtree add.) Expected: sem saída (só a formatação/comentários dentro de `#ifndef _WIN32` somem na visão Windows). Repita para todo arquivo alterado.

- [ ] **Step 4: Commits**

```bash
git checkout -- '*_en.ts' 2>/dev/null || true
git add deps/diagnose_basic/src
git commit -m "deps/diagnose_basic: Linux: no NTFS file attribute check"
git add build.sh deps/README.md
git commit -m "build: diagnose_basic plugin, without check_fileattributes on Linux"
```

---

### Task 6: `check_fnis`

**Files:**
- Create (subtree): `deps/check_fnis/`
- Modify: `deps/check_fnis/src/checkfnis.cpp`
- Modify: `build.sh`, `deps/README.md`

**Interfaces:**
- Consumes: `check_plugins plugin …` (Task 1).
- Produces: `libcheck_fnis.so`.

- [ ] **Step 1: Subtree, alvo e ver falhar**

```bash
git subtree add --prefix deps/check_fnis https://github.com/ModOrganizer2/modorganizer-check_fnis master --squash
```

Depois de `build_diagnose_basic`:

```bash
# check_fnis (Phase 4c.1): before the game runs, offers to run FNIS when its data changed;
# FNIS (a Windows program) runs through IOrganizer::startApplication() like Run
build_check_fnis() {
    cmake_build check_fnis "$ROOT/deps/check_fnis"
    check_plugins plugin "$INSTALL/bin/plugins/libcheck_fnis.so"
}
```

E a linha em `deps/README.md`. Run: `distrobox enter mo2-run -- ./build.sh --only check_fnis --tests`
Expected: FALHA na compilação (`Windows.h: No such file or directory`).

- [ ] **Step 2: Patch**

Em `deps/check_fnis/src/checkfnis.cpp`, troque `#include <Windows.h>  // for DWORD, HANDLE, INVALID_HANDLE_VALUE` por:

```cpp
#ifdef _WIN32
#include <Windows.h>  // for DWORD, HANDLE, INVALID_HANDLE_VALUE
#endif
```

(no Linux os tipos vêm do `win32compat` do `uibase`, pelos cabeçalhos `uibase/…` já incluídos). Run de novo. Expected: `ok   libcheck_fnis.so: '…' [] settings: …`. Outras falhas: princípio dos patches. Conferência `unifdef` como na Task 5 Step 3.

- [ ] **Step 3: Commits**

```bash
git checkout -- '*_en.ts' 2>/dev/null || true
git add deps/check_fnis/src
git commit -m "deps/check_fnis: Linux: Windows.h only on Windows"
git add build.sh deps/README.md
git commit -m "build: check_fnis plugin"
```

---

### Task 7: `tool_inieditor` e `tool_inibakery`

**Files:**
- Create (subtree): `deps/tool_inieditor/`, `deps/tool_inibakery/`
- Modify: `deps/tool_inieditor/src/inieditor.cpp`
- Modify: `build.sh`, `deps/README.md`

**Interfaces:**
- Consumes: `check_plugins tool …`, `check_plugins filemapper …` (Task 1); `MOBase::shell::Open(const QString&)` (`uibase/utility.h`, namespace `shell`).
- Produces: `libinieditor.so`, `libinibakery.so`.

- [ ] **Step 1: Subtrees, alvos e ver falhar**

```bash
git subtree add --prefix deps/tool_inieditor https://github.com/ModOrganizer2/modorganizer-tool_inieditor master --squash
git subtree add --prefix deps/tool_inibakery https://github.com/ModOrganizer2/modorganizer-tool_inibakery master --squash
```

Depois de `build_check_fnis`:

```bash
# the INI tools (Phase 4c.1): the INI Editor (Tools menu; its external mode opens the
# INI files with the desktop's handler on Linux) and the INI Bakery, which maps the
# INI files of the profile into the virtual file system
build_tool_inieditor() {
    cmake_build tool_inieditor "$ROOT/deps/tool_inieditor"
    check_plugins tool "$INSTALL/bin/plugins/libinieditor.so"
}

build_tool_inibakery() {
    cmake_build tool_inibakery "$ROOT/deps/tool_inibakery"
    check_plugins filemapper "$INSTALL/bin/plugins/libinibakery.so"
}
```

E as duas linhas em `deps/README.md`. Run: `distrobox enter mo2-run -- ./build.sh --only tool_inieditor --only tool_inibakery --tests`
Expected: `tool_inieditor` FALHA na compilação (`ShellExecuteW`/`SW_SHOWNORMAL` não declarados); `tool_inibakery` pode passar já (`ok   libinibakery.so: '…' [IPluginFileMapper]`) — se falhar, princípio dos patches.

- [ ] **Step 2: Patch do `inieditor`**

Em `deps/tool_inieditor/src/inieditor.cpp`, troque a chamada

```cpp
      ::ShellExecuteW(nullptr,
                      m_MOInfo->pluginSetting(name(), "associated").toBool() ? L"open"
                                                                             : L"edit",
                      ToWString(fileName).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
```

por

```cpp
#ifdef _WIN32
      ::ShellExecuteW(nullptr,
                      m_MOInfo->pluginSetting(name(), "associated").toBool() ? L"open"
                                                                             : L"edit",
                      ToWString(fileName).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
      // the desktop's handler of the file (xdg-open; in the mo2-run box it opens on the
      // host); Linux has no separate "edit" verb, so "associated" has no effect here
      MOBase::shell::Open(fileName);
#endif
```

(se `shell::Open` não for achado, inclua `<uibase/utility.h>` — já incluído no upstream — e confira o namespace; `MOBase::shell::Open` devolve um `Result` que pode ser ignorado, como os demais usos no app, ou registrado com `log::warn` se falhar: siga o padrão de `deps/modorganizer` — `grep -rn "shell::Open(" deps/modorganizer/src | head`.)

Run de novo. Expected: `ok   libinieditor.so: '…' [IPluginTool] settings: …` e `ok   libinibakery.so: …`. Conferência `unifdef` como na Task 5 Step 3.

- [ ] **Step 3: Commits**

```bash
git checkout -- '*_en.ts' 2>/dev/null || true
git add deps/tool_inieditor/src
git commit -m "deps/tool_inieditor: Linux: open INI files with the desktop's handler"
git add build.sh deps/README.md
git commit -m "build: tool_inieditor and tool_inibakery plugins"
```

(se `tool_inibakery` precisou de patch, commit próprio `deps/tool_inibakery: Linux: …` antes do de `build:`.)

---

### Task 8: Smoke — todo plugin instalado é carregado pelo MO2

**Files:**
- Modify: `tests/smoke/modorganizer-smoke.sh`

**Interfaces:**
- Consumes: os `.so` de `install/bin/plugins` (Tasks 2–7); a linha de log `loaded plugin '<nome>@<versão>' from '<arquivo>' - [<interfaces>]` de `PluginContainer` (`deps/modorganizer/src/plugincontainer.cpp`).

- [ ] **Step 1: Checagem**

No passo da janela principal (função que procura `displaying main window`), depois do bloco que espera o processo terminar e antes do bloco "hermetic: nothing written in the real HOME", acrescente:

```bash
    # every plugin of bin/plugins was loaded by the application (PluginContainer logs
    # "loaded plugin '<name>@<version>' from '<file>' - [...]"), and none failed: a
    # plugin can load in QPluginLoader (tests/plugin-load) and still fail its init()
    local so missing=()
    for so in "$INSTALL"/bin/plugins/*.so; do
        grep -qF "from '$(basename "$so")'" "$instance"/logs/mo_interface.log ||
            missing+=("$(basename "$so")")
    done
    ((${#missing[@]} == 0)) || fail "plugins not loaded by the application: ${missing[*]}"
    if grep -E "failed to load plugin|failed to initialize|failed to load \(may be outdated\)" \
        "$instance"/logs/mo_interface.log; then
        fail "a plugin failed to load (see above)"
    fi
    echo "all $(ls "$INSTALL"/bin/plugins/*.so | wc -l) plugins loaded"
```

(confira no script os nomes das variáveis `$INSTALL` e `$instance` e que o nível de log do teste é debug — o script já depende de `displaying main window`, que é debug.) Atualize o comentário do topo do script (lista do que o teste confere) com uma linha sobre isso.

- [ ] **Step 2: Rodar**

Run: `distrobox enter mo2-run -- ./build.sh --tests` (completo; os plugins novos precisam estar instalados).
Expected: `all 28 plugins loaded` (16 jogos + 3 instaladores antigos + 9 novos; confira com `ls install/bin/plugins/*.so | wc -l`), `smoke test passed`, e todos os outros testes como antes. Se um plugin antigo ou novo não aparecer como carregado, isso é um achado: investigue a causa (systematic-debugging) e reporte antes de mexer no teste.

- [ ] **Step 3: Commit**

```bash
git checkout -- '*_en.ts' 2>/dev/null || true
git add tests/smoke/modorganizer-smoke.sh
git commit -m "tests: smoke: every installed plugin is loaded by the application"
```

---

### Task 9: Documentação, checklist e PR

**Files:**
- Create: `docs/testing/phase4c1-manual-checklist.md`
- Modify: `README.md`, `STATE.md`, `ROADMAP.md`

- [ ] **Step 1: Checklist manual**

`docs/testing/phase4c1-manual-checklist.md`: título `# Fase 4c.1 — checklist manual (Bazzite)`, link para a spec, **Ambiente** como o da 4b (MO2 compilado na distrobox `mo2-run`, iniciado pelo box, instância de Skyrim SE das fases anteriores), e a tabela `| # | Passo | Esperado | Resultado |` com os 7 itens da spec §4 (manual), um por linha, e no fim "**Ao terminar:** …" e `## Observações`, como os checklists anteriores.

- [ ] **Step 2: README, STATE e ROADMAP**

- `README.md`: na lista de plugins/limitações, mencione que BAIN, bundle, pré-visualização (texto, imagem, BSA), extração de BSA, diagnósticos básicos, checagem do FNIS e as ferramentas de INI estão disponíveis; a checagem de atributos NTFS do `diagnose_basic` não existe no Linux.
- `ROADMAP.md`: na tabela "Visão geral", troque a linha `4c` por três: `4c.1 | Plugins C++ restantes | installer_bain, installer_bundle, preview_base, preview_bsa, bsa_extractor, diagnose_basic, check_fnis, tool_inieditor, tool_inibakery | Todas as funcionalidades | 🟡 Em andamento`, `4c.2 | Pendências da Fase 2 | as 6 pendências adiadas do teste manual e da revisão final da Fase 2 | — | ⚪ Não iniciada`, `4c.3 | Ferramentas pelo VFS | BodySlide, Nemesis/Pandora, xEdit de outros jogos | Ferramentas pelo VFS (critério 6) | ⚪ Não iniciada`; na seção "## Fase 4 — Ecossistema", a linha `Fase 4c.1 — Spec: … · Plano: … · Checklist manual: …` com os links.
- `STATE.md`: "**Fase atual:** 4c.1 — Plugins C++ restantes · **Status:** 🟡 implementação concluída; revisão final, CI e teste manual pendentes · **Anterior:** 4b — LOOT (🟡 PR #3, teste manual pendente)"; "**Branch da Fase 4c.1:** `fase-4c1-plugins` (a partir de `fase-4b-loot`)"; seção "## Fase 4c.1 — progresso" no topo com a tabela das Tasks 1–10 e os commits (`git log --oneline fase-4b-loot..`), Task 10 "⚪ pendente (teste manual)"; "## Limitações conhecidas da Fase 4c.1": sem checagem de atributos NTFS; `tool_inieditor` externo ignora "associated"; pré-visualização de DDS só com plugin de imagem da Qt para DDS; FNIS só testado se o usuário usar; "Decisões tomadas": a divisão da 4c em 4c.1–4c.3 com PRs em sequência (2026-10-20) e as decisões da spec §1.2. Em "Próximos passos": teste manual da 4b e da 4c.1, merges na ordem, depois 4c.2.

- [ ] **Step 3: Commit**

```bash
git add docs/testing/phase4c1-manual-checklist.md README.md STATE.md ROADMAP.md
git commit -m "docs: Phase 4c.1 checklist, state and roadmap"
```

- [ ] **Step 4: PR (o usuário pediu um PR por fase, 2026-10-20)**

Depois da revisão final do branch (controlador):

```bash
git commit --allow-empty -m "ci: Phase 4c.1 checkpoint [windows-ci]"
git push -u origin fase-4c1-plugins
gh pr create --base fase-4b-loot --head fase-4c1-plugins --title "Fase 4c.1 — Plugins C++ restantes" --body-file <arquivo>
```

Corpo no formato do PR #3 (o que entra, testes, pendências, spec e plano, "Não fazer merge antes do teste manual e do merge da 4b"), terminando com as linhas de atribuição de PR da sessão. Acompanhe o CI (`gh run list --branch fase-4c1-plugins`): Linux verde com as 9 linhas `ok   lib….so` e `all N plugins loaded`; Windows verde.

---

### Task 10: Teste manual com o usuário e fechamento

**Files:**
- Modify: `docs/testing/phase4c1-manual-checklist.md`, `STATE.md`, `ROADMAP.md`, spec (status)

- [ ] **Step 1:** Quando o usuário for testar (depois da 4b), entregar o checklist com os comandos do ambiente dele.
- [ ] **Step 2:** Cada falha vira correção com systematic-debugging, teste automático quando possível, commit próprio e linha em "Observações" e em "Decisões tomadas".
- [ ] **Step 3:** Com o checklist aprovado e o CI verde: spec "aprovada; implementada (teste manual aprovado em <data>)"; ROADMAP 4c.1 "✅ Concluída (<data>)"; STATE atualizado; merge só com a autorização do usuário, depois do merge da 4b (o PR é redirecionado ao `main`).

## Observações

- As Tasks 2–7 acrescentam alvos ao `ALL_TARGETS` de uma vez na Task 2; os alvos ainda sem função são pulados ("not implemented yet") até a tarefa deles, então o build completo continua passando entre as tarefas.
- A conferência `unifdef` compara a visão Windows do nosso arquivo com a do upstream; ela não substitui o CI Windows, que continua usando os plugins do upstream (spec §2).
