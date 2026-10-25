# Fase 5 — Distribuição — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** O MO2 do Linux sai como um AppImage x86_64 que roda direto no host, se atualiza sozinho (zsync/Gear Lever e o `SelfUpdater`), é gerado pelo CI a partir de uma tag e tem documentação de usuário em inglês e português (spec §1).

**Architecture:** O `./build.sh` continua produzindo `install/`. Um script novo, `build/appimage/build-appimage.sh`, monta um AppDir a partir dele (com `linuxdeploy` + plugin Qt, Python 3.14 + PyQt6 e libstdc++ embutidos), separa os símbolos e gera AppImage + `.zsync` + `.debug.tar.zst` + `SHA256SUMS`. No MO2, um módulo Linux novo (`appimage_linux`) captura `APPIMAGE`/`APPDIR` e restaura o ambiente do host no início do `main` (spec §3.3). O `SelfUpdater` ganha no Linux a escolha do asset, a conferência do SHA-256, a troca atômica e o reinício por `execv` (spec §3.4). Um workflow de release gera rascunhos.

**Tech Stack:** C++23 (GCC 16), Qt 6.11 (QtWebEngine), CMake + Ninja, GoogleTest, bash, `linuxdeploy` + `linuxdeploy-plugin-qt`, `appimagetool` (runtime type 2 estático), zsync, `objcopy`/`strip`, `zstd`, GitHub Actions, containers `fedora:44`/`archlinux`/`ubuntu`.

**Spec:** `docs/superpowers/specs/2026-10-24-phase5-distribuicao-design.md` (ler inteira antes de cada tarefa; §N são seções dela).

## Global Constraints

- Worktree `../ModOrganizerLinux-5` (`/var/home/fmoura-server/Documentos/Applications/ModOrganizerLinux-5`), branch `fase-5-distribuicao` (a partir de `fase-4e-bsa` em `398dc5f0`). **Nunca** compilar, instalar ou rodar testes no checkout principal nem nos outros worktrees.
- Versão do port: `2.5.2.N` (tag `v2.5.2.N`, N ≥ 1); builds locais continuam `2.5.2` (spec §1.2).
- Nome dos arquivos do release: `ModOrganizer-<versão>-x86_64.AppImage`, `….AppImage.zsync`, `ModOrganizer-<versão>-x86_64.debug.tar.zst`, `SHA256SUMS`.
- Informação de atualização embutida: `gh-releases-zsync|FM0Ura|ModOrganizerLinux|latest|ModOrganizer-*-x86_64.AppImage.zsync`.
- Repositório do `SelfUpdater` no Linux: `FM0Ura/ModOrganizerLinux`; Windows continua `Modorganizer2/modorganizer`.
- O CI **só cria releases rascunho** (`gh release create --draft`); nunca publica.
- `umu-run`, Proton, Steam e FUSE **não** vão no AppImage (spec §1.2).
- O AppImage nunca carrega Python, `site-packages` ou Qt do host (spec §2.3).
- Código Windows idêntico: mudanças em fontes compartilhados ficam em `#ifndef _WIN32`/`#ifdef _WIN32`.
- `./build.sh`, a distrobox `mo2-run` e o `linux.yml` continuam funcionando como hoje.
- Ferramentas de terceiros baixadas em versão fixada, com SHA-256 conferido no script.
- Commits: `deps/modorganizer: Linux: …`, `deps/plugin_python: Linux: …` (só arquivos do subtree), `build: …`, `tests: …`, `ci: …`, `docs: …`. Todo commit termina com:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  ```
- Mensagens de log e de interface em inglês (traduzíveis com `tr()`); comentários no estilo do arquivo. `*_en.ts` regenerados não são commitados. Nada de push sem autorização do usuário.

## Ambiente de execução

No Bazzite, na distrobox `mo2-run`, da raiz do worktree, com um TMPDIR só deste worktree:

```bash
cd /var/home/fmoura-server/Documentos/Applications/ModOrganizerLinux-5
mkdir -p /tmp/mo2t-5
distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-5 ./build.sh --tests
```

O controlador roda o primeiro build completo antes da Task 1. Depois: `./build.sh --only <alvo> --tests`. Os smoke tests de containers (Task 6) rodam no **host** com `podman`. Nunca `pkill -f`/`pgrep -f` com padrão da própria linha de comando.

## Review Focus

1. **Host com libstdc++ mais nova que a do Fedora 44** (Arch quando o GCC passar do 16): o AppImage tem de usar a do host, senão o Mesa do host falha dentro do processo. Teste: o `AppRun` escolhe a mais nova (Task 5, `tests/appimage/test-apprun.sh`).
2. **`$APPIMAGE` num diretório sem permissão de escrita** (ex.: `/opt`) ou num sistema de arquivos diferente do temporário: a atualização não pode deixar meio arquivo nem perder o antigo. Teste: `ReplaceAppImage.ReadOnlyDirectoryKeepsOld` (Task 8).
3. **Variável que não existia antes do AppRun** (ex.: `LD_LIBRARY_PATH` ausente no host): restaurar significa **remover**, não deixar vazia (`LD_LIBRARY_PATH=` vazio quebra programas). Teste: `HostEnvironment.RemovesVariablesThatDidNotExist` (Task 3).
4. **Nome do AppImage com espaço ou acento** (Gear Lever renomeia; `~/Applications/Mod Organizer.AppImage`): o `.desktop` do `nxm://` e o `execv` usam o caminho intacto. Teste: `NxmHandler.DesktopFileQuotesAppImageWithSpaces` (Task 3) e o `execv` recebe o caminho sem passar por shell (Task 8).
5. **Release sem `SHA256SUMS` ou sem o AppImage** (rascunho incompleto publicado à mão): o MO2 não atualiza e diz por quê, sem baixar nada às cegas. Teste: `SelfUpdateLinux.MissingChecksumRefusesUpdate` (Task 8).

---

### Task 1: Queda ao reiniciar no mesmo processo (spec §3.8)

**Files:**
- Modify: `deps/modorganizer/src/main.cpp` (antes de `MOApplication app(argc, argv);`)
- Modify: `tests/smoke/modorganizer-smoke.sh` (passo novo)
- Modify: `docs/testing/phase5-manual-checklist.md` (criar; item da queda)

**Interfaces:**
- Produces: nada consumido por outras tarefas.

- [ ] **Step 1: Reproduzir antes da correção.** No box, com o `install/` deste worktree, abrir uma instância, **Change Game** (ou abrir outra instância pelo gerenciador) e depois fechar o MO2. Esperado: SIGSEGV com `OzoneUtilQt::getQOpenGLContext` na pilha (`coredumpctl info`). Anotar o resultado no relatório da tarefa.
- [ ] **Step 2: Teste no smoke.** Acrescentar ao `modorganizer-smoke.sh` um passo 4: com a instância `smoke` pronta, iniciar o MO2 com a variável de teste `MO2_SMOKE_RESTART_ONCE=1`, que faz o `MOApplication` (Linux, só quando a variável existe) devolver `RestartExitCode` logo depois de mostrar a janela principal uma vez; o teste espera duas linhas `displaying main window` no log e então SIGTERM; falha com crash dump ou saída por sinal diferente de TERM. Rodar sem a correção: esperado FAIL (SIGSEGV no segundo `aboutToQuit`). Se o caminho de teste não reproduzir a queda headless (offscreen sem WebEngine com GL), registrar no relatório e ficar com o Step 1 + checklist manual.
- [ ] **Step 3: Corrigir.** Em `main.cpp`, `#ifndef _WIN32`, `QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);` antes de construir o `MOApplication`, com um comentário de duas linhas citando o STATE (testes de 2026-10-24, item 1).
- [ ] **Step 4: Verificar.** `./build.sh --only modorganizer --tests` passa (inclui o passo 4 do smoke) e o Step 1 repetido não cai. Se ainda cair, **parar e voltar ao usuário** (spec §3.8).
- [ ] **Step 5: Commit** — `deps/modorganizer: Linux: share OpenGL contexts so restarts do not crash` e `tests: smoke: restart once in the same process`.

### Task 2: Versão vinda do CI (spec §3.4, "Versões")

**Files:**
- Modify: `deps/modorganizer/src/CMakeLists.txt:219-236` (bloco da versão)
- Modify: `build.sh` (`build_modorganizer`)
- Create: `deps/modorganizer/tests/test_versionorder.cpp`

**Interfaces:**
- Produces: cache CMake `MO2_VERSION_OVERRIDE` (string, vazio por padrão); variável de ambiente `MO2_VERSION` lida pelo `build.sh` e repassada como `-DMO2_VERSION_OVERRIDE=$MO2_VERSION`. Com override, `MO2_VERSION_PRERELEASE=0`.

- [ ] **Step 1: Teste da ordenação** (`test_versionorder.cpp`, links do `organizer_tests` já incluem o `uibase`):

```cpp
TEST(VersionOrder, PortNumberOrdersAfterUpstream)
{
  using MOBase::Version;
  const auto v = [](const char* s) { return Version::parse(s, Version::ParseMode::MO2); };
  EXPECT_LT(v("2.5.2"), v("2.5.2.1"));
  EXPECT_LT(v("2.5.2.1"), v("2.5.2.2"));
  EXPECT_LT(v("2.5.2.9"), v("2.5.2.10"));
  EXPECT_LT(v("2.5.2.10"), v("2.5.3.1"));
  EXPECT_EQ(v("2.5.2.1").string(), "2.5.2.1");
}
```

- [ ] **Step 2:** `./build.sh --only modorganizer --tests` — esperado PASS já (o parser aceita o 4º número; se falhar, parar e reportar: a decisão de formato da spec não vale).
- [ ] **Step 3: Override.** No CMake, se `MO2_VERSION_OVERRIDE` não é vazio, ele vira `_organizer_version` e `_organizer_prerelease` vira 0; validar com regex `^[0-9]+\.[0-9]+\.[0-9]+(\.[0-9]+)?$` (`FATAL_ERROR` se não casar). No `build.sh`, `build_modorganizer` acrescenta `-DMO2_VERSION_OVERRIDE="${MO2_VERSION:-}"` (vazio limpa um cache antigo).
- [ ] **Step 4: Verificar.** `MO2_VERSION=2.5.2.7 ./build.sh --only modorganizer` e `install/bin/ModOrganizer --version` (ou o log `Mod Organizer version`) mostra `2.5.2.7`; sem `MO2_VERSION`, volta a `2.5.2`. `MO2_VERSION=2.5.2-linux.1` falha no configure.
- [ ] **Step 5: Commit** — `deps/modorganizer: Linux: version override from the build` e `build: pass MO2_VERSION to the application`.

### Task 3: Ambiente do host e identidade do AppImage (spec §3.3)

**Files:**
- Create: `deps/modorganizer/src/linux/appimage_linux.h`, `deps/modorganizer/src/linux/appimage_linux.cpp`
- Modify: `deps/modorganizer/src/CMakeLists.txt` (`_organizer_env_sources` += `linux/appimage_linux.cpp`)
- Modify: `deps/modorganizer/src/main.cpp` (`run()`, logo depois de `setExceptionHandlers()`)
- Modify: `deps/modorganizer/src/linux/nxmhandler_linux.cpp:121` (usar `appimage::info()` em vez de `qEnvironmentVariable("APPIMAGE")`)
- Create: `deps/modorganizer/tests/test_appimage.cpp`; Modify: `deps/modorganizer/tests/test_nxmhandler.cpp`

**Interfaces:**
- Produces (namespace `appimage`, `appimage_linux.h`):
  - `struct Info { QString appImage; QString appDir; bool active() const { return !appImage.isEmpty(); } };`
  - `const Info& info();` — o valor capturado por `captureAndRestore`; vazio fora do AppImage.
  - `std::map<std::string, std::optional<std::string>> hostEnvironment(const std::map<std::string, std::string>& current);` — função pura: para cada `MO2_APPIMAGE_ORIG_<VAR>` em `current`, devolve `<VAR>` → valor original, ou `std::nullopt` se o valor guardado é o marcador `MO2_APPIMAGE_UNSET`; também devolve `std::nullopt` para `APPIMAGE`, `APPDIR`, `ARGV0`, `OWD` e para todas as `MO2_APPIMAGE_ORIG_*`. Variáveis sem marcador não aparecem (ficam como estão).
  - `void captureAndRestore();` — lê `environ`, preenche `info()` (de `APPIMAGE`/`APPDIR`), aplica `hostEnvironment` com `setenv`/`unsetenv`. Sem `APPIMAGE` no ambiente, não faz nada.
  - `std::map<std::string, std::string> currentEnvironment();` — `environ` como mapa (para o `captureAndRestore` e os testes).
- Contrato com o `AppRun` (Task 5): para cada variável `V` que o AppRun muda, exporta `MO2_APPIMAGE_ORIG_V` com o valor anterior ou `MO2_APPIMAGE_UNSET`.

- [ ] **Step 1: Testes** (`test_appimage.cpp`):
  - `HostEnvironment.RestoresChangedVariables`: `{LD_LIBRARY_PATH=/app/lib, MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH=/host/lib}` → `LD_LIBRARY_PATH` = `"/host/lib"`.
  - `HostEnvironment.RemovesVariablesThatDidNotExist`: `MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH=MO2_APPIMAGE_UNSET` → `LD_LIBRARY_PATH` = `nullopt`.
  - `HostEnvironment.RemovesAppImageVariables`: `APPIMAGE`, `APPDIR`, `ARGV0`, `OWD` e o próprio `MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH` → `nullopt`.
  - `HostEnvironment.KeepsUnmarkedVariables`: `HOME=/h` sem marcador → `HOME` ausente do resultado.
  - `HostEnvironment.OutsideAppImageIsEmpty`: sem `APPIMAGE` e sem marcadores → resultado vazio.
  - `CaptureAndRestore.AppliesToProcess` (com `setenv` num teste isolado, restaurando no fim): depois de `captureAndRestore()`, `getenv("APPIMAGE") == nullptr`, `info().appImage == "/x/Mod Organizer.AppImage"`, e um filho (`posix_spawn` de `/usr/bin/env`) não mostra `MO2_APPIMAGE_`.
  - `NxmHandler.DesktopFileQuotesAppImageWithSpaces` (`test_nxmhandler.cpp`): `desktopFileContent` com `appImage = "/home/u/Applications/Mod Organizer ç.AppImage"` gera `Exec=` com o caminho entre aspas do formato `.desktop` e `%u`.
- [ ] **Step 2:** `./build.sh --only modorganizer --tests` — FAIL (símbolos inexistentes).
- [ ] **Step 3: Implementar** `appimage_linux.cpp` conforme a interface; em `main.cpp` (`#ifndef _WIN32`), `appimage::captureAndRestore();` antes de `cl.process`. No `nxmhandler_linux.cpp`, `e.appImage = appimage::info().appImage;`.
- [ ] **Step 4:** testes PASS; smoke test do `build.sh` continua PASS.
- [ ] **Step 5: Commit** — `deps/modorganizer: Linux: restore the host environment inside an AppImage`.

### Task 4: Python embutido (spec §3.2)

**Files:**
- Modify: `deps/plugin_python/src/runner/syspath.cpp` / `syspath.h` (função nova)
- Modify: `deps/plugin_python/src/runner/pythonrunner.cpp:122-145` (ramo Linux)
- Modify: `deps/plugin_python/src/proxy/CMakeLists.txt` (RUNPATH do proxy alcança `usr/lib` do AppDir: `$ORIGIN/../../lib`, além do atual)
- Test: o teste unitário do runner que já cobre `filterSysPath` (mesmo arquivo de testes do `syspath`)

**Interfaces:**
- Produces: `std::optional<std::filesystem::path> bundledPythonHome(const std::filesystem::path& pluginFolder);` — devolve `pluginFolder/../..` (normalizado) se existir `<isso>/lib/python3.<minor>/os.py` (versão de `PY_MINOR_VERSION`), senão `nullopt`. O `pluginFolder` é o pai de `plugin_python/libs` (o primeiro de `paths`).
- Consumes: layout do AppDir da Task 5 (`usr/bin/plugins`, `usr/lib/python3.14`, `usr/lib/libpython3.14.so.1.0`).

- [ ] **Step 1: Testes:** `BundledPythonHome.FoundInAppDirLayout` (diretório temporário com `usr/bin/plugins` e `usr/lib/python3.14/os.py` → devolve `usr`); `BundledPythonHome.AbsentInDevelopmentTree` (sem `lib/python3.14` → `nullopt`).
- [ ] **Step 2:** rodar — FAIL.
- [ ] **Step 3: Implementar.** No ramo Linux de `initialize`, se `bundledPythonHome(paths[0].parent_path().parent_path())` existe, `PyConfig_SetBytesString(&config, &config.home, …)` antes de `py::initialize_interpreter`; o filtro de `sys.path` (`systemSysPathRoots`, raiz `sys.base_prefix`) fica igual e passa a apontar para o AppDir. Fora do AppDir, nada muda.
- [ ] **Step 4:** testes PASS; `./build.sh --only plugin_python --tests` e o smoke (Python carregado) PASS.
- [ ] **Step 5: Commit** — `deps/plugin_python: Linux: use the Python of the AppImage when bundled`.

### Task 5: Montagem do AppImage, símbolos e licenças (spec §3.1, §3.5 símbolos, §3.7)

**Files:**
- Create: `build/appimage/build-appimage.sh` — `build-appimage.sh <install-dir> <out-dir>`; versão de `MO2_VERSION` (obrigatória).
- Create: `build/appimage/AppRun` (bash), `build/appimage/ModOrganizer.desktop` (com `MimeType=x-scheme-handler/nxm;` e `Exec=ModOrganizer %u`), `build/appimage/excludelist-extra` (se preciso)
- Create: `build/appimage/licenses/` (textos e `THIRD-PARTY-NOTICES.md`, inclusive a proveniência do `dxgiformat.h`)
- Create: `scripts/check-appdir.sh` — `check-appdir.sh <AppDir> <max-glibc>`
- Create: `tests/appimage/test-apprun.sh`
- Modify: `build/Containerfile` (pacotes: `zsync`, `zstd`, `file`, `patchelf`, `desktop-file-utils`, e o que o `linuxdeploy` exigir)
- Modify: `deps/README.md` (versões e hashes de `linuxdeploy`, plugin Qt, `appimagetool`/runtime)

**Interfaces:**
- Produces: em `<out-dir>`, os quatro arquivos da Global Constraints; o AppDir em `<out-dir>/AppDir` (mantido para o smoke). Layout: `usr/bin/{ModOrganizer,mo2-vfs,plugins/,loot/,resources/,stylesheets/,translations/,tutorials/,qt.conf}`, `usr/lib/{libuibase.so,libmo2-archive.so,7z.so,libpython3.14.so.1.0,python3.14/,libstdc++.so.6,libgcc_s.so.1,…Qt}`, `usr/plugins/` (Qt), `usr/libexec/QtWebEngineProcess`, `usr/share/licenses/ModOrganizer/`.
- Contrato do `AppRun` (consumido pela Task 3): ele só muda `LD_LIBRARY_PATH` (e nada mais, salvo necessidade registrada) e exporta `MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH`.

- [ ] **Step 1: `AppRun` e o teste dele** (`tests/appimage/test-apprun.sh`, sem AppImage: um AppDir falso com um `usr/bin/ModOrganizer` que imprime `env`). Regras do `AppRun`: compara o maior `GLIBCXX_3.4.N` de `usr/lib/libstdc++.so.6` com o da libstdc++ do host (`ldconfig -p`, primeira `libstdc++.so.6` x86-64); se a do host for **mais nova**, cria `${XDG_RUNTIME_DIR:-/tmp}/mo2-hostlibs-$UID/` com links para a `libstdc++.so.6` e a `libgcc_s.so.1` do host e põe esse diretório **na frente** de `LD_LIBRARY_PATH`, gravando `MO2_APPIMAGE_ORIG_LD_LIBRARY_PATH` (valor antigo ou `MO2_APPIMAGE_UNSET`); senão, não mexe em nada (a embutida vem pelo RUNPATH). Depois `exec "$APPDIR/usr/bin/ModOrganizer" "$@"`. Casos do teste: host mais novo → diretório na frente e marcador; host igual/mais velho → ambiente intacto e sem marcador; `LD_LIBRARY_PATH` inexistente → marcador `MO2_APPIMAGE_UNSET`.
- [ ] **Step 2:** rodar o teste — FAIL (sem `AppRun`); implementar; PASS.
- [ ] **Step 3: `build-appimage.sh`.** Baixa as ferramentas (versões e SHA-256 fixados no topo), copia `install/{bin,lib}` para `AppDir/usr/`, copia `libpython3.14.so.1.0`, `/usr/lib64/python3.14` (podado: sem `test/`, `idlelib/`, `tkinter/`, `turtledemo/`, `ensurepip/`, `__pycache__` fora os de `-O2`) e o PyQt6 do sistema para `usr/lib/python3.14/site-packages/PyQt6`; roda `linuxdeploy --appdir AppDir --executable … --plugin qt --custom-apprun build/appimage/AppRun --desktop-file … --icon-file …` com `QMAKE`/`EXTRA_QT_MODULES` para WebEngine, Wayland e xcb; acrescenta `libstdc++.so.6`/`libgcc_s.so.1` ao `usr/lib`; ajusta RUNPATH dos `.so` do PyQt6 para o `usr/lib` do AppDir (`patchelf`); copia as licenças.
- [ ] **Step 4: Símbolos.** Para cada ELF do AppDir com `.debug_info`: `objcopy --only-keep-debug f debug/<build-id>.debug`, `strip --strip-debug f`, `objcopy --add-gnu-debuglink`; empacotar `debug/` em `ModOrganizer-<versão>-x86_64.debug.tar.zst` organizado por `.build-id/xx/yyyy.debug` (layout que o `gdb` acha com `set debug-file-directory`).
- [ ] **Step 5: `scripts/check-appdir.sh`.** Falha se (a) algum ELF exige `GLIBC_` acima de `<max-glibc>` (`objdump -T`), (b) `ldd` de algum ELF resolve para fora do AppDir uma biblioteca que não está na lista permitida (glibc, `libGL*`/`libEGL*`/`libvulkan*`/`libgbm*`/`libdrm*`, `libX11*`/`libxcb*`/`libwayland-*`/`libxkbcommon*`, `libfontconfig`/`libfreetype`, `libdbus-1`; `libz` e todo o resto vêm do AppDir), (c) algum `.pth`, `sitecustomize.py` ou RUNPATH do AppDir aponta para `/usr/lib64/python*` ou `/usr/lib/python*` do host. `<max-glibc>` = a do Fedora 44 (`ldd --version` do container), registrada no README pela Task 9.
- [ ] **Step 6: `appimagetool`** com `-u "gh-releases-zsync|FM0Ura|ModOrganizerLinux|latest|ModOrganizer-*-x86_64.AppImage.zsync"` e runtime type 2 estático → AppImage + `.zsync`; `sha256sum` dos três arquivos em `SHA256SUMS`.
- [ ] **Step 7: Verificar** no box: `MO2_VERSION=2.5.2.1 build/appimage/build-appimage.sh install /tmp/mo2t-5/appimage` termina; `scripts/check-appdir.sh` PASS; no **host** do Bazzite, `/tmp/mo2t-5/appimage/ModOrganizer-2.5.2.1-x86_64.AppImage --help` imprime o uso; abrir de verdade mostra a seleção de instância e o painel sem "Python could not be loaded". Registrar o tamanho do AppImage.
- [ ] **Step 8: Commit** — `build: AppImage (linuxdeploy, Python, libstdc++, debug symbols)`, `tests: AppRun picks the newer libstdc++`.

### Task 6: Smoke test do AppImage em containers (spec §5.2)

**Files:**
- Modify: `tests/smoke/modorganizer-smoke.sh` (`MO2_SMOKE_APP`: comando a executar, padrão `$INSTALL/bin/ModOrganizer`)
- Create: `tests/appimage/run-distro-smoke.sh` — `run-distro-smoke.sh <AppImage> <image>`
- Create: `tests/appimage/distros.txt` — `fedora:44`, `archlinux:latest` e o Ubuntu escolhido

**Interfaces:**
- Consumes: AppImage da Task 5.
- Produces: `run-distro-smoke.sh` sai com 0/1; usado pela Task 9.

- [ ] **Step 1:** `MO2_SMOKE_APP` no smoke (e `INSTALL` apontando para `squashfs-root/usr` para a lista de plugins). Verificar que `./build.sh --tests` continua PASS.
- [ ] **Step 2: `run-distro-smoke.sh`:** `podman run --rm` (ou `docker`, o que existir) da imagem limpa, instala só o mínimo de runtime da distro (lista em `distros.txt`, por exemplo `libglvnd`, `fontconfig`, `libxkbcommon`, `nss` — o que o QtWebEngine exige do host fica **documentado**, Task 9), extrai com `--appimage-extract`, roda o smoke com `MO2_SMOKE_APP=squashfs-root/AppRun`, `QT_QPA_PLATFORM=offscreen`, e ainda `squashfs-root/usr/bin/loot/lootcli --help` e `squashfs-root/usr/bin/mo2-vfs --version` (ou o equivalente que exista).
- [ ] **Step 3: Escolher o Ubuntu:** o mais recente cuja glibc ≥ a máxima do Step 5 da Task 5; se nenhum, registrar em `distros.txt` e seguir com Fedora + Arch.
- [ ] **Step 4: Verificar** no host do Bazzite: `tests/appimage/run-distro-smoke.sh /tmp/mo2t-5/appimage/ModOrganizer-2.5.2.1-x86_64.AppImage <img>` PASS para cada imagem de `distros.txt`.
- [ ] **Step 5: Commit** — `tests: AppImage smoke test in clean containers`.

### Task 7: Dependências do host (spec §3.6)

**Files:**
- Create: `deps/modorganizer/src/linux/hostdeps_linux.h`, `…/hostdeps_linux.cpp` (em `_organizer_launch_sources`)
- Modify: `deps/modorganizer/src/linux/processrunner_linux.cpp:314-320` (usar a mensagem nova) e o ponto em que o `LinuxVfsConnector` inicia o `mo2-vfs`
- Test: `deps/modorganizer/tests/test_hostdeps.cpp`

**Interfaces:**
- Produces: `struct MissingHostTool { QString name; QString purpose; QString docAnchor; };` e `std::vector<MissingHostTool> missingHostTools(const std::function<QString(const QString&)>& findExecutable);` — confere `umu-run` (purpose: rodar programas Windows) e `fusermount3` (purpose: montar a pasta virtual dos mods); `QString missingHostToolsMessage(const std::vector<MissingHostTool>&);` com link para `docs/user/en/install.md#host-dependencies` no GitHub.
- A busca usa o `PATH` do host (o processo já está com o ambiente restaurado, Task 3); pela ponte `distrobox-host-exec`, a checagem é a do host (`vfsbridge::helperCommand("sh", "-c", "command -v …")` ou equivalente).

- [ ] **Step 1: Testes:** `HostDeps.NothingMissing`, `HostDeps.UmuRunMissing` (só `umu-run` falta → um item, `name == "umu-run"`), `HostDeps.MessageNamesEveryTool` (mensagem contém os dois nomes e o link).
- [ ] **Step 2:** FAIL; **Step 3:** implementar; ao clicar em **Run**, antes do `spawn`, se faltar algo, mostrar `missingHostToolsMessage` e não iniciar. **Step 4:** PASS + smoke PASS.
- [ ] **Step 5: Commit** — `deps/modorganizer: Linux: check umu-run and fusermount3 before running`.

### Task 8: Atualização pelo MO2 (spec §3.4)

**Files:**
- Create: `deps/modorganizer/src/linux/selfupdate_linux.h`, `…/selfupdate_linux.cpp` (em `_organizer_desktop_sources`)
- Modify: `deps/modorganizer/src/selfupdater.cpp` (ramos `#ifndef _WIN32` em `testForUpdate`, `startUpdate`, `installUpdate`)
- Modify: `deps/modorganizer/src/main.cpp` (`main()`: reexec pendente depois de `run()`)
- Create: `deps/modorganizer/tests/test_selfupdate.cpp`, `deps/modorganizer/tests/fixtures/selfupdate/release.json` (gravado do formato da API do GitHub)

**Interfaces:**
- Consumes: `appimage::info()` (Task 3); `hostEnvironment` já aplicado ao processo.
- Produces (namespace `selfupdate`):
  - `struct Asset { QString name; QUrl url; };`
  - `std::optional<Asset> appImageAsset(const QJsonObject& release);` — o asset cujo nome casa `^ModOrganizer-.+-x86_64\.AppImage$`.
  - `std::optional<Asset> checksumAsset(const QJsonObject& release);` — o asset `SHA256SUMS`.
  - `std::optional<QByteArray> expectedSha256(const QByteArray& sums, const QString& fileName);` — formato do `sha256sum` (`<hex>  <nome>`), hex minúsculo.
  - `enum class ReplaceError { None, NotWritable, ChecksumMismatch, IoError };`
  - `ReplaceError replaceAppImage(const QString& downloaded, const QString& target, const QByteArray& expectedSha256);` — confere o hash do `downloaded` (que deve estar no mesmo diretório do `target`), copia as permissões do `target`, `rename` atômico; em qualquer erro, `target` intacto e `downloaded` apagado.
  - `QString downloadPathFor(const QString& target);` — `<dir do target>/.<nome>.update-<pid>`.
  - `void scheduleReexec(const QString& appImage, const QStringList& args);` e `bool runPendingReexec();` — `execv` direto (sem shell) do caminho, com `argv[0]` = caminho; só retorna se o `execv` falhar (log de erro).

- [ ] **Step 1: Testes** (`test_selfupdate.cpp`):
  - `SelfUpdateLinux.PicksAppImageAsset` / `IgnoresZsyncAndDebug` (o `.zsync` e o `.debug.tar.zst` do fixture não são escolhidos).
  - `SelfUpdateLinux.MissingChecksumRefusesUpdate`: release sem `SHA256SUMS` → `checksumAsset` = `nullopt` (e o `SelfUpdater` mostra erro sem baixar; coberto pelo Step 3).
  - `SelfUpdateLinux.ExpectedShaFromSums`: acha a linha certa entre três; nome ausente → `nullopt`.
  - `ReplaceAppImage.ReplacesAtomicallyKeepingMode` (target `0755` → novo `0755`, conteúdo novo).
  - `ReplaceAppImage.ChecksumMismatchKeepsOld` (target intacto, `downloaded` apagado).
  - `ReplaceAppImage.ReadOnlyDirectoryKeepsOld` (`chmod 0555` no diretório → `NotWritable`, target intacto).
  - `ReplaceAppImage.PathWithSpacesAndAccents` (`Mod Organizer ç.AppImage`).
- [ ] **Step 2:** FAIL; **Step 3: Implementar** `selfupdate_linux.cpp` e os ramos do `SelfUpdater`:
  - `testForUpdate`: sem `appimage::info().active()` ou com o diretório do `$APPIMAGE` sem escrita → log de depuração e retorna; senão `GitHub::Repository("FM0Ura", "ModOrganizerLinux")`; erro HTTP (404 etc.) → só log.
  - `startUpdate`: escolhe com `appImageAsset`/`checksumAsset`; faltando um, `QMessageBox` com o motivo e link para a página do release (`html_url`). Se há programa rodando pelo VFS (o mesmo estado de "mount ativo" que adia o refresh, Fase 2), mensagem "close the running programs first" e retorna. Baixa o `SHA256SUMS` primeiro, depois o AppImage para `downloadPathFor($APPIMAGE)`.
  - `installUpdate`: `replaceAppImage`; sucesso → `scheduleReexec(appimage::info().appImage, argumentos originais sem o `argv[0]`)` e `QCoreApplication::quit()`; erro → mensagem com o motivo.
  - `main()`: depois de `run()`, `#ifndef _WIN32` `selfupdate::runPendingReexec();` (o processo saiu do laço normalmente, VFS desmontado, `aboutToQuit` já emitido uma vez).
- [ ] **Step 4:** testes PASS; smoke PASS.
- [ ] **Step 5: Teste local de ponta a ponta** (sem GitHub): servidor HTTP local com um `release.json` apontando para dois AppImages (`2.5.2.1` instalado, `2.5.2.2` no servidor), com uma variável **só de teste** `MO2_UPDATE_API_URL` (lida apenas se definida; documentada como de desenvolvimento) no lugar de `https://api.github.com`. Esperado: aviso, download, troca, reinício na `2.5.2.2`. Registrar no relatório.
- [ ] **Step 6: Commit** — `deps/modorganizer: Linux: self-update the AppImage from the GitHub releases`.

### Task 9: CI de release e documentação (spec §3.5, §4)

**Files:**
- Create: `.github/workflows/release-linux.yml`
- Modify: `.github/workflows/linux.yml`, `.github/workflows/windows.yml` (actions sem Node 20: `actions/checkout@v5`, `actions/cache@v4`→versão atual, `actions/upload-artifact@v5` — conferir as versões vigentes; cache da imagem do container)
- Create (com a skill **`anthropic-skills:readme-builder`**, pedido do usuário): `README.md` (inglês), `README.pt-BR.md`
- Create: `docs/user/en/{install,first-steps,updating,games,troubleshooting}.md` e os mesmos em `docs/user/pt-BR/`
- Create: `docs/dev/building.md` (o conteúdo de desenvolvimento que sai do README: build, distrobox, CI)
- Modify: `docs/testing/phase5-manual-checklist.md` (completo, §5.3), `ROADMAP.md`, `STATE.md`

**Interfaces:**
- Consumes: `build-appimage.sh` (Task 5), `run-distro-smoke.sh` (Task 6), `MO2_VERSION` (Task 2), âncora `#host-dependencies` (Task 7).

- [ ] **Step 1: `release-linux.yml`:** `on: push: tags: ['v*']` e `workflow_dispatch` (input `version`); `MO2_VERSION` = tag sem `v`; build com testes no container (mesmo `docker run` do `linux.yml`, com `--device /dev/fuse`), `build-appimage.sh`, `check-appdir.sh`, smoke em cada imagem de `distros.txt`, `gh release create "v$MO2_VERSION" --draft --title "Mod Organizer $MO2_VERSION (Linux)" --notes-file …` com os quatro arquivos. `permissions: contents: write`. Commits com `[appimage-ci]` disparam o mesmo workflow sem criar release (só artefatos).
- [ ] **Step 2: Verificar** o workflow com `actionlint` (se disponível no box) e um push de teste **só com autorização do usuário**.
- [ ] **Step 3: README** com a skill `readme-builder`: público = usuário final; seções: o que é, distros testadas e glibc mínima, instalação (Gear Lever / executar), dependências do host, atualização, jogos que funcionam, problemas conhecidos, link para `docs/dev/building.md`, licença. `README.pt-BR.md` com o mesmo conteúdo; links cruzados no topo dos dois.
- [ ] **Step 4: Guia** `docs/user/{en,pt-BR}/` conforme spec §4 (cinco arquivos por língua; `install.md` com a âncora `host-dependencies` e as dependências do QtWebEngine levantadas na Task 6); `troubleshooting.md` inclui como usar o `.debug.tar.zst` com `coredumpctl gdb`.
- [ ] **Step 5: Checklist manual** com os sete passos da spec §5.3, a variante "trocar de instância antes de atualizar" e o `/proc/<pid>/environ` do jogo e de um navegador aberto pelo MO2.
- [ ] **Step 6: ROADMAP/STATE:** Fase 5 em andamento com links para spec, plano e checklist; pendências "Fase 5" do STATE riscadas com onde foram resolvidas; a queda ao reiniciar sai de "Depois da Fase 5".
- [ ] **Step 7: Commit** — `ci: Linux release workflow (draft releases)`, `ci: actions without Node 20; cache the container image`, `docs: user documentation in English and Portuguese`, `docs: Phase 5 checklist, ROADMAP and STATE`; por fim um commit vazio `ci: Phase 5 checkpoint [windows-ci] [appimage-ci]`.

### Task 10: Abrir o repositório (spec §1.3) — controlador, com o usuário

- [ ] **Step 1:** Merge do commit GPLv3 `3269fa8b` (branch `licenca-gpl`) na `fase-5-distribuicao`; o `usr/share/licenses/ModOrganizer/` passa a levar a GPLv3 do port.
- [ ] **Step 2: Varredura do histórico** de todas as branches que serão públicas: `gitleaks detect --log-opts="--all"` (ou `trufflehog git file://…`), mais `git log --all -p | grep -nE "fanzm|/var/home/fmoura|token|apikey|Bearer"` para dados pessoais; resultado mostrado ao usuário com o que precisa ser reescrito (se algo).
- [ ] **Step 3:** **Com a confirmação explícita do usuário no momento**, `gh repo edit FM0Ura/ModOrganizerLinux --visibility public --accept-visibility-change-consequences`.

### Task 11: Teste manual (usuário)

Segue `docs/testing/phase5-manual-checklist.md` (spec §5.3): com o repositório público, o usuário publica `2.5.2.1` e depois `2.5.2.2` (rascunhos do CI), testa a instalação pelo Gear Lever num usuário limpo do Bazzite, Skyrim SE + SKSE + `nxm://` + LOOT + plugin Python + BSA Packer, e as duas atualizações (MO2 e Gear Lever), inclusive depois de trocar de instância.
