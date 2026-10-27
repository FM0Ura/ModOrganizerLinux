# Ícones de arquivos e executáveis no Linux — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** no Linux, a aba Data mostra um ícone por tipo de arquivo (com ícones próprios para tipos de jogo), e os `.exe` mostram o próprio ícone embutido, na aba Data, na lista de executáveis e no ícone do jogo.

**Architecture:** em `deps/uibase`, o leitor PE existente (`peversion_linux.cpp`) ganha a leitura do grupo de ícones (`RT_GROUP_ICON`/`RT_ICON`), que monta um `.ico` em memória; `iconForExecutable` no Linux o decodifica com `QImageReader` (plugin ICO do Qt) e guarda em cache. No MO2, funções puras (`fileicons.h/.cpp`, em `organizer_desktop`, testadas) dão o ícone de um tipo de jogo e os nomes de ícone MIME de uma extensão; o `IconFetcher` passa a usá-las no Linux.

**Tech Stack:** C++20/Qt6 (Core, Gui), gtest, Python 3 (gerador da fixture). Build e testes no distrobox `mo2-run` (`./build.sh`).

**Spec:** `docs/superpowers/specs/2026-10-27-linux-icons-design.md`

## Global Constraints

- Código novo só no Linux (`#ifndef _WIN32`, arquivos `*_linux.cpp` ou bibliotecas Linux-only). O Windows não muda.
- O arquivo PE é entrada não confiável: todo deslocamento e tamanho é checado (mesmo padrão de `peversion_linux.cpp`: `fits`, `Reader::read`, limites de contagem). Arquivo malformado → `nullopt`/ícone genérico, nunca crash.
- Ícone embutido: o **primeiro** grupo de `RT_GROUP_ICON` (tipo 14), primeira língua; as imagens vêm de `RT_ICON` (tipo 3) pelo `nId` de cada entrada do grupo.
- Sem ícone embutido, ou erro: `QIcon::fromTheme("application-x-executable")` (comportamento atual).
- `.dll` usa o ícone da tabela de tipos (biblioteca), **não** o embutido. Só `.exe` usa o embutido; `.ico` mostra a própria imagem; `.lnk` fica genérico.
- Ordem para uma extensão: tabela de tipos de jogo → ícones MIME (`iconName`, depois `genericIconName`) → ícone genérico de arquivo. Pastas: genérico de pasta.
- Tabela (nomes de tema em ordem de preferência; o primeiro que `QIcon::hasThemeIcon` aceitar; senão a reserva do qrc, se houver; senão segue a ordem acima):

  | Extensões | Nomes de tema | Reserva qrc |
  |---|---|---|
  | esp, esm, esl | `plugins` | `:/MO/gui/plugins` |
  | bsa, ba2 | `application-x-archive` | `:/MO/gui/package` |
  | nif, kf, hkx | `application-x-blender` | — |
  | pex, psc | `text-x-script` | — |
  | dds | `image-x-generic` | — |
  | ini | `configure`, `settings-configure` | — |
  | dll, asi | `application-x-sharedlib` | — |
  | pak, utoc, ucas | `package-x-generic` | `:/MO/gui/package` |

  Comparação de extensão sem diferenciar maiúsculas, sem o ponto.
- Arquivos dentro do VFS: ler sempre pelo caminho real (`item.realPath()`, como hoje).
- `*_en.ts` regenerados pelo build nunca entram em commit (`git checkout -- '*.ts'`).
- Comandos: `distrobox enter mo2-run -- bash -c "cd <worktree> && ..."`. O smoke test falha se o Mod Organizer do usuário estiver aberto (`pgrep -af ModOrganizer`).
- Commits terminam com:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01SY1tkB5pkSTaJybLJ4mWMg
  ```

## Review Focus

- Grupo de ícones cujo `nId` aponta para um `RT_ICON` inexistente ou com tamanho declarado maior que o real: a entrada é ignorada (ou tudo vira `nullopt`), sem ler fora do arquivo. Teste `PeIcon.MalformedIsRejected` (Task 1).
- `.exe` sem recurso de ícone (o `version.exe` existente): `readPeIcon` → `nullopt`, `iconForExecutable` → genérico. Teste `PeIcon.NoIconResource` (Task 1).
- Extensão em maiúsculas (`.ESP`, `.DDS`): mesmo ícone das minúsculas. Teste `FileIcons.CaseInsensitive` (Task 3).
- Extensão desconhecida do banco MIME (`.xyz123`): sem nomes MIME → ícone genérico. Teste `FileIcons.UnknownExtensionHasNoMimeIcon` (Task 3).
- O mesmo `.exe` mudando no disco durante a sessão (mod atualizado): o cache usa caminho + data de modificação + tamanho, então o ícone novo aparece. Teste `IconForExecutable.CacheFollowsFileChanges` (Task 2).

---

### Task 1: Ícone embutido no PE (`readPeIcon`)

**Files:**
- Modify: `deps/uibase/src/linux/peversion_linux.h/.cpp` (extrair de `readVersionResource` a parte genérica: cabeçalhos PE, tabela de seções e árvore de recursos, para servir aos dois leitores; sem mudar o comportamento de `readPeVersionInfo`)
- Create: `deps/uibase/tests/data/make_icon_exe.py` (gerador, Python 3 puro, sem MinGW: escreve um PE32+ mínimo com uma seção `.rsrc` contendo `RT_GROUP_ICON` id 1 com duas imagens `RT_ICON` ids 1 e 2: 16×16 e 32×32, ambas PNG de cor sólida, 32 bits) e `deps/uibase/tests/data/icon.exe` (o binário gerado, commitado, sem bit de execução)
- Test: `deps/uibase/tests/test_linux_utility.cpp`

**Interfaces:**
- Produces: `std::optional<QByteArray> MOBase::details::readPeIcon(const QString& path);` — o conteúdo de um arquivo `.ico` (ICONDIR + ICONDIRENTRYs + imagens) montado a partir do primeiro grupo; `nullopt` se o arquivo não puder ser lido, não for PE, não tiver `RT_GROUP_ICON` ou nenhuma imagem do grupo for legível. Limites: no máximo 64 entradas por grupo e 1 MiB por imagem (entradas acima disso são ignoradas).

- [ ] **Step 1: Write the failing tests** (suite `PeIcon`):
  - `ReadsGroup`: `readPeIcon(fixture("icon.exe"))` tem valor; bytes 0–5 = reservado 0, tipo 1, contagem 2; `QImageReader` sobre um `QBuffer` com esses bytes e formato `"ico"` tem `imageCount() == 2`, e lendo as duas imagens obtém tamanhos `{16×16, 32×32}` (em qualquer ordem). Se `QImageReader::supportedImageFormats()` não contiver `"ico"`, `GTEST_SKIP()` só da parte do `QImageReader` (o cabeçalho continua testado).
  - `NoIconResource`: `readPeIcon(fixture("version.exe"))` == `nullopt`; arquivo texto → `nullopt`; caminho inexistente → `nullopt`.
  - `MalformedIsRejected`: como `FileVersion.MalformedPeIsRejected`: toda truncagem de `icon.exe` em passos de 7 bytes e a corrupção de cada campo de 32 bits com valores grandes não travam nem leem fora do arquivo (o teste só exige não crashar e, quando houver valor, que comece com `00 00 01 00`).
- [ ] **Step 2: Run, expect failure**: `./build.sh --only uibase --tests` → falha de compilação (`readPeIcon` não existe).
- [ ] **Step 3: Implement** o gerador, a fixture e `readPeIcon`. O `.ico` montado: ICONDIR de 6 bytes; uma ICONDIRENTRY de 16 bytes por imagem legível, copiando largura, altura, cores, reservado, planos, bits e tamanho da GRPICONDIRENTRY (14 bytes) e trocando o `nId` de 2 bytes pelo deslocamento de 4 bytes da imagem; depois as imagens, na mesma ordem. O tamanho de cada imagem é o tamanho real do recurso `RT_ICON` (não o declarado no grupo).
- [ ] **Step 4: Run**: `./build.sh --only uibase --tests`; expected: `PeIcon.*` e os `FileVersion.*` existentes passam.
- [ ] **Step 5: Commit** `deps/uibase: Linux: read the icon of PE executables`.

### Task 2: `iconForExecutable` no Linux

**Files:**
- Modify: `deps/uibase/src/linux/utility_linux.cpp` (`iconForExecutable`)
- Test: `deps/uibase/tests/test_linux_utility.cpp`

**Interfaces:**
- Consumes: `readPeIcon` (Task 1).
- Produces: `QIcon MOBase::iconForExecutable(const QString& filePath)` (assinatura existente, `utility.h:496`): no Linux, para um arquivo com ícone embutido, um `QIcon` com **todas** as imagens do `.ico` (uma `addPixmap`/`addFile` por imagem, via `QImageReader` com `jumpToNextImage`); senão `QIcon::fromTheme("application-x-executable")`. Cache em memória protegido por mutex (a função é chamada da thread do `IconFetcher` e da interface), chave = caminho absoluto + `lastModified` + tamanho; guarda também o resultado "sem ícone". Helper interno testável: `std::vector<QImage> MOBase::details::peIconImages(const QString& path)` (vazio se não houver), declarado em `peversion_linux.h`.

- [ ] **Step 1: Write the failing tests** (suite `IconForExecutable`, usando `peIconImages`, que não precisa de `QGuiApplication`; `GTEST_SKIP()` se não houver plugin `ico`):
  - `ImagesFromFixture`: `peIconImages(fixture("icon.exe"))` tem 2 imagens não nulas, tamanhos {16×16, 32×32}.
  - `NoIconGivesNoImages`: `peIconImages(fixture("version.exe"))` vazio.
  - `CacheFollowsFileChanges`: copiar `icon.exe` para um diretório temporário; `peIconImages(copy)` tem 2 imagens; sobrescrever a cópia com `version.exe` e ajustar a data de modificação para 1 s depois; `peIconImages(copy)` vazio.
- [ ] **Step 2: Run, expect failure**: `./build.sh --only uibase --tests`.
- [ ] **Step 3: Implement** `peIconImages` (com o cache) e `iconForExecutable` sobre ele.
- [ ] **Step 4: Run**: `./build.sh --only uibase --tests`; expected: passa.
- [ ] **Step 5: Commit** `deps/uibase: Linux: icons of executables from their PE resources`.

### Task 3: Ícones por tipo de arquivo (`fileicons`)

**Files:**
- Create: `deps/modorganizer/src/fileicons.h`, `deps/modorganizer/src/fileicons.cpp`
- Modify: `deps/modorganizer/src/CMakeLists.txt` (adicionar `fileicons.cpp` em `_organizer_desktop_sources`)
- Test: `deps/modorganizer/tests/test_fileicons.cpp`

**Interfaces:**
- Produces (namespace `fileicons`):
  - `struct GameFileIcon { QStringList themeNames; QString fallbackResource; };`
  - `std::optional<GameFileIcon> gameFileIcon(QStringView extension);` — `extension` sem o ponto; a tabela das Global Constraints, sem diferenciar maiúsculas.
  - `QStringList mimeIconNames(QStringView extension);` — `QMimeDatabase().mimeTypeForFile("x." + extension, QMimeDatabase::MatchExtension)`; se o tipo for o padrão (`isDefault()`), lista vazia; senão `[iconName(), genericIconName()]` sem vazios nem repetidos.

- [ ] **Step 1: Write the failing tests** (suite `FileIcons`):
  - `GameTypes`: `gameFileIcon(u"esp")->themeNames == QStringList{"plugins"}` e `fallbackResource == ":/MO/gui/plugins"`; `bsa` → `{"application-x-archive"}`, `":/MO/gui/package"`; `nif` → `{"application-x-blender"}`, `""`; `ini` → `{"configure","settings-configure"}`; `dll` → `{"application-x-sharedlib"}`; `utoc` → `{"package-x-generic"}`, `":/MO/gui/package"`; `txt` → `nullopt`.
  - `CaseInsensitive`: `gameFileIcon(u"ESP")` e `gameFileIcon(u"Dds")` iguais aos de minúsculas.
  - `MimeNames`: `mimeIconNames(u"txt")` não vazio e contém `"text-plain"` ou `"text-x-generic"`; `mimeIconNames(u"png")` contém um nome começando com `"image-"`.
  - `UnknownExtensionHasNoMimeIcon`: `mimeIconNames(u"xyz123")` vazio.
- [ ] **Step 2: Run, expect failure**: `./build.sh --only modorganizer --tests` (compilação).
- [ ] **Step 3: Implement** `fileicons.h/.cpp`.
- [ ] **Step 4: Run**: `./build.sh --only modorganizer --tests`; expected: `FileIcons.*` e o resto passam.
- [ ] **Step 5: Commit** `deps/modorganizer: Linux: icons of file types, with game types`.

### Task 4: `IconFetcher` no Linux, checklist e AppImage

**Files:**
- Modify: `deps/modorganizer/src/iconfetcher.h/.cpp`
- Create: `docs/testing/linux-icons-manual-checklist.md`

**Interfaces:**
- Consumes: `fileicons::gameFileIcon`, `fileicons::mimeIconNames` (Task 3); `MOBase::iconForExecutable` (Task 2).
- Produces: nenhuma interface nova; `IconFetcher::icon(path)` muda o resultado no Linux.

Comportamento (Linux; Windows sem mudança):
- `hasOwnIcon`: só `.exe` e `.ico` (`.lnk` sai e passa a ir pela extensão → genérico).
- Ícone de arquivo próprio (`checkCache` de `m_fileCache`): `.exe` → `iconForExecutable(path).pixmap(m_iconSize)`; `.ico` → `QIcon(path).pixmap(m_iconSize)`; pixmap nulo → `m_quickCache.file`.
- Ícone de extensão (`checkCache` de `m_extensionCache`, `ext` com o ponto): `gameFileIcon(ext sem ponto)` → primeiro nome com `QIcon::hasThemeIcon`, senão `QIcon(fallbackResource)` se não vazio; se ainda nada, `mimeIconNames` → primeiro com `hasThemeIcon`; senão `m_quickCache.file`.
- Mantém a thread, as filas e os caches atuais.

- [ ] **Step 1: Implement** o comportamento acima.
- [ ] **Step 2: Build and smoke**: `pgrep -af ModOrganizer` sem o MO2 do usuário; `./build.sh --only uibase --only modorganizer --tests`; expected: testes e smoke passam.
- [ ] **Step 3: Write** `docs/testing/linux-icons-manual-checklist.md` com os testes manuais do §5 do spec (1–5) mais: 6. `.lnk` e extensão desconhecida com ícone genérico; 7. trocar de tema do KDE não é necessário — anotar qual tema estava ativo.
- [ ] **Step 4: Build the local AppImage** (`MO2_VERSION=2.5.2.4`; mesmo procedimento do branch da aba Nexus: `build/appimage/build-appimage.sh $PWD/install $PWD/appimage-out`, com `BUILD_OUT` do checkout principal se o worktree não tiver o libloot) e verificar dentro do AppDir que existem `usr/plugins/imageformats/libqico.so` e o tema de ícones usado (`breeze`/`breeze-dark`) ou registrar que o AppImage usa o tema do sistema.
- [ ] **Step 5: Commit** `deps/modorganizer: Linux: file and executable icons in the Data tab`, com o checklist.
