# Fase 1 — Build nativo do MO2 no Linux — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** O executável `ModOrganizer` compila com GCC no Linux, abre a interface completa e gerencia instâncias, perfis e mods (sem VFS e sem executar jogos), mantendo o build Windows intacto.

**Architecture:** O superprojeto `ModOrganizerLinux` contém o container de build, o `build.sh`, o CI e as dependências do upstream em `deps/` (via `git subtree --squash`). O fork `modorganizer` ganha implementações Linux do namespace `env` em `src/linux/`, uma interface `VfsConnector` (USVFS no Windows, nula no Linux) e um preset CMake `linux-gcc`. Tipos Windows que aparecem em cabeçalhos públicos vêm de `uibase/win32compat.h` no Linux.

**Tech Stack:** C++23 (GCC ≥ 14), CMake ≥ 3.22 + Ninja, Qt 6 (pacotes Fedora), Boost, spdlog, lz4, zlib, GoogleTest, 7-Zip 26.01 (fonte oficial), Podman/Docker, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md` (seção 4 = Fase 1)

## Global Constraints

- Branch de trabalho nos dois repositórios: `claude/serene-maxwell-1dyrbg`.
- O fork `modorganizer` continua compilando no Windows **contra o `uibase` upstream** (o CI Windows usa `ModOrganizer2/build-with-mob-action`). Logo: nenhuma mudança no `modorganizer` pode depender de API nova do `uibase`; toda mudança no `uibase` é aditiva ou restrita a arquivos `*_linux.cpp`/`win32compat.h`.
- Código Windows existente não é movido nem reescrito; arquivos Windows passam a ser compilados só sob `if(WIN32)`. Implementações Linux em `src/linux/<nome>_linux.cpp` (modorganizer) e `src/linux/<nome>_linux.cpp` (uibase).
- `#ifdef _WIN32` só para mudanças de 1–3 linhas em código compartilhado.
- Nenhum `#include <windows.h>` fora de arquivos compilados apenas no Windows ou de blocos `#ifdef _WIN32`.
- Formatação: `clang-format` com o `.clang-format` de cada repositório (o fork tem CI de lint).
- No Linux não se usa vcpkg; dependências vêm do container (Fedora, versão igual à base atual do Bazzite — `fedora:44` na data da spec).
- Versões do upstream: cada dependência em `deps/` acompanha o `master` upstream (HEAD na data da atualização, commits exatos em `deps/README.md`), como o CI do MO2 (`build-with-mob-action`) — “`uibase` upstream” acima significa o `master` do `uibase`; exceção: `dds-header` segue a porta `mo2-dds-header` do registry (`DirectXTex` tag `jun2024`); 7-Zip **26.01**. (Antes da Task 7b: baseline `1316300c…` do `ModOrganizer2/vcpkg-registry`.)
- Funcionalidades fora da Fase 1 nunca quebram: “Executar” mostra diálogo informativo; LOOT desabilitado sem `lootcli`; criação de BSA oculta; `selfupdater` desabilitado.
- Instâncias globais: `QStandardPaths::AppLocalDataLocation` (já resolve para `$XDG_DATA_HOME/ModOrganizer` no Linux); instâncias portáteis continuam ao lado do executável.
- Commits terminam com as linhas de atribuição da sessão; mensagens sem identificador de modelo.

## Ambiente de execução

O CI (GitHub Actions, container Fedora) é a referência de “passou”. Se o executor não tiver Podman/Docker, pode montar um ambiente local equivalente com pacotes do conda-forge (`qt6-main`, `boost-cpp`, `spdlog`, `gtest`, `lz4-c`, `zlib`, `gxx`) num diretório de rascunho; esse ambiente **não** é versionado.

## Review Focus

1. **Nomes de arquivo com maiúsculas/minúsculas diferentes** (`Textures/` vs `textures/` em mods distintos): a aba Data e o cálculo de conflitos devem tratá-los como o mesmo caminho, como no Windows → teste em Task 9.
2. **Nomes não-ASCII** (`Ünïcödé Mod/`, arquivos em japonês) na varredura e na instalação: devem aparecer corretamente, sem `?` → testes em Task 5 e Task 9.
3. **Separador `\` vindo de arquivos do usuário** (`modlist.txt`, `meta.ini`, caminhos FOMOD como `textures\armor\a.dds`): devem resolver para o arquivo real → teste em Task 10.
4. **Link simbólico quebrado ou pasta sem permissão dentro de um mod**: a varredura deve pular a entrada e registrar aviso, sem travar → teste em Task 9.
5. **Arquivo compactado corrompido ou protegido por senha** na instalação: mensagem de erro, sem crash → teste em Task 5.

---

### Task 1: Superprojeto — dependências em `deps/` via subtree

**Files:**
- Create: `ModOrganizerLinux/deps/{cmake_common,uibase,archive,bsatk,esptk,lootcli,dds-header,game_bethesda,installer_quick,installer_manual,installer_fomod}/` (via `git subtree add --squash`)
- Create: `ModOrganizerLinux/deps/README.md`
- Create: `ModOrganizerLinux/scripts/update-dep.sh`
- Create: `ModOrganizerLinux/README.md`

**Interfaces:**
- Produces: árvore `deps/<nome>` para os tasks seguintes; `scripts/update-dep.sh <nome> <ref>` = `git subtree pull --prefix deps/<nome> <url> <ref> --squash`.

- [ ] **Step 1:** Ler, no `vcpkg-registry` no baseline `1316300c…`, a versão de cada porta (`mo2-cmake`, `mo2-uibase`, `mo2-archive`, `mo2-bsatk`, `mo2-esptk`, `mo2-lootcli-header`, `mo2-dds-header`) e a tag/commit correspondente (`REF` no `portfile.cmake`). Repositórios: `ModOrganizer2/cmake_common` e `ModOrganizer2/modorganizer-<nome>`; `dds-header` conforme o `portfile`. Os plugins (`modorganizer-game_bethesda`, `modorganizer-installer_quick`, `modorganizer-installer_manual`, `modorganizer-installer_fomod`) não estão no registry: usar o commit mais recente da branch padrão que compile contra o `uibase` escolhido (conferir a versão de `uibase` exigida no `vcpkg.json` de cada um).
- [ ] **Step 2:** `git subtree add --prefix deps/<nome> <url> <ref> --squash` para cada um.
- [ ] **Step 3:** Escrever `deps/README.md` com a tabela `nome | repositório | ref | versão` e a regra “mudanças no subtree ficam em commits separados, prefixo `deps/<nome>:`”. Escrever `scripts/update-dep.sh` (bash, `set -euo pipefail`, mapa nome→url). `README.md` raiz: objetivo, link para a spec, como compilar (preenchido no Task 2).
- [ ] **Step 4:** Verificar: `ls -d deps/*/` lista os 11 diretórios e cada um tem `CMakeLists.txt` (exceção documentada no README para dependências só de cabeçalho); `bash -n scripts/update-dep.sh` sem erro.
- [ ] **Step 5:** Commit `deps: import upstream MO2 dependencies as subtrees` e push.

### Task 2: Container, `build.sh` e CI Linux

**Files:**
- Create: `ModOrganizerLinux/build/Containerfile`
- Create: `ModOrganizerLinux/build.sh`
- Create: `ModOrganizerLinux/.github/workflows/linux.yml`

**Interfaces:**
- Produces: `./build.sh [--only <alvo>] [--tests]` — compila na ordem `7zip → cmake_common → uibase → archive → bsatk → esptk → lootcli → dds-header → modorganizer → game_bethesda → installer_quick → installer_manual → installer_fomod`, com `CMAKE_PREFIX_PATH=$PWD/install`, instala em `$PWD/install`, build em `$PWD/build-out/<alvo>`. `MO2_SRC` (padrão `../modorganizer`) aponta para o fork. `--tests` liga `BUILD_TESTING=ON` e roda `ctest --output-on-failure` em cada alvo que tiver testes. Alvos ainda não portados são ignorados com `--only`.
- Produces: imagem `mo2-linux-build` com GCC, CMake, Ninja, `qt6-qtbase-devel`, `qt6-qtwebengine-devel`, `qt6-qtwebsockets-devel`, `qt6-qtnetworkauth-devel`, `qt6-qtdeclarative-devel`, `qt6-qttools-devel`, `boost-devel`, `spdlog-devel`, `fmt-devel`, `lz4-devel`, `zlib-devel`, `gtest-devel`, `fuse3-devel`, `clang-tools-extra`, `git`, `python3`, Rust via `rustup` (stable).
- Produces: workflow `linux.yml` — em todo push/PR: faz checkout do superprojeto e do fork `FM0Ura/modorganizer` (mesmo nome de branch se existir, senão `master`) em `../modorganizer`, constrói a imagem e roda `./build.sh --tests` dentro dela; publica `install/` como artefato.

- [ ] **Step 1:** Escrever `Containerfile`, `build.sh` (com `set -euo pipefail`; nesta etapa só o alvo `cmake_common` precisa funcionar) e `linux.yml`.
- [ ] **Step 2:** Verificar localmente ou no CI: `./build.sh --only cmake_common` termina com `install/share/mo2-cmake/mo2-cmake-config.cmake` (ou o caminho que o `install()` do `cmake_common` define) presente.
- [ ] **Step 3:** Completar `README.md` com “compilar com podman/distrobox”.
- [ ] **Step 4:** Commit `build: add Fedora build container, build.sh and Linux CI` e push; confirmar o job verde no GitHub Actions.

### Task 3: `deps/cmake_common` — caminho GCC

**Files:**
- Modify: `deps/cmake_common/mo2_cpp.cmake` (`mo2_configure_target`, novo `mo2_configure_gcc`)
- Modify: `deps/cmake_common/mo2_utils.cmake` (`mo2_deploy_qt`, `mo2_deploy_qt_for_tests`, `mo2_set_project_to_run_from_install`, `mo2_add_translations` se depender de MSVC)
- Create: `ModOrganizerLinux/tests/cmake-smoke/CMakeLists.txt`, `ModOrganizerLinux/tests/cmake-smoke/main.cpp`

**Interfaces:**
- Produces: `mo2_configure_gcc(TARGET)` — `CXX_STANDARD 23`, `CXX_EXTENSIONS OFF`, `-Wall -Wextra` (`WARNINGS` mapeado: `ON`/`4` → `-Wall -Wextra`, `All` → `-Wall -Wextra -Wpedantic`), `POSITION_INDEPENDENT_CODE ON`, `-fvisibility=hidden` em bibliotecas compartilhadas. Chamada por `mo2_configure_target` quando `NOT MSVC`.
- Produces: no Linux, funções de deploy/PDB/VS são no-op; `mo2_install_plugin` instala em `bin/plugins` (mesmo layout do Windows).

- [ ] **Step 1:** Escrever `tests/cmake-smoke`: biblioteca `smoke` com `mo2_configure_target(smoke WARNINGS ON)` e `main.cpp` que usa `std::expected<int, std::string>` e `std::format`; adicionar alvo ao `build.sh` (`cmake-smoke`, roda o executável).
- [ ] **Step 2:** Rodar `./build.sh --only cmake_common --only cmake-smoke` → esperado FAIL (sem `-std=c++23`, `std::expected` não existe).
- [ ] **Step 3:** Implementar `mo2_configure_gcc` e os no-ops.
- [ ] **Step 4:** Rodar de novo → PASS; o executável imprime `ok`.
- [ ] **Step 5:** Commit `deps/cmake_common: add GCC/Linux configuration` e push.

### Task 4: 7-Zip 26.01 como `7z.so`

**Files:**
- Create: `ModOrganizerLinux/build/7zip/build-7zip.sh`
- Create: `ModOrganizerLinux/build/7zip/7zip-config.cmake`
- Modify: `ModOrganizerLinux/build.sh` (alvo `7zip`)

**Interfaces:**
- Produces: `install/lib/7z.so` (de `CPP/7zip/Bundles/Format7zF/makefile.gcc`, fonte `ip7z/7zip` tag `26.01` com SHA256 verificado), cabeçalhos em `install/include/7zip/` (árvore `C/` e `CPP/` necessária para `IArchive.h`, `Common/MyWindows.h`), e pacote CMake `7zip` com alvo `7zip::7zip` **INTERFACE** (só include dirs; o `archive` carrega a `.so` em tempo de execução).

- [ ] **Step 1:** Escrever o script e o config; integrar no `build.sh`.
- [ ] **Step 2:** Verificar: `./build.sh --only 7zip` e `nm -D install/lib/7z.so | grep -E ' (CreateObject|GetNumberOfFormats|GetHandlerProperty2)$'` lista os 3 símbolos.
- [ ] **Step 3:** Commit `build: build 7-Zip 26.01 shared library` e push.

### Task 5: `deps/archive` no Linux

**Files:**
- Modify: `deps/archive/src/library.h` (`dlopen`/`dlsym`/`dlclose` sob `#ifndef _WIN32`)
- Modify: `deps/archive/src/fileio.{h,cpp}` (implementação POSIX: `open`/`pread`/`pwrite`/`fstat`/`lseek`; caminhos `std::filesystem::path`)
- Modify: `deps/archive/src/*.cpp|h` que incluem `<windows.h>` → no Linux incluir `Common/MyWindows.h` do 7-Zip
- Modify: `deps/archive/src/CMakeLists.txt` (sem `version.rc` e flags MSVC fora do Windows; `dl` linkado)
- Modify: `deps/archive/src/archive.cpp` (nome padrão da biblioteca: `7z.dll` no Windows, `7z.so` no Linux, procurada em `<dir do executável>/../lib` (Linux) e `<dir do executável>/dlls` (Windows))
- Create: `deps/archive/tests/{CMakeLists.txt,test_archive.cpp}`, `deps/archive/tests/data/{simple.7z,simple.zip,simple.rar,unicode.7z,corrupt.7z,password.7z}`

**Interfaces:**
- Consumes: `7zip::7zip`, `install/lib/7z.so` (Task 4).
- Produces: API pública `archive/archive.h` **inalterada** (`CreateArchive()`, `Archive::open/extract/getFileList`).

- [ ] **Step 1:** Criar os arquivos de teste (conteúdo: `a.txt` = `hello`, `dir/B.txt` = `world`; `unicode.7z` contém `Ünïcödé/日本.txt`; `password.7z` com senha `mo2`; `corrupt.7z` = `simple.7z` truncado na metade) e os testes GoogleTest:
  - `ArchiveTest.ListsFiles` — `getFileList()` de cada formato contém `a.txt` e `dir/B.txt`.
  - `ArchiveTest.ExtractsContent` — extrai para diretório temporário; conteúdo de `a.txt` == `"hello"`.
  - `ArchiveTest.UnicodeNames` — extrai `unicode.7z`; existe `Ünïcödé/日本.txt` (UTF-8 no disco).
  - `ArchiveTest.CorruptFails` — `open()` retorna `false` ou `extract()` retorna `false`, sem crash; `getLastError()` != `ERROR_NONE`.
  - `ArchiveTest.PasswordCallback` — com callback retornando `"mo2"` extrai; com `""` falha sem crash.
- [ ] **Step 2:** `./build.sh --only archive --tests` → FAIL (não compila).
- [ ] **Step 3:** Portar conforme os arquivos listados. Conversão de nomes: `wchar_t` (UTF-32 no Linux) ↔ UTF-8 via `QString`/`std::filesystem::path`; nada de `wcstombs` dependente de locale.
- [ ] **Step 4:** `./build.sh --only archive --tests` → todos PASS.
- [ ] **Step 5:** Commit `deps/archive: Linux support (dlopen 7z.so, POSIX file IO)` e push.

### Task 6: `deps/uibase` no Linux

**Files:**
- Create: `deps/uibase/include/uibase/win32compat.h`
- Modify: cabeçalhos públicos que incluem `<windows.h>`/`<ShlObj.h>` (`utility.h`, `registry.h`, `errorcodes.h`, `imoinfo.h`, `formatters/strings.h`, `taskprogressmanager.h`, `game_features/scriptextender.h`) → no Linux incluem `win32compat.h`
- Create: `deps/uibase/src/linux/{utility_linux.cpp,registry_linux.cpp,report_linux.cpp,errorcodes_linux.cpp,steamutility_linux.cpp}`
- Modify: `deps/uibase/src/{utility.cpp,registry.cpp,report.cpp,errorcodes.cpp,log.cpp,tutorialcontrol.cpp,steamutility.cpp}` — mover apenas as funções com Win32 para blocos/arquivos Windows; código portável fica compartilhado
- Modify: `deps/uibase/src/CMakeLists.txt`, `deps/uibase/tests/CMakeLists.txt`
- Create: `deps/uibase/tests/test_linux_utility.cpp`

**Interfaces:**
- Produces: `win32compat.h` (somente se `!_WIN32`): `DWORD=std::uint32_t`, `LONG=std::int32_t`, `BOOL=int`, `HANDLE=void*`, `INVALID_HANDLE_VALUE=reinterpret_cast<HANDLE>(-1)`, `HWND=void*`, `HRESULT=std::int32_t`, `LPCWSTR=const wchar_t*`, `FILETIME{DWORD dwLowDateTime, dwHighDateTime}`, `struct GUID{uint32_t Data1; uint16_t Data2, Data3; uint8_t Data4[8];}`, `using KNOWNFOLDERID = GUID; using REFKNOWNFOLDERID = const GUID&;` e constantes `FOLDERID_{Desktop,Documents,Downloads,LocalAppData,RoamingAppData,ProgramFiles,ProgramFilesX86,StartMenu,Windows,OneDrive}` com os GUIDs oficiais do Windows, `MAX_PATH=260`, `ERROR_SUCCESS=0`, `ERROR_FILE_NOT_FOUND=2`, `ERROR_ACCESS_DENIED=5`.
- Produces: comportamento Linux das funções públicas:
  - `getKnownFolder(id)`: Desktop/Documents/Downloads → `QStandardPaths` correspondente; LocalAppData → `$XDG_DATA_HOME`; RoamingAppData → `$XDG_CONFIG_HOME`; StartMenu → `~/.local/share/applications`; ProgramFiles/ProgramFilesX86/Windows/OneDrive → lança `std::runtime_error`; `getOptionalKnownFolder` → string vazia para esses. (Fase 3 redireciona para o prefixo do Proton via plugin de jogo.)
  - `shellDelete(..., recycle=true)` → lixeira freedesktop (`$XDG_DATA_HOME/Trash/{files,info}` com `.trashinfo` `Path=`/`DeletionDate=`); `recycle=false` → remoção recursiva. `shellCopy/shellMove/shellRename` → `std::filesystem` (copy recursivo, `rename` com fallback copy+remove entre dispositivos).
  - `shell::Explore` → `QDesktopServices::openUrl` da pasta (arquivo: abre a pasta que o contém); `shell::Open`/`Execute` → `QProcess::startDetached` / `QDesktopServices`; `Result` carrega `errno` em `error()`; `SetUrlHandler` → no-op com log (Fase 4).
  - `WriteRegistryValue(app, key, value, file)` → escreve no arquivo INI preservando o resto, mesma semântica de `WritePrivateProfileStringW` (seção/chave case-insensitive, cria se não existir, CRLF preservado se o arquivo já usa CRLF).
  - `getFileVersion`/`getProductVersion` → leem o recurso `VS_VERSION_INFO` de executáveis PE (parser mínimo próprio em `utility_linux.cpp`); arquivo não-PE → string vazia.
  - `iconForExecutable` → ícone genérico `QIcon::fromTheme("application-x-executable")`.
  - `formatSystemMessage`/`windowsErrorString` → `std::strerror`.
  - `findSteam()` → primeiro existente de `~/.local/share/Steam`, `~/.steam/steam`, `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`; `findSteamGame` → procura `steamapps/common/<appName>/<validFile>` nas bibliotecas de `libraryfolders.vdf` (parser VDF mínimo; o módulo completo é da Fase 3).

- [ ] **Step 1:** Escrever `tests/test_linux_utility.cpp` (compilado só no Linux):
  - `KnownFolder.DocumentsMatchesQt` — `getKnownFolder(FOLDERID_Documents).path() == QStandardPaths::writableLocation(DocumentsLocation)`.
  - `KnownFolder.ProgramFilesOptionalEmpty` — `getOptionalKnownFolder(FOLDERID_ProgramFiles).isEmpty()`.
  - `ShellDelete.RecycleMovesToTrash` — com `XDG_DATA_HOME` apontando para dir temporário: após `shellDelete({f}, true)` o arquivo não existe, existe `Trash/files/<nome>` e `Trash/info/<nome>.trashinfo` contendo `Path=` com o caminho absoluto.
  - `ShellMove.CrossDirectory` — move árvore com 2 níveis; origem some, destino tem o mesmo conteúdo.
  - `Registry.WritesIni` — arquivo com `[General]\r\nfoo=1\r\n`; `WriteRegistryValue(L"general", L"FOO", L"2", path)` resulta em `[General]\r\nfoo=2\r\n`; chave nova em seção nova é adicionada no fim.
  - `FileVersion.ReadsPe` — fixture `tests/data/version.exe` (PE pequeno com `FileVersion 1.2.3.4`) → `"1.2.3.4"`; arquivo texto → `""`.
  - `Steam.FindsGameInLibrary` — `HOME` temporário com `.local/share/Steam/steamapps/libraryfolders.vdf` apontando para uma segunda biblioteca contendo `steamapps/common/Skyrim Special Edition/SkyrimSE.exe` → `findSteamGame("Skyrim Special Edition", "SkyrimSE.exe")` retorna esse diretório.
- [ ] **Step 2:** `./build.sh --only uibase --tests` → FAIL (não compila).
- [ ] **Step 3:** Implementar `win32compat.h`, separar código Win32 e escrever `src/linux/*_linux.cpp`; CMake: `if(WIN32)` lista arquivos/ libs Windows, `else()` lista `src/linux/*.cpp`, sem `version.rc`.
- [ ] **Step 4:** `./build.sh --only uibase --tests` → novos testes e os testes upstream (`test_formatters`, `test_ifiletree`, `test_strings`, `test_versioning`) PASS.
- [ ] **Step 5:** Commit `deps/uibase: Linux support` e push.

### Task 7: `bsatk`, `esptk`, `lootcli` (header), `dds-header` no Linux

**Files:**
- Modify: `deps/bsatk/src/*`, `deps/esptk/src/*`, respectivos `CMakeLists.txt` (só o necessário para compilar com GCC: `version.rc` só no Windows, includes Windows, `_wfopen` → `std::ifstream` com `std::filesystem::path`, etc.)
- Create: `deps/bsatk/tests/test_bsa.cpp`, `deps/esptk/tests/test_esp.cpp` + fixtures mínimas geradas por script Python `tests/data/make_fixtures.py` versionado junto

**Interfaces:**
- Produces: alvos `mo2::bsatk`, `mo2::esptk`, `mo2::lootcli-header`, `mo2::dds-header` instalados com o mesmo nome do Windows.

- [ ] **Step 1:** Testes: `Bsa.ListsFiles` (BSA v105 sintético com `textures\a.dds` → lista contém `textures\a.dds`), `Bsa.ExtractsFile` (conteúdo extraído == bytes da fixture); `Esp.ReadsHeader` (ESP sintético com master `Skyrim.esm` e flag ESL → `masters()=={"Skyrim.esm"}`, `isLight()`).
- [ ] **Step 2:** `./build.sh --only bsatk --only esptk --only lootcli --only dds-header --tests` → FAIL.
- [ ] **Step 3:** Portar.
- [ ] **Step 4:** Mesmo comando → PASS.
- [ ] **Step 5:** Commit `deps/bsatk, deps/esptk: Linux support` e push.

### Task 7b: re-fixar `deps/` no `master` upstream (inserida pelo controlador, decisão R9)

O baseline do registry era antigo demais para o HEAD do fork (`4ba22f0`, 2026-10-05), que já usa API mais nova do upstream (`IPluginGame::blueprintPrefix()` do `uibase` `61451bd`; sobrecargas `std::filesystem::path` de `BSA::Archive::read/extract/extractAll`, PR #27 do `bsatk`). Todas as dependências foram atualizadas para o `master` upstream com `scripts/update-dep.sh <nome> master`, preservando as mudanças Linux (Tasks 3, 5, 6, 7); a nova regra está em Global Constraints e em `deps/README.md`.

### Task 8: `modorganizer` — CMake Linux, preset e esqueleto `src/linux/`

**Files (repo `modorganizer`):**
- Modify: `CMakePresets.json` (preset `linux-gcc`: Ninja, `binaryDir` `build-linux`, sem toolchain vcpkg, `CMAKE_BUILD_TYPE=RelWithDebInfo`)
- Modify: `CMakeLists.txt` (instalar `dump_running_process.bat` só no Windows)
- Modify: `src/CMakeLists.txt` — `if(WIN32)`: `usvfs`, `libbsarch`, `Shlwapi Bcrypt Version Dbghelp`, manifests, `.rc`, `mo2_deploy_qt`, arquivos `env*.cpp`/`spawn.cpp`/`usvfsconnector.cpp`/`shared/windows_error.cpp`; `else()`: `src/linux/*.cpp`, `dl`, `install(TARGETS organizer RUNTIME DESTINATION bin)`, `INSTALL_RPATH "$ORIGIN/../lib"`; recursos (`stylesheets`, `tutorials`, `translations`, `resources`) ficam em `bin/` como no Windows, então o código que os localiza via `applicationDirPath()` não muda; opção `MO2_WITH_LIBBSARCH` (padrão `ON` se `WIN32`).
- Modify: `src/organizercore.cpp:76,1116` — uso de `libbsarch` sob `#ifdef MO2_WITH_LIBBSARCH`; sem ele, o mesmo caminho usa `bsatk` para ler o arquivo do BSA.
- Create: `src/linux/README.md` (regra: um `<nome>_linux.cpp` por `env*.cpp`/arquivo Windows substituído)
- Modify: `.github/workflows/build.yml` (adicionar `workflow_dispatch` e `push` em `claude/**`)
- Modify: `ModOrganizerLinux/build.sh` (alvo `modorganizer` com `--preset linux-gcc`)

**Interfaces:**
- Produces: macro de compilação `MO2_WITH_LIBBSARCH`; layout de instalação Linux = `install/bin/{ModOrganizer,plugins/,stylesheets/,tutorials/,translations/,resources/,loot/}` + `install/lib/` (bibliotecas, incluindo `libuibase.so` e `7z.so`), usado pelos tasks seguintes.

- [ ] **Step 1:** Fazer as mudanças de CMake/preset; rodar `./build.sh --only modorganizer` e registrar a lista de erros de compilação em `ModOrganizerLinux/docs/superpowers/plans/phase1-compile-errors.md` (agrupados por arquivo) — é o checklist dos tasks 9–12.
- [ ] **Step 2:** Trocar `libbsarch` por `bsatk` no caminho Linux de `organizercore.cpp`.
- [ ] **Step 3:** Disparar o workflow Windows do fork (`workflow_dispatch` no branch) → verde.
- [ ] **Step 4:** Commit no fork `Add Linux CMake preset and platform split` e no superprojeto `build: build modorganizer`; push nos dois.

### Task 9: `env` — sistema de arquivos (`envfs`) e árvore de dados

**Files:**
- Create: `modorganizer/src/linux/envfs_linux.cpp`
- Modify: `modorganizer/src/envfs.h` (sem `<windows.h>` no Linux; `FILETIME` via `win32compat.h`)
- Modify: `modorganizer/src/shared/directoryentry.cpp`, `fileentry.cpp`, `filesorigin.cpp`, `util.{h,cpp}` — só onde o compilador apontar
- Create: `modorganizer/tests/{CMakeLists.txt,test_main.cpp,test_envfs.cpp,test_directoryentry.cpp}`; `src/CMakeLists.txt` ganha `add_subdirectory(../tests)` sob `BUILD_TESTING`

**Interfaces:**
- Produces: `env::DirectoryWalker::forEachEntry(path, cx, dirStartF, dirEndF, fileF)` e `env::getFilesAndDirs(path)` no Linux usando `openat(O_DIRECTORY)` + `getdents64` recursivo (pilha explícita, sem recursão de função); `FILETIME` = `st_mtim` convertido para intervalos de 100 ns desde 1601-01-01 (`(sec + 11644473600) * 10'000'000 + nsec / 100`); nomes convertidos de UTF-8 para `std::wstring` via `QString::fromUtf8(...).toStdWString()`; entradas `DT_UNKNOWN` resolvidas com `fstatat`; links simbólicos seguidos para diretórios, quebrados ignorados com `log::warn`; `EACCES` em subpasta ignorado com `log::warn`.
- Produces: comparações de nome em `DirectoryEntry` continuam case-insensitive (já usam `ToLowerCopy`); qualquer uso de `_wcsicmp`/`CompareStringW` substituído por helper `MOShared::caseInsensitiveEqual(std::wstring_view, std::wstring_view)` em `shared/util.h` (Windows: implementação atual).

- [ ] **Step 1:** Testes (dir temporário):
  - `EnvFs.WalksTree` — 3 níveis, 5 arquivos → callbacks recebem todos os arquivos com tamanhos corretos e `dirStart/dirEnd` pareados.
  - `EnvFs.FileTimeEpoch` — arquivo com `mtime = 0` (via `utimensat`) → `FILETIME` == `116444736000000000`.
  - `EnvFs.UnicodeNames` — `Ünïcödé Mod/日本.txt` aparece com o nome exato.
  - `EnvFs.SkipsBrokenSymlinkAndUnreadableDir` — link quebrado + pasta `chmod 000` → walker termina, demais arquivos listados (pular se rodando como root).
  - `DirectoryEntry.CaseInsensitiveMerge` — origem A tem `Textures/a.dds`, origem B (prioridade maior) tem `textures/A.DDS` → `findFile(L"textures\\a.dds")` existe uma vez e o `origin` vencedor é B; conflito registrado com A como alternativa.
- [ ] **Step 2:** `./build.sh --only modorganizer --tests` (alvo `organizer_tests`) → FAIL.
- [ ] **Step 3:** Implementar.
- [ ] **Step 4:** Mesmo comando → PASS.
- [ ] **Step 5:** Commit `Linux: native directory walker and data tree` e push.

### Task 10: `env` — processos, módulos, segurança, shell, atalhos, métricas, info do sistema

**Files:**
- Create: `modorganizer/src/linux/{envmodule_linux.cpp,envsecurity_linux.cpp,envshell_linux.cpp,envshortcut_linux.cpp,envmetrics_linux.cpp,envwindows_linux.cpp,env_linux.cpp}`
- Modify: `modorganizer/src/{envmodule.h,envshell.h,envmetrics.h,envwindows.h,env.h}` — tipos Win32 só sob `#ifdef _WIN32`; membros privados Windows (`HMENU`, `COMPtr`, `wndProc`) sob `#ifdef _WIN32`
- Create: `modorganizer/tests/{test_envmodule.cpp,test_envshortcut.cpp,test_paths.cpp}`

**Interfaces:**
- Produces (Linux):
  - `env::getRunningProcesses()` → lê `/proc/<pid>/{stat,comm,exe}`; `Process` guarda `pid`, `ppid`, `name` (basename do `exe`, ou `comm`); `getProcessTree(HANDLE)` recebe o pid convertido (`HANDLE` = `reinterpret_cast<void*>(intptr_t(pid))` no Linux — documentado em `envmodule.h`).
  - `env::getLoadedModules()` → caminhos únicos de `/proc/self/maps`.
  - `env::getSecurityProducts()` → vazio; `getFileSecurity(file)` → `FileRights` a partir de `access(R_OK|W_OK|X_OK)` e dono via `stat`.
  - `env::ShellMenu`/`ShellMenuCollection::exec` → menu Qt com “Abrir”, “Abrir pasta”, “Copiar caminho”.
  - `env::Shortcut::add/remove/exists(loc)` → arquivo `<nome>.desktop` em `Desktop` (`QStandardPaths::DesktopLocation`) ou `StartMenu` (`~/.local/share/applications`), com `Type=Application`, `Name=`, `Exec=` (alvo + argumentos, escapados conforme a Desktop Entry Spec), `Path=` (working dir), `Icon=`; permissão `0755`.
  - `env::Metrics` → `QGuiApplication::screens()` (nome, DPI lógico, primário).
  - `env::WindowsInfo` (nome mantido) no Linux → `QSysInfo::prettyProductName()`/`kernelVersion()`; `isElevated()` → `geteuid()==0`.
  - `env::Environment::dump/coreDumps` → no Linux registra o backtrace (`backtrace_symbols_fd`) no log em `SIGSEGV/SIGABRT` e deixa o core dump do sistema; `CoreDumpTypes` mantém os valores.
- Produces: helper `QString MOShared::normalizeSeparators(QString)` (troca `\` por `/` no Linux; identidade no Windows) usado onde caminhos de arquivos do usuário (`modlist.txt`, `meta.ini`, entradas de instalação) viram caminhos de disco.

- [ ] **Step 1:** Testes:
  - `EnvModule.FindsSelf` — `getRunningProcesses()` contém processo com `pid == getpid()` e `ppid == getppid()`.
  - `EnvShortcut.WritesDesktopEntry` — com `XDG_DATA_HOME` temporário: `Shortcut().name("Skyrim SE").target("/opt/mo2/bin/ModOrganizer").arguments("\"moshortcut://:SKSE\"").add(StartMenu)` cria `applications/Skyrim SE.desktop` contendo `Exec=/opt/mo2/bin/ModOrganizer "moshortcut://:SKSE"`; `exists` → true; `remove` → arquivo some.
  - `Paths.NormalizesUserSeparators` — `normalizeSeparators("textures\\armor\\a.dds") == "textures/armor/a.dds"` no Linux.
- [ ] **Step 2:** `./build.sh --only modorganizer --tests` → FAIL.
- [ ] **Step 3:** Implementar e aplicar `normalizeSeparators` nos pontos do checklist `phase1-compile-errors.md` e em `installationmanager.cpp` (destino de arquivos extraídos), `modinforegular.cpp` (leitura de `meta.ini`), `profile.cpp` (leitura de `modlist.txt`).
- [ ] **Step 4:** Mesmo comando → PASS.
- [ ] **Step 5:** Commit `Linux: env process, shell, shortcut, metrics implementations` e push.

### Task 11: `VfsConnector` e execução desabilitada no Linux

**Files:**
- Create: `modorganizer/src/vfsconnector.h` (interface), `modorganizer/src/linux/vfsconnector_linux.cpp` (nula)
- Modify: `modorganizer/src/usvfsconnector.{h,cpp}` → `UsvfsConnector` implementa a interface (só Windows)
- Modify: `modorganizer/src/organizercore.{h,cpp}`, `processrunner.{h,cpp}`, `spawn.{h,cpp}`, `uilocker.{h,cpp}`, `mainwindow.cpp`, `sanitychecks.cpp`, `aboutdialog.cpp`, `moapplication.cpp`, `loot.cpp`
- Create: `modorganizer/src/linux/{spawn_linux.cpp,processrunner_linux.cpp}`
- Create: `modorganizer/tests/test_vfsconnector.cpp`

**Interfaces:**
- Produces: `class VfsConnector { public: virtual ~VfsConnector(); virtual void updateMapping(const MappingType&) = 0; virtual void updateParams(MOBase::log::Levels, env::CoreDumpTypes, const QString& crashDumpsPath, std::chrono::seconds spawnDelay, QString executableBlacklist, const QStringList& skipFileSuffixes, const QStringList& skipDirectories) = 0; virtual void updateForcedLibraries(const QList<MOBase::ExecutableForcedLoadSetting>&) = 0; virtual QString name() const = 0; };` e `std::unique_ptr<VfsConnector> createVfsConnector();` (Windows → `UsvfsConnector`; Linux → `NullVfsConnector`, que guarda o último mapping em `const MappingType& lastMapping() const` para testes e para a Fase 2). `OrganizerCore::m_USVFS` passa a ser `std::unique_ptr<VfsConnector> m_VFS`.
- Produces: no Linux, `ProcessRunner::run()` retorna `ProcessRunner::Error` e mostra `QMessageBox::information` com o texto **“Executar programas ainda não está disponível no Linux. Esta funcionalidade chega na Fase 3 do port.”** (marcado com `tr()`); LOOT: `lootcli` procurado em `<bin>/loot/lootcli`; ausente → ação desabilitada com tooltip **“LOOT ainda não está disponível nesta versão para Linux.”**; criação de BSA: ação oculta quando `!MO2_WITH_LIBBSARCH`; `selfupdater` não verifica atualizações no Linux.

- [ ] **Step 1:** Teste `VfsConnector.NullStoresMapping` — `createVfsConnector()` no Linux; `name()=="none"`; após `updateMapping({Mapping{"/a","/b",true,false}})`, `lastMapping().size()==1`.
- [ ] **Step 2:** `./build.sh --only modorganizer --tests` → FAIL.
- [ ] **Step 3:** Implementar; no Windows o comportamento é idêntico ao atual (só troca de tipo do membro).
- [ ] **Step 4:** Testes Linux PASS **e** workflow Windows do fork verde.
- [ ] **Step 5:** Commit `Introduce VfsConnector interface; disable process launching on Linux` e push.

### Task 12: Restante da compilação e smoke test

**Files:**
- Modify: arquivos restantes do checklist `phase1-compile-errors.md` (esperados: `mainwindow.cpp`, `profile.cpp`, `pluginlist.cpp`, `filetreeitem.cpp`, `profilesdialog.cpp`, `settingsutilities.cpp`, `organizerproxy.{h,cpp}`, `savestab.cpp`, `commandline.cpp`, `selfupdater.cpp`, `messagedialog.cpp`, `main.cpp`, `shared/util.cpp`, `shared/windows_error.*`)
- Modify: `ModOrganizerLinux/.github/workflows/linux.yml` (passo de smoke test)

**Interfaces:**
- Produces: `install/bin/ModOrganizer` executável; `ModOrganizer --help` imprime a ajuda e sai com código 0 sob `QT_QPA_PLATFORM=offscreen`.

- [ ] **Step 1:** Adicionar ao CI o smoke test `QT_QPA_PLATFORM=offscreen install/bin/ModOrganizer --help` (espera exit 0 e saída contendo `instance`); rodar → FAIL (não linka).
- [ ] **Step 2:** Resolver os itens restantes do checklist, marcando cada um no arquivo; nenhum `#include <windows.h>` fora do permitido (verificar: `grep -rln 'include <windows.h>\|include <Windows.h>' src | xargs` só lista arquivos compilados apenas no Windows).
- [ ] **Step 3:** `./build.sh --tests` completo + smoke test → PASS; workflow Windows verde.
- [ ] **Step 4:** Commit `Linux: complete native build` (fork) e `ci: add smoke test` (superprojeto); push.

### Task 13: Plugins mínimos — `game_bethesda` e instaladores

**Files:**
- Modify: `deps/game_bethesda/**` (código compartilhado `src/gamebryo`, `src/creation` e jogos), `deps/installer_quick/**`, `deps/installer_manual/**`, `deps/installer_fomod/**` — só o necessário para compilar com GCC e rodar no Linux
- Modify: `ModOrganizerLinux/build.sh` (alvos dos plugins, instalados em `install/bin/plugins`)
- Create: `deps/game_bethesda/tests/test_skyrimse_linux.cpp`
- Create: `ModOrganizerLinux/tests/fixtures/fomod-sample/` (mod FOMOD mínimo: `fomod/ModuleConfig.xml` com 1 passo, 2 opções exclusivas `Option A`/`Option B`, cada uma instalando `textures/option_x.dds`)
- Create: `deps/installer_fomod/tests/test_fomod_parse.cpp`

**Interfaces:**
- Consumes: `uibase` Linux (Task 6), em especial `getKnownFolder` e `findSteamGame`.
- Produces: `install/bin/plugins/{libgame_*.so,libinstaller_quick.so,libinstaller_manual.so,libinstaller_fomod.so}` carregados pelo `PluginContainer` (que já usa `QPluginLoader`; o filtro de arquivos, se houver `.dll` fixo, passa a aceitar `.so` no Linux).
- Produces: no Linux, a detecção de instalação dos jogos Bethesda tenta `findSteamGame(<pasta Steam do jogo>, <exe>)` antes do registro; sem resultado, o usuário informa o caminho manualmente (fluxo atual do MO2). Pastas “Meus Documentos/My Games” e “AppData/Local” usam `getKnownFolder` (redirecionadas ao prefixo do Proton na Fase 3).

- [ ] **Step 1:** Testes:
  - `SkyrimSE.DetectsSteamInstall` — `HOME` temporário com biblioteca Steam falsa contendo `steamapps/common/Skyrim Special Edition/SkyrimSE.exe` → `gameDirectory()` aponta para ela e `isInstalled()` é `true`.
  - `SkyrimSE.DataDirectory` — `dataDirectory()` == `<jogo>/Data`.
  - `Fomod.ParsesSample` — o parser do `installer_fomod` carrega `ModuleConfig.xml` da fixture e retorna 1 passo com as opções `Option A` e `Option B` (se o parser não for testável sem a UI, extrair a função de parsing para uma função livre em `installer_fomod`, sem mudar comportamento).
- [ ] **Step 2:** `./build.sh --only game_bethesda --only installer_quick --only installer_manual --only installer_fomod --tests` → FAIL.
- [ ] **Step 3:** Portar; os demais jogos do `game_bethesda` precisam apenas compilar.
- [ ] **Step 4:** Mesmo comando → PASS; smoke test: `QT_QPA_PLATFORM=offscreen install/bin/ModOrganizer --help` continua passando e o log (`--log-level debug`, se suportado) lista os plugins carregados sem erro.
- [ ] **Step 5:** Commit `deps/game_bethesda, deps/installer_*: Linux support` e push.

### Task 14: Benchmark de refresh e linha de base

**Files:**
- Create: `ModOrganizerLinux/benchmarks/gen_instance.py`
- Create: `modorganizer/tests/bench_refresh.cpp` (alvo `organizer_bench_refresh`, só com `MO2_BUILD_BENCHMARKS=ON`)
- Create: `ModOrganizerLinux/benchmarks/run.sh`, `ModOrganizerLinux/benchmarks/results/2026-10-phase1-baseline.md`

**Interfaces:**
- Produces: `gen_instance.py --mods 2000 --files 200000 --seed 1 <dir>` — cria `<dir>/mods/mod_NNNN/` com árvore estilo Bethesda (`meshes/`, `textures/`, `scripts/`, `interface/`), nomes com mistura de maiúsculas, ~10% de sobreposição de caminhos entre mods, arquivos de 0–4 KiB, e `<dir>/profiles/Default/modlist.txt` com todos habilitados. Determinístico pela `seed`.
- Produces: `organizer_bench_refresh <dir>` — constrói `DirectoryEntry` a partir dos mods em ordem de prioridade (mesmo caminho de código do `DirectoryRefresher`), 5 repetições, imprime `refresh_ms_median=<n>` e `peak_rss_kib=<n>`.

- [ ] **Step 1:** Escrever gerador e benchmark; `run.sh` gera a instância em dir temporário, executa e anexa o resultado (data, commit, CPU, kernel) em `results/`.
- [ ] **Step 2:** Rodar `benchmarks/run.sh` → arquivo de resultado com as duas métricas preenchidas.
- [ ] **Step 3:** Commit `bench: refresh benchmark and phase 1 baseline` (ambos os repos) e push.

### Task 15: Checklist manual e fechamento da fase

**Files:**
- Create: `ModOrganizerLinux/docs/testing/phase1-manual-checklist.md`
- Modify: `ModOrganizerLinux/README.md` (status da Fase 1, como baixar o artefato do CI e rodar no Bazzite via `distrobox`)

**Interfaces:**
- Consumes: artefato `install/` do CI (Tasks 2, 12 e 13).

- [ ] **Step 1:** Escrever o checklist (spec §4.7): criar instância Skyrim SE (detecção automática da biblioteca Steam; caminho manual se falhar); criar perfil; instalar 3 mods (`.7z`, `.zip`, `.rar`, um deles FOMOD); reordenar; conferir aba Data e conflitos (incluindo mods com `Textures`/`textures`); fechar e reabrir confirmando persistência; clicar “Executar” e ver a mensagem da Fase 3; abrir pasta do mod pelo menu de contexto.
- [ ] **Step 2:** Verificar critérios de pronto da spec §4.8 itens 1, 3, 4, 5 (o item 2 é executado pelo usuário no Bazzite).
- [ ] **Step 3:** Commit `docs: phase 1 manual checklist and status` e push; pedir ao usuário que rode o checklist no Bazzite.
