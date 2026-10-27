# Página da Nexus no painel lateral — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** um ícone de documento ao lado do checkbox de cada mod da Nexus abre, numa aba "Nexus" da lateral esquerda (ao lado da aba "Filtros"), a imagem principal e a descrição da página do mod.

**Architecture:** funções puras (`nexuspage.h/.cpp`: nome do arquivo de cache, decisão de baixar, HTML da página, texto de estado, marca de requisição do painel) entram na biblioteca `organizer_desktop` e são testadas no `organizer_tests`. Sobre elas: `NexusImageCache` (download e cache das imagens), `NexusPagePanel` (widget da aba, com `QWebEngineView` criado na primeira exibição), mudanças em `ModInfo`/`ModInfoRegular` (`nexusPictureUrl`, falha sem popup para o painel), o ícone na lista de mods (`ModList` + `ModListView`) e o `QTabWidget` na `MainWindow`.

**Tech Stack:** C++20/Qt6 (Widgets, WebEngineWidgets, Network), gtest. Build e testes no distrobox `mo2-run` (`./build.sh`).

**Spec:** `docs/superpowers/specs/2026-10-27-nexus-page-panel-design.md`

## Global Constraints

- Arquivos e símbolos novos no estilo do código ao redor (inglês em código, comentários e textos de interface). **Não existe fluxo de tradução pt-BR no projeto** (só `organizer_en.ts`), então os textos ficam só em inglês (desvio do §8 do spec, por falta do fluxo).
- `*_en.ts` regenerados pelo build nunca entram em commit (`git checkout -- '*.ts'` antes de cada commit).
- O ícone só aparece para `ModInfoRegular` com `nexusId() > 0` (não separador, Overwrite, foreign, backup).
- Regra de atualização: `needsDescriptionUpdate()` existente (mais de 1 dia desde `lastNexusQuery`). O botão Refresh força.
- Requisições do painel: `userData` = `QVariantMap{{"source", "nexusPanel"}}`. Elas não abrem popup em erro.
- O painel nunca envia requisição sem `NXMAccessManager::validated()`, e nunca provoca `needLogin()`.
- Tempo limite de resposta: **30 s**.
- Cache de imagens: `Settings::instance().paths().cache() + "/nexus-images"`. Arquivo `<gameName>_<modId>.<ext>` e, ao lado, `<gameName>_<modId>.url` com a URL de origem.
- Links da descrição abrem com `shell::Open(url)`, nunca dentro do painel.
- Trocar de aba não altera filtros (`m_filters`/`categoriesGroup` ficam intactos, só mudam de contêiner).
- Comandos: `distrobox enter mo2-run -- bash -c "cd <worktree> && ..."`. O smoke test falha se o Mod Organizer do usuário estiver aberto (`pgrep -af ModOrganizer` deve estar vazio).
- Commits terminam com:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01SY1tkB5pkSTaJybLJ4mWMg
  ```

## Review Focus

- URL de imagem com query string ou sem extensão (`…/image.png?v=2`, `…/image`): o arquivo deve sair `.png` e `.jpg`. Teste `NexusPage.CacheFileExtension` (Task 1).
- Descrição com `%1`, `%2` no texto do autor (comum em BBCode): o HTML não pode ser corrompido por `QString::arg` em cascata. Teste `NexusPage.HtmlKeepsPercentPlaceholders` (Task 1).
- Resposta da Nexus que chega depois de trocar de mod: os dados salvos mudam, a tela não. Teste manual 9 (Task 6), e o painel compara o `ModInfo::Ptr` atual antes de recarregar (Task 4).
- Clique no ícone com seleção múltipla ativa ou começando um arrasto: a seleção não muda e nada é arrastado. Teste manual 2 (Task 6), e o tratamento de clique consome o evento (Task 5).
- Mod apagado com o painel aberto: o painel limpa, sem crash por `ModInfo::Ptr` pendurado. Teste manual 10 (Task 6), e `MainWindow` limpa o painel em `modRemoved`/refresh da lista (Task 6).

---

### Task 1: Funções puras da página (`nexuspage`)

**Files:**
- Create: `deps/modorganizer/src/nexuspage.h`, `deps/modorganizer/src/nexuspage.cpp`
- Modify: `deps/modorganizer/src/CMakeLists.txt` (adicionar `nexuspage.cpp` em `_organizer_desktop_sources`)
- Test: `deps/modorganizer/tests/test_nexuspage.cpp`

**Interfaces:**
- Produces (namespace `nexuspage`):
  - `QString cacheFileName(const QString& gameName, int modId, const QString& pictureUrl)`: `"<gameName>_<modId>.<ext>"`. `ext` é o sufixo do caminho da URL (sem query), em minúsculas, se estiver em `{jpg, jpeg, png, webp, gif}`; senão `jpg`.
  - `bool needsImageDownload(const QString& pictureUrl, const QString& cachedFromUrl, bool fileExists)`: `!pictureUrl.isEmpty() && (!fileExists || cachedFromUrl != pictureUrl)`.
  - `QString pageHtml(const QString& pictureFileUrl, const QString& headerHtml, const QString& bodyHtml)`: documento HTML completo com o mesmo CSS de `modinfodialognexus.cpp:225-240` (sans-serif, 14px, fundo `#404040`, texto `#f1f1f1`), sem o `max-width: 1060px` (o painel é estreito), e `img { max-width: 100%; height: auto; }`. Se `pictureFileUrl` estiver vazio, não há `<img>` de cabeçalho. A montagem é por concatenação, sem `QString::arg` sobre o conteúdo.
  - `QString descriptionDocument(const QString& bodyHtml)`: o HTML exato que a aba Nexus da janela de informações usa hoje (o CSS de lá, com `max-width`). Substitui o literal em `modinfodialognexus.cpp`.
  - `QString statusText(const QDateTime& nexusLastModified, const QDateTime& lastQuery, const QDateTime& now)`: `"Updated on Nexus %1 · checked %2"`. Cada data vira `"today"`/`"yesterday"` se for o dia de `now`/o anterior (no fuso local), senão `QLocale().toString(date.toLocalTime().date(), QLocale::ShortFormat)`. Data inválida: aquela parte sai `"never"`.
  - `QVariant panelRequestTag()` → `QVariantMap{{"source","nexusPanel"}}`; `bool isPanelRequest(const QVariant& userData)`.

- [ ] **Step 1: Write the failing tests** em `test_nexuspage.cpp` (suite `NexusPage`):
  - `CacheFileExtension`: `cacheFileName("skyrimspecialedition", 42, "https://x/a/b.PNG?v=2") == "skyrimspecialedition_42.png"`; `"…/image"` → `_42.jpg`; `"…/pic.webp"` → `.webp`; `"…/f.exe"` → `.jpg`.
  - `NeedsImageDownload`: `("u","",false)` true; `("u","u",true)` false; `("u2","u",true)` true; `("","",false)` false.
  - `HtmlWithAndWithoutPicture`: `pageHtml("file:///c/x.jpg","<h1>H</h1>","<p>B</p>")` contém `<img src="file:///c/x.jpg"`, `<h1>H</h1>`, `<p>B</p>`; `pageHtml("", …)` não contém `<img`.
  - `HtmlKeepsPercentPlaceholders`: `pageHtml("", "%1", "a %1 b %2")` contém `a %1 b %2` e o header `%1` sem mudança.
  - `DescriptionDocumentKeepsCss`: `descriptionDocument("<p>x</p>")` contém `max-width: 1060px` e `<body><p>x</p></body>`.
  - `StatusText`: com `now` = 2026-10-27 12:00 local: modified 2026-10-27 → começa `"Updated on Nexus today"`; query 2026-10-26 → termina `"checked yesterday"`; query inválida → `"checked never"`.
  - `PanelTag`: `isPanelRequest(panelRequestTag())` true; `isPanelRequest(QVariant())` false; `isPanelRequest(QVariant(1))` false.
- [ ] **Step 2: Run, expect a compile failure** (`nexuspage.h` doesn't exist): `distrobox enter mo2-run -- bash -c "cd $PWD && ./build.sh --only modorganizer --tests"`.
- [ ] **Step 3: Implement** `nexuspage.h/.cpp` com as assinaturas acima.
- [ ] **Step 4: Run** o mesmo comando; expected: `organizer_tests` passa, incluindo `NexusPage.*`.
- [ ] **Step 5: Commit** `deps/modorganizer: Linux: pure helpers of the Nexus page panel`.

### Task 2: `ModInfo` — URL da imagem, refresh forçado e falha sem popup

**Files:**
- Modify: `deps/modorganizer/src/modinfo.h` (perto de `setNexusDescription`/`getNexusDescription`, `:872,889`, e `signals:` `:981`)
- Modify: `deps/modorganizer/src/modinforegular.h/.cpp` (`:99,272` meta.ini; `nxmDescriptionAvailable :354`; `nxmRequestFailed :411`)
- Modify: `modinfoforeign.h`, `modinfooverwrite.h`, `modinfobackup.h`, `modinfoseparator.h` (implementações vazias, como as de `getNexusDescription`)
- Modify: `deps/modorganizer/src/mainwindow.cpp:3442` (gravar também `picture_url`)
- Modify: `deps/modorganizer/src/modinfodialognexus.cpp:225-316` (usar `nexuspage::descriptionDocument`)

**Interfaces:**
- Consumes: `nexuspage::panelRequestTag()`, `nexuspage::isPanelRequest()`, `nexuspage::descriptionDocument()` (Task 1).
- Produces:
  - `virtual void ModInfo::setNexusPictureUrl(const QString& url) = 0;` / `virtual QString ModInfo::getNexusPictureUrl() const = 0;` (meta.ini `nexusPictureUrl`).
  - `virtual void ModInfo::requestNexusInfo(const QVariant& userData) {}`: em `ModInfoRegular`, `m_NexusBridge.requestDescription(m_GameName, m_NexusID, userData)` se `m_NexusID > 0`, sem checar a regra de 1 dia.
  - signal `void ModInfo::nexusRequestFailed(const QString& message);`: emitido por `nxmRequestFailed` **só** para requisições com `isPanelRequest(userData)`. Nesse caso não há `MessageDialog`. `modDetailsUpdated(false)` continua sendo emitido em todos os casos.
  - `nxmDescriptionAvailable` e `mainwindow.cpp:3442` gravam `result["picture_url"].toString()` em `setNexusPictureUrl`.

- [ ] **Step 1: Implement** os itens acima. `setNexusPictureUrl` marca `m_MetaInfoChanged` como `setNexusDescription` faz.
- [ ] **Step 2: Build**: `./build.sh --only modorganizer --tests`; expected: compila e `organizer_tests` passa.
- [ ] **Step 3: Check the info dialog** (manual, sem o MO2 do usuário aberto): a janela de informações de um mod → aba Nexus mostra a descrição como antes.
- [ ] **Step 4: Commit** `deps/modorganizer: Linux: keep the Nexus picture URL; Nexus requests without a popup`.

### Task 3: `NexusImageCache`

**Files:**
- Create: `deps/modorganizer/src/nexusimagecache.h`, `deps/modorganizer/src/nexusimagecache.cpp` (alvo `organizer`, com AUTOMOC; adicionar ao grupo de arquivos da aplicação em `src/CMakeLists.txt`, junto de `modinfodialognexus`)

**Interfaces:**
- Consumes: `nexuspage::cacheFileName`, `nexuspage::needsImageDownload` (Task 1).
- Produces: `class NexusImageCache : public QObject`
  - `explicit NexusImageCache(QString cacheDir, QObject* parent = nullptr);`
  - `QString cachedFile(const QString& gameName, int modId, const QString& pictureUrl) const;` → caminho absoluto se o arquivo existir, senão `{}`.
  - `void fetch(const QString& gameName, int modId, const QString& pictureUrl);`: se `needsImageDownload(pictureUrl, <conteúdo do .url>, <arquivo existe>)`, baixa com `QNetworkAccessManager` (redirecionamentos seguidos, sem API key). Grava em `<arquivo>.part`, renomeia para o nome final e grava o `.url`. Ignora um segundo `fetch` da mesma chave em andamento. Cria a pasta se faltar.
  - signals `void imageReady(const QString& gameName, int modId, const QString& filePath);` (também quando já estava em cache e não precisou baixar) e `void imageFailed(const QString& gameName, int modId);`.

- [ ] **Step 1: Implement** a classe. Erro de rede ou HTTP ≠ 2xx emite `imageFailed`, apaga o `.part` e não toca no arquivo antigo.
- [ ] **Step 2: Build**: `./build.sh --only modorganizer --tests`; expected: compila.
- [ ] **Step 3: Commit** `deps/modorganizer: Linux: cache of the Nexus pictures`.

### Task 4: `NexusPagePanel` (a aba Nexus)

**Files:**
- Create: `deps/modorganizer/src/nexuspagepanel.h`, `deps/modorganizer/src/nexuspagepanel.cpp` (alvo `organizer`, junto da Task 3)

**Interfaces:**
- Consumes: `NexusImageCache` (Task 3); `ModInfo::requestNexusInfo`, `getNexusPictureUrl`, `nexusRequestFailed`, `modDetailsUpdated`, `needsDescriptionUpdate`/`updateNXMInfo` (Task 2 e existentes); `nexuspage::pageHtml`, `statusText`, `panelRequestTag` (Task 1); `BBCode::convertToHTML`; `NexusInterface::instance().getAccessManager()->validated()` e o sinal `NXMAccessManager::credentialsReceived`; `NexusInterface::instance().getModURL(modId, gameName)`; `NexusTabWebpage` (`modinfodialognexus.h`) para os links.
- Produces: `class NexusPagePanel : public QWidget`
  - `explicit NexusPagePanel(QWidget* parent = nullptr);`
  - `void setMod(ModInfo::Ptr mod);`, `void clear();`, `ModInfo::Ptr mod() const;`
  - signal `void modChanged(ModInfo::Ptr mod);` (para a lista destacar o ícone; `nullptr` ao limpar).

Comportamento (spec §3.3–3.5):
- Layout: área de conteúdo (um `QWebEngineView` criado no primeiro `showEvent` ou no primeiro `setMod` com o widget visível, o que vier antes), e embaixo uma linha com `QPushButton` "Refresh", `QPushButton` "Open on Nexus" e um `QLabel` de estado (com quebra de linha).
- Sem mod: texto "Click the document icon next to a mod to see its Nexus page here." Botões desabilitados.
- `setMod`: desconecta os sinais do mod anterior e conecta `modDetailsUpdated`/`nexusRequestFailed` do novo. Mostra o salvo na hora: header `<h2>nome</h2><p>autor · versão</p>` (com `toHtmlEscaped`), body `BBCode::convertToHTML(getNexusDescription())`, imagem = `cachedFile(...)` como `QUrl::fromLocalFile`. Depois:
  - sem login validado: body "Log in to Nexus (Settings → Nexus) to see this mod's page." e nenhuma requisição; guarda que está esperando login;
  - descrição vazia ou `needsDescriptionUpdate()`: `requestNexusInfo(panelRequestTag())`, Refresh desabilitado, timer de 30 s; com descrição vazia, body "Loading from Nexus…";
  - `getNexusPictureUrl()` não vazio: `imageCache.fetch(...)`.
- `modDetailsUpdated(true)` do mod atual: para o timer, reabilita Refresh, recarrega o conteúdo e chama `fetch` (a URL pode ter mudado). Sinais de outros mods são ignorados por construção (só o atual fica conectado). Se a resposta chegar depois do tempo limite, recarrega normalmente.
- `nexusRequestFailed(msg)`: para o timer, reabilita Refresh, estado `"Couldn't update: " + msg`. O conteúdo salvo fica.
- Tempo limite: estado "Nexus didn't respond", Refresh reabilitado.
- `credentialsReceived`: se estiver esperando login, refaz `setMod(mod())`.
- `imageReady` com a chave do mod atual: recarrega o HTML com a imagem. `imageFailed`: nada (a área da imagem fica ausente).
- Estado normal: `nexuspage::statusText(getNexusLastModified(), getLastNexusQuery(), QDateTime::currentDateTime())`.
- Refresh: `requestNexusInfo(panelRequestTag())` + timer. Open on Nexus: `shell::Open(QUrl(getModURL(...)))`.
- `linkClicked` do `NexusTabWebpage` → `shell::Open(url)`.

- [ ] **Step 1: Implement** a classe.
- [ ] **Step 2: Build**: `./build.sh --only modorganizer --tests`; expected: compila.
- [ ] **Step 3: Commit** `deps/modorganizer: Linux: Nexus page panel widget`.

### Task 5: Ícone na lista de mods

**Files:**
- Modify: `deps/modorganizer/src/modlist.h/.cpp` (`data()`, `Qt::DecorationRole`, `modlist.cpp:345`)
- Modify: `deps/modorganizer/src/modlistview.h/.cpp` (tratamento do clique; respeitar o deslocamento de `visualRect`, `modlistview.cpp:909-918`)

**Interfaces:**
- Consumes: nada das tasks anteriores.
- Produces:
  - `void ModList::setNexusPageMod(std::optional<unsigned int> modIndex);`: guarda o índice e emite `dataChanged` da coluna `COL_NAME` das linhas antiga e nova.
  - Em `data()`, `Qt::DecorationRole` e `column == COL_NAME`: para `ModInfoRegular` (não separador/overwrite/foreign/backup) com `nexusId() > 0`, `QIcon::fromTheme("document-preview", QIcon(":/MO/gui/emblem_notes"))`. O mod de `setNexusPageMod` recebe a versão `QIcon::Selected` do mesmo ícone (`icon.pixmap(size, QIcon::Selected)` num `QIcon` novo) para ficar destacado. Tooltip da coluna inalterado; o tooltip "View Nexus page" vem da view ao passar sobre o ícone (`helpEvent`/`viewportEvent` com `QEvent::ToolTip`).
  - signal `void ModListView::nexusPageRequested(unsigned int modIndex);`
  - Clique: em `ModListView::mousePressEvent`, se o botão esquerdo cair dentro do retângulo de decoração de uma linha com ícone (`style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &opt, this)` com `opt` iniciado pelo delegate para aquele índice e o rect de `visualRect`), emite `nexusPageRequested(modIndex)` e **consome** o evento (sem chamar a base: a seleção não muda, não começa arrasto, o checkbox não alterna). `mouseReleaseEvent`/`mouseDoubleClickEvent` no mesmo retângulo também são consumidos.

- [ ] **Step 1: Implement** os itens acima. A reserva `:/MO/gui/emblem_notes` (`resources/accessories-text-editor.png`, `resources.qrc:61`) é o mesmo desenho do indicador de notas; se, no AppImage, a reserva aparecer no lugar de `document-preview` e confundir com esse indicador, adicionar um PNG próprio ao qrc com alias `nexus_page`.
- [ ] **Step 2: Build**: `./build.sh --only modorganizer --tests`; expected: compila.
- [ ] **Step 3: Commit** `deps/modorganizer: Linux: Nexus page icon in the mod list`.

### Task 6: Abas na lateral esquerda, ligação e checklist

**Files:**
- Modify: `deps/modorganizer/src/mainwindow.ui` (`categoriesSplitter`, `:38-45`: o `categoriesGroup` passa a ser a 1ª página de um `QTabWidget` `leftPanelTabs`, aba "Filters"; 2ª página vazia "Nexus", preenchida no código)
- Modify: `deps/modorganizer/src/mainwindow.h/.cpp` (`setCategoryListVisible :3777`, restore/save `:2183,2252`)
- Create: `docs/testing/nexus-page-panel-manual-checklist.md`

**Interfaces:**
- Consumes: `NexusPagePanel` (Task 4), `NexusImageCache` (Task 3), `ModList::setNexusPageMod` + `ModListView::nexusPageRequested` (Task 5).
- Produces: `void MainWindow::showNexusPage(unsigned int modIndex);`

Comportamento (spec §3.2):
- `MainWindow` cria um `NexusImageCache(Settings::instance().paths().cache() + "/nexus-images")` e um `NexusPagePanel` na aba Nexus.
- `setCategoryListVisible(bool)` mostra/esconde `ui->leftPanelTabs` (não mais só `categoriesGroup`). `restoreVisibility`/`saveVisibility` passam a usar `leftPanelTabs`. Aba atual: `s.widgets().saveIndex(ui->leftPanelTabs)`/`restoreIndex`.
- `showNexusPage(i)`: se o painel estiver visível, a aba atual for Nexus e `panel->mod() == ModInfo::getByIndex(i)`: esconde o painel (`displayCategoriesBtn->setChecked(false)`). Senão: `setChecked(true)`, `leftPanelTabs->setCurrentWidget(panel)`, `panel->setMod(...)`.
- `panel->modChanged` → `modList->setNexusPageMod(índice ou nullopt)`.
- Mod removido ou refresh da lista de mods (os sinais que a `MainWindow` já trata para isso): se `panel->mod()` não existir mais em `ModInfo`, `panel->clear()`.

- [ ] **Step 1: Implement** as mudanças da `.ui` e da `MainWindow`.
- [ ] **Step 2: Build and smoke**: `pgrep -af ModOrganizer` vazio; `./build.sh --only modorganizer --tests`; expected: testes e smoke passam.
- [ ] **Step 3: Write** `docs/testing/nexus-page-panel-manual-checklist.md` com os itens 1–8 do §7 do spec mais:
  9. Clicar no ícone do mod A e, antes da resposta, no do mod B: o painel fica em B; o `meta.ini` de A recebe os dados novos.
  10. Com o painel aberto num mod, removê-lo: o painel volta à mensagem inicial, sem crash.
  11. Refresh com a rede desligada: o conteúdo fica e o estado mostra "Couldn't update: …", sem popup.
  12. Imagem: a primeira abertura baixa para `<cache>/nexus-images/`; a segunda não baixa de novo (o arquivo não muda de data).
- [ ] **Step 4: Build the local AppImage** (como no fluxo de release local: `scripts/`/`tests/appimage`; ver o README de distribuição) e fazer os itens 1–12 no AppImage.
- [ ] **Step 5: Commit** `deps/modorganizer: Linux: Filters and Nexus tabs on the left side`, com o checklist.
