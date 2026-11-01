# Modlists, parte 1: núcleo + `.mo2list` — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** exportar um perfil do MO2 como um arquivo `.mo2list` e importá-lo em outra máquina (instância nova ou perfil), com as mesmas escolhas de FOMOD, prioridade, ordem de plugins, INIs e executáveis. Os downloads são automáticos com Premium e guiados, um clique por mod, sem Premium.

**Architecture:** a lógica pura fica numa biblioteca estática nova, `organizer_modlists` (em `deps/modorganizer/src/modlists/`), testada em `organizer_tests`. Ela tem o plano e o seu JSON, os marcadores de caminho, o ZIP `.mo2list`, o export, a classificação, a ordem e o casamento de downloads. Sobre ela:
- uma API pequena em uibase (`IInstallationManager::installerOptions`) e o modo lote do `InstallationManager`;
- a gravação e a reaplicação das escolhas no `installer_fomod`;
- o orquestrador `ModlistInstaller`;
- as janelas: destino, revisão, downloads guiados, resumo e export;
- a criação da instância nova, com a importação retomada depois do reinício.

**Tech Stack:** C++20, Qt 6.11 (Widgets, Network, `Qt6::CorePrivate` para `QZipReader`/`QZipWriter`), gtest. Build e testes no distrobox `mo2-run` (`./build.sh`).

**Spec:** `docs/superpowers/specs/2026-10-28-modlists-design.md`. Este plano cobre a parte 1 (§10). A parte 2 (Collections, §7) terá o seu próprio plano.

## Global Constraints

- Branch `modlists-parte1`, criado a partir de `worktree-spec-modlists` (que é `main` 80754ca + o spec + este plano), num worktree próprio. O PR vai para `main` e o merge é por fast-forward, sem merge commit. Para sincronizar com `main`, use `git merge`, nunca rebase (os `deps/` têm squash de subtree).
- Os commits que mexem em `deps/<nome>` usam o prefixo `deps/<nome>: ` (exemplo: `deps/modorganizer: modlists: plan model`).
- Código, comentários e textos de interface em inglês, no estilo do código ao redor. O projeto não tem fluxo de tradução pt-BR.
- Os `*_en.ts` que o build regenera nunca entram em commit: rode `git checkout -- '*.ts'` antes de cada commit.
- `schemaVersion` atual = **1**. Um `.mo2list` com `schemaVersion` maior é recusado com "This modlist was made by a newer Mod Organizer. Update Mod Organizer to import it."
- A extensão é `.mo2list`. Dentro do ZIP: `manifest.json`, `profile/…`, `bundled/<mod>/…`, `README.md` opcional (§6.1).
- Marcadores de caminho: `%INSTANCE%`, `%GAME%`, `%PROTON_PREFIX%`, `%HOME%`, substituídos do caminho mais longo para o mais curto.
- Nunca vão no pacote: saves, `ModOrganizer.ini`, tokens, chave de API, logs (§6.2).
- Tipo de fonte desconhecido ou `schemaVersion` maior falham alto (`modlists::PlanError`), nunca em silêncio.
- O import nunca executa nada (§6.3).
- No modo lote não aparece nenhum diálogo modal. O que exigiria decisão vai para o resumo com "Install manually" (§5.2).
- Instância nova e perfil novo saem com INIs locais e saves locais ligados (§5.0).
- O destino não vem pré-marcado (§5.0).
- Os mods do usuário nunca são apagados nem reinstalados por cima. Uma versão diferente instala ao lado, com o sufixo ` [<lista>]`, e desativa a antiga só no perfil de destino (6b, §4).
- Comandos: `distrobox enter mo2-run -- bash -c "cd <worktree> && ./build.sh --only <alvo> --tests"`. O smoke test falha se o MO2 do usuário estiver aberto (`pgrep -af ModOrganizer` precisa estar vazio).
- Commits terminam com:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01SY1tk9wJzvLxVtcRLxpw4W
  ```

## Review Focus

- **Nome de mod com caracteres de caminho ou acentos** (`Dar's Overhaul / Fix`, `Câmara`): no ZIP, a pasta `bundled/<mod>/` precisa de um nome seguro e reversível, e o mod volta com o nome original. Teste `Mo2ListZip.BundledNameRoundTrip` (Task 3).
- **Caminho do home dentro do caminho do jogo** (o jogo em `/home/u/Games/Skyrim`, a instância em `/home/u/.local/share/...`): `%GAME%` e `%INSTANCE%` precisam ganhar de `%HOME%`. E um prefixo de mesmo começo (`/home/u` vs `/home/u2`) não pode virar marcador. Teste `PathMarkers.LongestFirstAndBoundaries` (Task 2).
- **Mesmo `nxm://` duas vezes, ou chegando depois de "Skip"** na janela de downloads guiados: não pode contar duas vezes nem ressuscitar um item pulado sem o usuário pedir. Teste `GuidedMatch.DuplicateAndSkipped` (Task 7).
- **FOMOD com página escondida por condição**: a página não aparece no registro e, na reaplicação, ela não é exigida. Uma página registrada que a reaplicação esconde falha alto. Teste `FomodChoices.HiddenPageNotRequired` (Task 5).
- **Import interrompido** (MO2 fechado durante a importação numa instância nova): ao reabrir, a importação pendente é oferecida de novo, sem duplicar os mods que já entraram. Teste `PendingImport.ResumeSkipsInstalled` (Task 7) e o item 9 do checklist manual (Task 12).

---

### Task 1: Biblioteca `organizer_modlists` e o plano em JSON

**Files:**
- Create: `deps/modorganizer/src/modlists/plan.h`, `deps/modorganizer/src/modlists/plan.cpp`
- Modify: `deps/modorganizer/src/CMakeLists.txt`. Uma lista `_organizer_modlists_sources` com os `.cpp` de `modlists/` que não são de interface (os desta task e das Tasks 2, 3, 6 e 7). Ela fica excluída do alvo `organizer` como as outras listas (`:160-211`) e vira `add_library(organizer_modlists STATIC …)`, com `mo2_configure_target(... NO_SOURCES WARNINGS 4 TRANSLATIONS OFF AUTOMOC OFF)`, PCH reutilizado de `organizer_datatree` e link com `Qt6::CorePrivate`. Ligue-a a `organizer` e a `organizer_tests`.
- Modify: `deps/modorganizer/tests/CMakeLists.txt` (link de `organizer_modlists`)
- Test: `deps/modorganizer/tests/test_modlistplan.cpp`

**Interfaces:**
- Produces (namespace `modlists`, `plan.h`):
  - `inline constexpr int SchemaVersion = 1;`
  - `enum class SourceKind { Nexus, Manual, Bundled };` No JSON, `"type": "nexus" | "manual" | "bundled"`.
  - `struct Source { SourceKind kind; int modId = 0; int fileId = 0; QString md5; qint64 size = -1; QString url; QString fileName; QString bundledDir; std::optional<QJsonArray> fomodChoices; };`. `bundledDir` é a pasta dentro de `bundled/`. `fomodChoices` usa o formato da Task 5.
  - `struct ModEntry { QString name; QString version; bool isSeparator = false; bool optional = false; bool enabled = true; std::vector<Source> sources; int phase = 0; QString vortexModType; };`
  - `struct OrderRule { QString mod; QString type; QString reference; };`. `type` ∈ `before`/`after`. Não é usado na parte 1, mas entra na ida e volta.
  - `struct PluginEntry { QString name; bool enabled = true; };`
  - `struct ExecutableEntry { QString title, binary, arguments, workingDirectory, steamAppId, proton; QStringList environment; bool showOnToolbar = false; };`. Os caminhos ficam com marcadores.
  - `struct ProfileConfig { bool localInis = true; bool localSaves = true; QStringList iniFiles; };`. `iniFiles` são os nomes dentro de `profile/`.
  - `struct Header { int schemaVersion = SchemaVersion; QString name, author, version, gameName, nexusDomain; QString origin = "mo2list"; QString collectionSlug; int collectionRevision = 0; };`
  - `struct Plan { Header header; std::vector<ModEntry> mods; std::vector<OrderRule> rules; bool pluginsUseLoot = false; std::vector<PluginEntry> plugins; ProfileConfig profile; std::vector<ExecutableEntry> executables; };`. `mods` vai da prioridade mais baixa para a mais alta, com os separadores no lugar deles.
  - `class PlanError : public std::runtime_error { public: enum class Kind { Invalid, NewerSchema, UnknownSource }; PlanError(Kind, const QString&); Kind kind() const; };`
  - `QJsonObject toJson(const Plan&);`, `Plan fromJson(const QJsonObject&);` (lança `PlanError`)

- [ ] **Step 1: Write the failing tests** (suite `ModlistPlan`):
  - `RoundTrip`: um plano com cabeçalho completo, um separador, um mod com duas fontes `nexus` (a segunda com `fomodChoices` = `[{"name":"Main","groups":[{"name":"Textures","choices":[{"name":"2K","idx":1}]}]}]`), um `manual` (url, fileName, md5, size), um `bundled`, plugins (um inativo), `pluginsUseLoot=false`, `profile`, e um executável com `environment`. O resultado de `fromJson(toJson(p))` é igual a `p`, campo a campo, e a ordem das fontes e dos mods se mantém.
  - `NewerSchemaRejected`: `schemaVersion: 2` lança `PlanError` com `kind() == NewerSchema`.
  - `UnknownSourceFailsLoud`: uma fonte `"type":"wabbajack"` lança `UnknownSource`, e a mensagem contém `wabbajack`.
  - `MissingRequiredField`: um mod sem `name` lança `Invalid`.
- [ ] **Step 2: Run, expect a compile failure:** `./build.sh --only modorganizer --tests` (no distrobox).
- [ ] **Step 3: Implement** `plan.h/.cpp` e a biblioteca no CMake. Ao serializar, campos com o valor padrão podem ser omitidos, mas `schemaVersion` e `type` sempre saem.
- [ ] **Step 4: Run.** Esperado: `organizer_tests` passa, incluindo `ModlistPlan.*`.
- [ ] **Step 5: Commit** `deps/modorganizer: modlists: plan model and JSON`.

### Task 2: Marcadores de caminho e busca de vazamentos

**Files:**
- Create: `deps/modorganizer/src/modlists/pathmarkers.h/.cpp` (em `_organizer_modlists_sources`)
- Test: `deps/modorganizer/tests/test_pathmarkers.cpp`

**Interfaces:**
- Produces:
  - `struct PathMarkers { QString instance, game, protonPrefix, home; };`. São caminhos absolutos, sem `/` final. Um campo vazio não entra na substituição.
  - `QString toMarkers(const QString& text, const PathMarkers&)`. Substitui do caminho mais longo para o mais curto, e só em fronteira de caminho: depois do prefixo vem `/`, `\`, aspas, espaço, `;`, fim de linha ou fim do texto. Também aceita a forma Wine `Z:\home\…` dos mesmos caminhos (`Z:` + caminho com `\`), e o marcador sai com o separador do original.
  - `QString fromMarkers(const QString& text, const PathMarkers&)`
  - `struct Leak { QString file; int line; QString excerpt; };`
  - `std::vector<Leak> findLeaks(const QString& file, const QString& text, const QString& userName, const QString& home)`. Procura `home` e o `userName` como palavra inteira, sem diferenciar maiúsculas. Ignora `userName` com menos de 3 caracteres.

- [ ] **Step 1: Write the failing tests** (suite `PathMarkers`):
  - `LongestFirstAndBoundaries`: com `home=/home/u`, `game=/home/u/Games/Skyrim` e `instance=/home/u/.local/share/ModOrganizer/SSE`, o texto `"/home/u/Games/Skyrim/Data;/home/u2/x;/home/u/.local/share/ModOrganizer/SSE/mods"` vira `"%GAME%/Data;/home/u2/x;%INSTANCE%/mods"`.
  - `WineForm`: `"Z:\\home\\u\\Games\\Skyrim\\SkyrimSE.exe"` vira `"%GAME%\\SkyrimSE.exe"`.
  - `RoundTrip`: `fromMarkers(toMarkers(t, m), m2)` com outro `m2` dá os caminhos de `m2`.
  - `FindLeaks`: um INI com `sPath=/home/fabio/x` na linha 3 devolve `{file, 3, …}`. Com `userName="fabio"`, `"Fabio's mod"` aparece como vazamento. Com `userName="ab"`, nada é procurado.
- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Esperado: `PathMarkers.*` passa.
- [ ] **Step 5: Commit** `deps/modorganizer: modlists: path markers and leak check`.

### Task 3: O arquivo `.mo2list`: gravar, ler, validar e achar conteúdo executável

**Files:**
- Create: `deps/modorganizer/src/modlists/mo2listzip.h/.cpp` (em `_organizer_modlists_sources`)
- Test: `deps/modorganizer/tests/test_mo2listzip.cpp`

**Interfaces:**
- Consumes: `Plan`, `toJson`/`fromJson` (Task 1).
- Produces:
  - `QString bundledDirName(const QString& modName)`: troca `/ \ : * ? " < > |` e caracteres de controle por `_`, corta espaços e pontos nas pontas e acrescenta `-<8 hex do sha1 do nome>`. Isso evita colisões. O nome original continua em `ModEntry::name`.
  - `struct PackageInput { Plan plan; std::map<QString, QByteArray> profileFiles; std::vector<std::pair<QString, QString>> bundledFiles; QString readme; };`. As chaves de `profileFiles` são relativas a `profile/`. Cada par de `bundledFiles` é (caminho no ZIP debaixo de `bundled/`, caminho absoluto no disco).
  - `void writeMo2List(const QString& path, const PackageInput&)`, com `QZipWriter`. Grava num `.part` e renomeia no fim. Lança `std::runtime_error` em erro de E/S.
  - `class Mo2ListPackage { public: static Mo2ListPackage open(const QString& path); const Plan& plan() const; QString readme() const; QByteArray profileFile(const QString& name) const; QStringList bundledEntries(const QString& bundledDir) const; void extractBundled(const QString& bundledDir, const QString& destDir) const; };`. `open` lança `PlanError(Invalid)` quando:
    - falta `manifest.json`;
    - uma entrada tem `..`, começa por `/` ou `\`, tem `X:`, ou é link simbólico (`QZipReader::FileInfo::isSymLink`).
    A mensagem cita a entrada.
  - `enum class ExecFinding::Kind { NativeFile, Executable, IniLoadsCode };` e `struct ExecFinding { Kind kind; QString what; QString detail; };`
  - `std::vector<ExecFinding> findExecutableContent(const Mo2ListPackage&)`:
    - arquivos `bundled` com extensão em {`dll`,`exe`,`bat`,`cmd`,`sh`,`ps1`,`py`,`so`,`jar`,`vbs`} (o `.pex` é script do jogo e fica fora) ou que começam com `MZ` ou `\x7fELF`;
    - todos os `plan.executables`, com binário, argumentos e ambiente em `detail`;
    - linhas de INIs do perfil cujo valor contém `.dll` ou um marcador de caminho.

- [ ] **Step 1: Write the failing tests** (suite `Mo2ListZip`, num `QTemporaryDir`):
  - `WriteThenOpen`: o plano volta igual, e `profileFile("Skyrim.ini")` e `readme()` também.
  - `BundledNameRoundTrip`: o mod `"Dar's Overhaul / Fix"` grava em `bundled/<bundledDirName>/a.txt`, e `bundledDirName` não contém `/`. Dois nomes que só diferem por `/` vs `:` dão pastas diferentes. `extractBundled` recria `a.txt` em `destDir`.
  - `RejectsTraversal`: ZIPs montados à mão com `QZipWriter`, com entradas `../evil`, `/abs`, `C:/x` e um link simbólico. Cada um lança `PlanError(Invalid)`.
  - `RejectsNewerSchema`: um `manifest.json` com `schemaVersion: 2` lança `NewerSchema`.
  - `FindsExecutableContent`: um `bundled` com `x.dll`, outro com um arquivo sem extensão que começa com `\x7fELF`, um executável no plano e `sDLL=foo.dll` num INI dão os três `Kind`s. Um pacote só com `.esp` e `.dds` não dá nenhum.
- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement.** `#include <QtCore/private/qzipreader_p.h>` e `qzipwriter_p.h`.
- [ ] **Step 4: Run.** Esperado: `Mo2ListZip.*` passa.
- [ ] **Step 5: Commit** `deps/modorganizer: modlists: .mo2list package`.

### Task 4: Opções para os instaladores, modo lote e ordem dos arquivos instalados

**Files:**
- Modify: `deps/uibase/include/uibase/iinstallationmanager.h`. Dois virtuais **não puros** no fim da classe, para não quebrar os bindings Python (`basic_classes.cpp:848` só amarra métodos):
  - `virtual QVariantMap installerOptions([[maybe_unused]] const QString& installerName) const { return {}; }`
  - `virtual void setInstallerDetail([[maybe_unused]] const QString& detail) {}`
- Modify: `deps/modorganizer/src/installationmanager.h/.cpp`
- Modify: `deps/modorganizer/src/modinforegular.h/.cpp` (`addInstalledFile :437`) e `modinfo.h` (virtual com a implementação vazia nas outras subclasses, como `installedFiles`)
- Modify: `deps/installer_quick/src/installerquick.cpp:144` (`batch` conta como `silent`)
- Create: `deps/modorganizer/src/modlists/installorder.h/.cpp` (em `_organizer_modlists_sources`)
- Test: `deps/modorganizer/tests/test_installorder.cpp`

**Interfaces:**
- Produces:
  - `modlists::fileKey(int modId, int fileId) -> QString`: `"<modId>_<fileId>"`. Com o id desconhecido (0), é `"archive_<base64url(sha1(nome do arquivo))[0..12]>"`, pela sobrecarga `fileKey(const QString& archiveName)`.
  - `modlists::appendInstallOrder(QStringList order, const QString& key) -> QStringList`: remove `key` se já estiver na lista e o põe no fim.
  - `ModInfoRegular`: `QStringList installedFilesOrder() const`. É gravado no `meta.ini` como `installedFilesOrder` (lista de `fileKey`) e atualizado em `addInstalledFile` com `appendInstallOrder`.
  - `InstallationManager::setBatch(bool)` / `bool batch() const`, e `setInstallerOptions(const QString& installer, QVariantMap)`, `clearInstallerOptions()`, `QString lastInstallerDetail() const`.
  - `installerOptions(name)` devolve as opções dadas para aquele instalador mais, sempre, `fileKey` (da `.meta` do download em `install()`, `:537-554`) e `batch` (bool).
  - No modo lote, `install()`:
    - só considera instaladores com `isManualInstaller() == false` cujo `name()` está em {`Fomod Installer`, `Simple Installer`, `Bundle Installer`} ou que tenham `batch` em `installerOptions`;
    - não abre `testOverwrite` (o chamador já passa um nome livre; se o nome existir, falha com o detalhe `"A mod with this name already exists"`);
    - escolhe "Proceed" no `nexusQuery` (`:674`);
    - não mostra "Installation failed" (`:853`);
    - quando nenhum instalador serve, devolve `RESULT_FAILED` com o detalhe `"Needs a manual installation"`.

- [ ] **Step 1: Write the failing tests** (suite `InstallOrder`): `fileKey(12, 34) == "12_34"`; `fileKey("a.7z")` começa com `archive_` e é estável; `appendInstallOrder({"1_1","2_2"}, "1_1") == {"2_2","1_1"}`; `appendInstallOrder({}, "3_3") == {"3_3"}`.
- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement** as funções puras, a API de uibase, o modo lote e `installedFilesOrder`. Rode `./build.sh --only uibase` antes de `modorganizer`, `installer_quick` e `plugin_python`.
- [ ] **Step 4: Run** `./build.sh --only uibase --only modorganizer --only installer_quick --only plugin_python --tests`. Esperado: tudo compila, `InstallOrder.*` passa e os testes Python (`tests/python-import`) continuam passando.
- [ ] **Step 5: Commit** dois commits: `deps/uibase: installer options for batch installs` e `deps/modorganizer: modlists: batch install mode, installed files order` (este com o `installer_quick`, que pode ir num terceiro commit `deps/installer_quick: honour batch installs`).

### Task 5: FOMOD: gravar e reaplicar as escolhas

**Files:**
- Modify: `deps/installer_fomod/src/fomodinstallerdialog.h/.cpp`
- Modify: `deps/installer_fomod/src/installerfomod.h/.cpp` (`install` `:213`, `onInstallationEnd` `:90`)
- Modify: `tests/fixtures/fomod-sample/fomod/ModuleConfig.xml` (ou um segundo fixture `fomod-sample-conditional/`, com o `FOMOD_SAMPLE_DIR` atual intacto e `FOMOD_SAMPLE2_DIR` novo em `build.sh:359` e no `tests/CMakeLists.txt` do plugin): uma segunda página visível só quando a opção A estiver marcada (`<visible><flagDependency flag="a" value="on"/></visible>`).
- Test: `deps/installer_fomod/tests/test_fomod_choices.cpp`

**Interfaces:**
- Consumes: `installerOptions(name())` e `setInstallerDetail` (Task 4). As chaves usadas são `choices` (`QString` com o JSON compacto), `fileKey` e `batch`.
- Produces:
  - Formato das escolhas (o mesmo das Collections do Vortex): `[{"name": "<passo>", "groups": [{"name": "<grupo>", "choices": [{"name": "<opção>", "idx": <índice no grupo>}]}]}]`. Ele traz só as páginas visíveis, na ordem em que foram mostradas.
  - `QJsonArray FomodInstallerDialog::selectedChoices() const`
  - `void FomodInstallerDialog::applyChoices(const QJsonArray&)`. Para cada página visível, em ordem, procura a página registrada com o mesmo nome. Uma página visível sem registro é aceita com o padrão do XML. Marca as opções por nome (`idx` só desempata nomes repetidos) e avança, como `nextPage()`. Lança `std::runtime_error` com `"FOMOD step 'X' not found"`, `"FOMOD group 'G' not found in step 'X'"` ou `"FOMOD option 'O' not found in group 'G'"`. O mesmo vale para uma página registrada que não ficou visível (`"FOMOD step 'X' is not shown with these choices"`).
  - No `InstallerFomod::install`:
    - com `choices`, cria o diálogo, chama `initData`, `applyChoices` e `updateTree(tree)`, sem `exec()`. Se der erro, `setInstallerDetail(e.what())` e `RESULT_FAILED`;
    - com `batch` e sem `choices`, chama `setInstallerDetail("FOMOD without recorded choices")` e devolve `RESULT_FAILED` sem abrir janela;
    - nos outros casos, o fluxo de hoje e, se aceito, guarda `m_LastChoices = dialog.selectedChoices()`.
  - `onInstallationEnd(SUCCESS, newMod)` grava `newMod->setPluginSetting(name(), "choices_" + fileKey, QString(QJsonDocument(m_LastChoices).toJson(Compact)))` quando há escolhas.
  - Leitura pelo core: `modlists::fomodChoicesKey(fileKey) == "choices_" + fileKey`, em `installorder.h`. O plugin repete a string, porque não liga com `organizer_modlists`.

- [ ] **Step 1: Write the failing tests** (suite `FomodChoices`, no mesmo padrão do `test_fomod_parse.cpp`: plataforma offscreen e `extractSample`):
  - `RecordDefault`: sem cliques, `selectedChoices()` traz a página 1 com a opção padrão do XML.
  - `ReplaySelectsOption`: `applyChoices` com a opção B dá `selectedChoices()` igual ao que entrou, e `updateTree` deixa só `textures/option_b.dds`.
  - `HiddenPageNotRequired`: com a opção B, a página 2 fica escondida e o registro não a traz. Reaplicar um registro com a página 2 e a opção B lança `"is not shown"`.
  - `MissingOptionFails`: a opção `"C"` lança a mensagem com `"option 'C'"`.
- [ ] **Step 2: Run** `./build.sh --only installer_fomod --tests`. Esperado: compile failure.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Esperado: `FomodChoices.*` e os testes de parse existentes passam.
- [ ] **Step 5: Commit** `deps/installer_fomod: record and replay installer choices` (o fixture vai junto, em `tests/fixtures`).

### Task 6: Export: do perfil para o plano

**Files:**
- Create: `deps/modorganizer/src/modlists/exportbuilder.h/.cpp` (puro, em `_organizer_modlists_sources`)
- Create: `deps/modorganizer/src/modlists/exportcollector.h/.cpp` (lê `ModInfo`, `Profile`, `ExecutablesList`, `PluginList` e a pasta `downloads`; fica no alvo `organizer`)
- Test: `deps/modorganizer/tests/test_exportbuilder.cpp`

**Interfaces:**
- Consumes: Tasks 1 a 4 (`fomodChoicesKey`, `installedFilesOrder`, `PathMarkers`).
- Produces:
  - `enum class NonNexusChoice { Manual, Bundled, Exclude };`
  - `struct ExportFile { int modId; int fileId; QString key; QString md5; qint64 size = -1; std::optional<QJsonArray> fomodChoices; bool fomodWithoutChoices = false; };`
  - `struct ExportMod { QString name; QString version; bool isSeparator = false; bool enabled = true; std::vector<ExportFile> files; bool hasInstallOrder = true; NonNexusChoice nonNexus = NonNexusChoice::Manual; QString manualUrl; QString manualFileName; QString manualMd5; QString absolutePath; };`. `files` já vem na ordem gravada; quando não há ordem, na ordem de `installedFiles` e com `hasInstallOrder=false`.
  - `struct ExportOptions { bool includeInactive = false; std::vector<QString> overwriteFiles; QString listName, author, version, readme; };`. `overwriteFiles` são os relativos marcados, e o padrão é nenhum.
  - `enum class ExportWarning::Kind { NoFomodChoices, NoInstallOrder, NoMd5, Excluded, ManualWithoutUrl };` e `struct ExportWarning { Kind kind; QString mod; };`
  - `struct ExportResult { PackageInput package; std::vector<ExportWarning> warnings; int nexusCount, manualCount, bundledCount, excludedCount; };`
  - `ExportResult buildExport(const std::vector<ExportMod>& modsLowToHigh, const std::vector<PluginEntry>&, const std::map<QString, QByteArray>& profileInis, const std::vector<ExecutableEntry>&, const ProfileConfig&, const PathMarkers&, const QString& overwriteDir, const ExportOptions&, const Header& base)`
  - Regras:
    - inativos só entram com `includeInactive`, e aí como `optional=true, enabled=false`;
    - os separadores sempre entram;
    - um mod com arquivos da Nexus vira uma entrada com uma fonte `nexus` por arquivo;
    - sem Nexus, `nonNexus` decide entre `manual`, `bundled` (com os arquivos de `absolutePath`, menos `meta.ini`) ou ficar de fora;
    - `overwriteFiles` vira o mod `bundled` `"[<listName>] Overwrite"`, no topo;
    - INIs e executáveis passam por `toMarkers`;
    - os nomes `ModOrganizer.ini`, `*.ess`, `*.skse`, `*.log` e qualquer coisa debaixo de `saves/` nunca entram, mesmo que venham na entrada.
  - `ExportCollector`: `ExportCollector(OrganizerCore&, const Profile&)`, `std::vector<ExportMod> mods() const`, `PathMarkers markers() const` (o prefixo do Proton vem da Fase 3, o mesmo valor que `ProtonLauncher` usa), `std::map<QString, QByteArray> profileInis() const` (só os INIs do jogo na pasta do perfil, quando `localSettingsEnabled()`), `std::vector<ExecutableEntry> executables() const` e `std::vector<PluginEntry> plugins() const` (da `PluginList`, na ordem). O MD5 vem da `.meta` do download, se existir. Senão, é calculado do arquivo em `downloads`, ou fica vazio (aviso `NoMd5`). `fomodWithoutChoices` é verdadeiro quando não há escolhas gravadas e o arquivo em `downloads` tem `fomod/ModuleConfig.xml` (listado com o `archive`, sem extrair).

- [ ] **Step 1: Write the failing tests** (suite `ExportBuilder`):
  - `OnlyActiveByDefault`: com um ativo e um inativo, só o ativo entra. Com `includeInactive`, o inativo entra como `optional` e desativado.
  - `MultiFileOneEntry`: um mod com `files` 2_20 e 1_10 nessa ordem dá uma entrada com duas fontes, nessa ordem. Com `hasInstallOrder=false`, sai o aviso `NoInstallOrder`.
  - `NonNexusChoices`: `Manual` sem url dá o aviso `ManualWithoutUrl`, `Bundled` lista os arquivos de um diretório temporário (sem `meta.ini`) e `Exclude` dá `excludedCount==1`.
  - `OverwriteUncheckedByDefault`: com `overwriteFiles` vazio, não sai nenhum mod Overwrite. Com `{"SKSE/x.ini"}`, sai a última entrada `"[L] Overwrite"`.
  - `Sanitized`: um INI com `/home/u/Games/Skyrim` e um executável em `/home/u/Games/Skyrim/skse64_loader.exe` saem com `%GAME%`. Os INIs com `ModOrganizer.ini` e `saves/x.ess` na entrada não aparecem em `package.profileFiles`.
  - `FomodWarnings`: um arquivo com `fomodWithoutChoices` dá `NoFomodChoices`, e um com `fomodChoices` leva as escolhas para a fonte.
- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement** o builder e o collector. O collector compila no alvo `organizer`, sem teste unitário, e é coberto pelo checklist manual.
- [ ] **Step 4: Run.** Esperado: `ExportBuilder.*` passa e o `organizer` compila.
- [ ] **Step 5: Commit** `deps/modorganizer: modlists: export builder and collector`.

### Task 7: Import: classificação, nomes, ordem, downloads reaproveitados, casamento do `nxm://` e importação pendente

**Files:**
- Create: `deps/modorganizer/src/modlists/importplan.h/.cpp` (puro, em `_organizer_modlists_sources`)
- Create: `deps/modorganizer/src/modlists/guidedmatch.h/.cpp` (puro)
- Create: `deps/modorganizer/src/modlists/pendingimport.h/.cpp` (puro, JSON)
- Test: `deps/modorganizer/tests/test_importplan.cpp`, `test_guidedmatch.cpp`, `test_pendingimport.cpp`

**Interfaces:**
- Produces (`importplan.h`):
  - `enum class Destination { NewInstance, NewProfile, ExistingProfile };`
  - `struct InstalledMod { QString name; int modId; std::vector<int> fileIds; QString version; };`
  - `enum class RowStatus { New, AlreadyInstalled, DifferentVersion, Manual, Bundled };`. Para `NewInstance`, `DifferentVersion` não se aplica e vira `New` (6b, §4).
  - `RowStatus classify(const ModEntry&, const std::vector<InstalledMod>&, Destination)`. É `AlreadyInstalled` quando o `modId` é o mesmo e todos os `fileId`s da entrada estão instalados. É `DifferentVersion` quando o `modId` é o mesmo e os arquivos diferem. A primeira fonte decide entre `Manual` e `Bundled`.
  - `enum class VersionChoice { UseList, KeepMine };`
  - `QString installName(const ModEntry&, const QString& listName, const std::set<QString, CaseInsensitive>& existingNames)`. Devolve o nome da entrada se ele estiver livre. Senão, `"<nome> [<lista>]"`, e depois `" (2)"`, `" (3)"`… até achar um livre.
  - `struct ModOrderResult { std::vector<QString> order; std::vector<QString> disabled; };`
  - `ModOrderResult mergeModOrder(const std::vector<QString>& userLowToHigh, const std::vector<QString>& planInstalledLowToHigh, const std::vector<QString>& replacedUserMods, Destination, bool moveReused)`. Em `NewInstance`/`NewProfile`, a ordem é só a do plano e os outros mods ficam desativados no perfil. Em `ExistingProfile`, a ordem do usuário se mantém e os mods do plano que ainda não estão nela entram por cima, na ordem do plano. Com `moveReused`, os reaproveitados também vão para a posição do plano. `replacedUserMods` vão para `disabled`.
  - `struct PluginOrderResult { std::vector<PluginEntry> order; std::vector<QString> notInstalled; bool userOrderChanged = false; };`
  - `PluginOrderResult mergePluginOrder(const std::vector<PluginEntry>& user, const std::vector<PluginEntry>& plan, const std::set<QString, CaseInsensitive>& available, Destination)`. Em `NewInstance`/`NewProfile`, entram os plugins do plano que estão em `available`, na ordem do plano; os outros vão para `notInstalled`. Em `ExistingProfile`, a ordem do usuário fica igual, e os plugins do plano que o usuário ainda não tem entram depois, na ordem do plano (`userOrderChanged=false`); o resumo sempre oferece LOOT nesse caso.
  - `struct DownloadCandidate { QString path; int modId = 0; int fileId = 0; qint64 size = -1; };`
  - `std::optional<QString> findReusableDownload(const Source&, const std::vector<DownloadCandidate>&, const std::function<QString(const QString& path)>& md5Of)`. Primeiro tenta `modId`/`fileId` iguais e confere o MD5 se o plano tiver um. Depois tenta pelo MD5, calculado só para candidatos com o mesmo `size`, quando o plano tem `size`.
  - `QUrl nexusFilePageUrl(const QString& nexusDomain, int modId, int fileId)` = `https://www.nexusmods.com/<domain>/mods/<modId>?tab=files&file_id=<fileId>`. No Step 3, confira no navegador se essa URL abre o arquivo com o botão "Mod Manager Download". Se a Nexus usar outro parâmetro, ajuste o teste e registre isso no commit.
- Produces (`guidedmatch.h`):
  - `struct GuidedItem { int entryIndex; int sourceIndex; int modId; int fileId; enum State { Pending, Queued, Skipped } state = Pending; };`
  - `enum class MatchResult { Current, OtherItem, AlreadyQueued, SkippedItem, NotInList };`
  - `class GuidedQueue { public: explicit GuidedQueue(std::vector<GuidedItem>); std::optional<std::size_t> current() const; MatchResult match(int modId, int fileId); void skip(); void unskip(std::size_t); std::size_t done() const; std::size_t total() const; const std::vector<GuidedItem>& items() const; };`. Em `match`:
    - `Current` e `OtherItem` marcam o item como `Queued` e avançam `current()` para o próximo `Pending`;
    - `AlreadyQueued` e `SkippedItem` não mudam nada; o item pulado só volta com `unskip`;
    - `NotInList` também não muda nada (o download segue o fluxo normal).
- Produces (`pendingimport.h`):
  - `struct PendingImport { QString packagePath; Destination destination; QString profileName; std::vector<int> selectedEntries; std::map<int, VersionChoice> versionChoices; QString sourceDownloadsDir; std::vector<QString> installedEntries; };`
  - `QJsonObject toJson(const PendingImport&)` / `PendingImport pendingFromJson(const QJsonObject&)`
  - `std::vector<int> remainingEntries(const PendingImport&, const Plan&)` = os `selectedEntries` cujo nome (o `installName` gravado) não está em `installedEntries`.
  - O arquivo é `<pasta da instância>/modlist-import.json`, com o `.mo2list` copiado para `<pasta da instância>/modlist-import.mo2list`.

- [ ] **Step 1: Write the failing tests:**
  - `ImportPlan.Classify`: os quatro estados (mesmos arquivos, outro `fileId`, sem `modId` instalado, `manual`/`bundled`). Em `NewInstance`, `DifferentVersion` vira `New`.
  - `ImportPlan.InstallName`: `"SkyUI"` livre fica `"SkyUI"`; ocupado vira `"SkyUI [Minha Lista]"`; e ocupado nos dois, `"SkyUI [Minha Lista] (2)"`.
  - `ImportPlan.MergeModOrderExisting`: com o usuário `{A,B}` e o plano `{X,B,Y}`, sem `moveReused` dá `{A,B,X,Y}` e com `moveReused` dá `{A,X,B,Y}`. Com `replaced={A}`, `disabled=={A}`.
  - `ImportPlan.MergeModOrderNew`: em `NewProfile`, a ordem é a do plano.
  - `ImportPlan.MergePluginsNew`: com o plano `{a.esm, b.esp, c.esp}` e `available={a.esm, c.esp}`, a ordem é `{a.esm, c.esp}` e `notInstalled=={b.esp}`.
  - `ImportPlan.MergePluginsExisting`: com o usuário `{u.esp, a.esm}` e o plano `{a.esm, b.esp}`, dá `{u.esp, a.esm, b.esp}` e `userOrderChanged==false`.
  - `ImportPlan.ReuseDownload`: o mesmo `modId`/`fileId` é reaproveitado; um candidato só com o MD5 igual também; com o `size` diferente, o `md5Of` não é chamado (conte as chamadas).
  - `ImportPlan.FilePageUrl`: `nexusFilePageUrl("skyrimspecialedition", 266, 1000)`.
  - `GuidedMatch.InOrder`: com três itens, `match` do `current()` duas vezes dá `done()==2`.
  - `GuidedMatch.OtherItem`: `match` do terceiro primeiro dá `OtherItem`, e `current()` continua no primeiro.
  - `GuidedMatch.DuplicateAndSkipped`: `match` repetido dá `AlreadyQueued`. Depois de `skip()`, `match` do item pulado dá `SkippedItem` e o item fica `Skipped`; com `unskip`, volta a `Pending`.
  - `GuidedMatch.NotInList`: dá `NotInList` e nada muda.
  - `PendingImport.RoundTrip` e `PendingImport.ResumeSkipsInstalled`: com `selected={0,1,2}` e o mod da entrada 1 já em `installedEntries`, dá `{0,2}`.
- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Esperado: os três conjuntos de testes passam.
- [ ] **Step 5: Commit** `deps/modorganizer: modlists: import planning, guided download queue, pending import`.

### Task 8: `ModlistInstaller`: downloads, instalação em lote, prioridade, plugins, INIs, executáveis e resumo

**Files:**
- Modify: `deps/modorganizer/src/downloadmanager.h/.cpp`. Um sinal boost `onNXMDownloadQueued(std::function<void(const QString& game, int modId, int fileId, int downloadId)>)`, disparado em `addNXMDownload` (`:738`) quando a função devolve um id diferente de 0 (incluindo o caso "já na fila", `:797`). E um `setBatchMessages(bool)` que suprime os `QMessageBox` de "Already Queued"/"already started" enquanto a importação roda; nesses casos só o id volta.
- Create: `deps/modorganizer/src/modlists/modlistinstaller.h/.cpp` (alvo `organizer`)
- Create: `deps/modorganizer/src/modlists/importsummary.h/.cpp` (puro, em `_organizer_modlists_sources`), com `test_importsummary.cpp`

**Interfaces:**
- Consumes: Tasks 3 a 7. `DownloadManager::startDownloadNexusFile`, `onDownloadComplete/Failed`, `downloadPath`; `OrganizerCore::installArchive(path, priority, false, nullptr, name)`; `InstallationManager::setBatch/setInstallerOptions/lastInstallerDetail`; `Profile::setModEnabled/setModPriority`; `PluginList`; `ExecutablesList`; `LootDialog` (4b) para "Sort with LOOT".
- Produces:
  - `importsummary.h`:
    - `enum class Outcome { Installed, Reused, Failed, Skipped };`
    - `struct SummaryRow { QString mod; Outcome outcome; QString detail; bool canInstallManually = false; };`
    - `struct Summary { std::vector<SummaryRow> rows; std::vector<QString> pluginsNotInstalled; std::vector<std::pair<QString, QStringList>> missingMasters; std::vector<QString> inisNotApplied; std::vector<QString> executablesWithoutTarget; bool offerLoot = false; bool userPluginOrderChanged = false; };`
    - `QString summaryText(const Summary&)`: texto simples, em seções, para copiar.
    - `std::vector<int> retryable(const Summary&, const std::map<QString, int>& entryOfMod)`.
    - `canInstallManually` é verdadeiro quando o detalhe é `"FOMOD without recorded choices"`, `"Needs a manual installation"` ou começa com `"FOMOD "`.
  - `class ModlistInstaller : public QObject` com `ModlistInstaller(OrganizerCore&, Mo2ListPackage, PendingImport, QWidget* parent)`, `void start()`, `void retry(std::vector<int> entries)` e os sinais `progress(int done, int total, QString what)`, `needsGuidedDownloads(std::vector<GuidedItem>)` e `finished(Summary)`. A sequência:
    1. Para cada entrada selecionada e cada fonte `nexus`, tenta `findReusableDownload` na pasta de downloads da instância e em `sourceDownloadsDir` (copia o arquivo e a `.meta` quando é outra instância).
    2. O que falta: com Premium (`APIUserAccount::type() == Premium`), `startDownloadNexusFile` para cada item, respeitando a fila. Sem Premium, emite `needsGuidedDownloads`, e a Task 9 alimenta `GuidedQueue` pelo `onNXMDownloadQueued`. Fontes `manual` entram na mesma janela guiada.
    3. Confere o MD5/tamanho de cada download concluído quando o plano tem esses dados. Se não bater, a linha fica `Failed` com `"Checksum mismatch"`.
    4. Pula as entradas `AlreadyInstalled` (viram `Reused`) e as `DifferentVersion` com `KeepMine` (também `Reused`, o mod do usuário fica). Com `UseList`, instala com `installName` e põe o mod antigo em `replacedUserMods`. Instala em ordem de `phase` e depois de prioridade, com `setBatch(true)`. Para cada fonte, as `choices` vão para o `Fomod Installer`. As fontes seguintes do mesmo mod instalam por cima com `installArchive(..., reinstallation=true, currentMod)`. Os `bundled` são extraídos com `extractBundled` num mod criado com `installName` (`OrganizerCore::createMod`). Cada mod instalado vai para `installedEntries`, e o `modlist-import.json` é regravado (retomada). Uma falha não para o lote.
    5. Grava no `meta.ini` de cada mod `modlist=<nome da lista>` (via `setPluginSetting("Modlists", "modlist", …)`).
    6. Ordem com `mergeModOrder`, aplicada com `setModPriority`/`setModEnabled`. Em `ExistingProfile` com reaproveitados fora do lugar, uma pergunta **depois** do lote ("Move N reused mods to the list's order?").
    7. Plugins com `mergePluginOrder`, aplicados na `PluginList` depois de `refreshLists()`. Os masters faltando vêm da `PluginList` (masters de cada plugin que não estão na lista). Com `pluginsUseLoot`, roda o LOOT e marca `offerLoot`.
    8. INIs: em destino novo, grava `fromMarkers(...)` na pasta do perfil. Em `ExistingProfile`, pergunta, listando os arquivos que diferem, e os não aplicados vão para `inisNotApplied`. As opções do perfil (`localInis`/`localSaves`) só mudam em destino novo.
    9. Executáveis: `fromMarkers` e `ExecutablesList::setExecutable` (merge por título). Os que têm alvo inexistente vão para `executablesWithoutTarget`.
    10. No fim: `setBatch(false)`, `clearInstallerOptions()`, apaga `modlist-import.json`/`.mo2list` quando não sobra nada pendente e emite `finished(summary)`.

- [ ] **Step 1: Write the failing tests** (suite `ImportSummary`): `summaryText` com uma linha de cada `Outcome`, mais masters, INIs e executáveis, contém cada seção e cada mod. `retryable` devolve só os `Failed`. `canInstallManually` é verdadeiro para os três detalhes e falso para `"Checksum mismatch"`.
- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement** `importsummary`, o hook do `DownloadManager` e o `ModlistInstaller`.
- [ ] **Step 4: Run.** Esperado: `ImportSummary.*` passa, `organizer` compila e os testes existentes continuam passando.
- [ ] **Step 5: Commit** `deps/modorganizer: modlists: batch installer`.

### Task 9: Janelas do import: destino, revisão, downloads guiados e resumo

**Files:**
- Create em `deps/modorganizer/src/modlists/` (alvo `organizer`): `destinationdialog.h/.cpp/.ui`, `reviewdialog.h/.cpp/.ui`, `guideddownloadsdialog.h/.cpp/.ui`, `summarydialog.h/.cpp/.ui`, `importflow.h/.cpp`
- Modify: `deps/modorganizer/src/createinstancedialog.h/.cpp`. Extraia de `finish()` (`:299-389`) a criação para `bool createInstanceFiles(const CreateInstanceDialog::CreationInfo&, const std::function<void(QString)>& log)`, e `finish()` passa a chamá-la. O comportamento do diálogo não muda.
- Modify: `deps/modorganizer/src/mainwindow.ui/.cpp`. No menu Mods, as ações `actionImportModlist` ("Import modlist…") e `actionExportModlist` ("Export modlist…", Task 10). Na abertura da janela principal, se existir `modlist-import.json` na instância, pergunta "Continue importing <lista>?" (Continue / Later / Discard).
- Modify: `deps/modorganizer/src/moapplication.cpp:397`. Uma mensagem externa que é um caminho de arquivo terminado em `.mo2list` abre o `ImportFlow` com esse arquivo.

**Interfaces:**
- Consumes: Tasks 3, 7 e 8.
- Produces:
  - `class ImportFlow : public QObject` com `ImportFlow(OrganizerCore&, QWidget*)`, `void startFromFile(const QString& path)` e `void resumePending()`. O fluxo de `startFromFile`:
    1. `Mo2ListPackage::open`. Num `PlanError`, mostra a mensagem; para `NewerSchema`, o texto das Global Constraints.
    2. `DestinationDialog`, sem nada pré-marcado: "New instance" (nome sugerido = `header.name`), "New profile in this instance" (nome sugerido = `header.name`) ou "Existing profile" (combo). Um jogo diferente do da instância desabilita as opções de perfil e mostra o porquê (§6.1).
    3. `ReviewDialog`.
    4. Destino novo: para `NewInstance`, `createInstanceFiles` com o mesmo jogo, a mesma pasta e a mesma variante da instância atual, as pastas padrão debaixo da pasta da instância e `localInis=localSaves=true`. Depois grava o `PendingImport` + a cópia do pacote na pasta nova, chama `setCurrentInstance` e `ExitModOrganizer(Exit::Restart)`. Para `NewProfile`: `Profile` novo com `enableLocalSettings(true)` e `enableLocalSaves(true)`, e segue na mesma sessão.
    5. `ModlistInstaller`, a `GuidedDownloadsDialog` quando pedida e a `SummaryDialog`.
  - `ReviewDialog(const Plan&, const std::vector<InstalledMod>&, Destination, bool premium, int reusableCount, QWidget*)`, com `std::vector<int> selectedEntries() const`, `std::map<int, VersionChoice> versionChoices() const` e `bool confirmedExecutableContent() const`. O layout segue a 6b, §4 (cabeçalho, filtro, marcar tudo, desmarcar opcionais, ocultar já instalados), com as linhas e o rodapé da §5.6 do spec: "Premium account ✔" ou "No Premium: you will click N downloads on Nexus", com N já sem os reaproveitados. Os opcionais vêm desmarcados. Quando `findExecutableContent` não está vazio, a seção "Executable content" lista tudo, e o botão "Download and install" fica desabilitado até marcar "I reviewed the executable content".
  - `GuidedDownloadsDialog(GuidedQueue&, const Plan&, QWidget*)`: abre `nexusFilePageUrl` do item atual com `shell::Open` e mostra "N of M", "Open again", "Skip", a lista do que falta e, para `manual`, "Open page" + "Choose downloaded file…" (com conferência do MD5).
  - `SummaryDialog(const Summary&, QWidget*)`: seções do resumo, "Copy", "Retry failed", "Install manually" por linha (abre o fluxo interativo de hoje, `installArchive` fora do lote, para o arquivo daquela linha) e "Sort with LOOT" quando `offerLoot`.

- [ ] **Step 1: Implement** as janelas e o `ImportFlow`. São só interface sobre as Tasks 7 e 8, sem lógica nova (a lógica testável já está nelas).
- [ ] **Step 2: Build** `./build.sh --only modorganizer --tests`. Esperado: compila e os testes passam.
- [ ] **Step 3: Smoke** (`pgrep -af ModOrganizer` vazio): `tests/smoke` passa. Abra o MO2 de teste, use "Import modlist…" com o `.mo2list` de fixture da Task 10 e confira se o diálogo de destino abre sem opção marcada. Feche sem importar.
- [ ] **Step 4: Commit** `deps/modorganizer: modlists: import dialogs and flow`.

### Task 10: Janela do export e um `.mo2list` de fixture

**Files:**
- Create: `deps/modorganizer/src/modlists/exportdialog.h/.cpp/.ui` (alvo `organizer`)
- Create: `tests/fixtures/modlists/small.mo2list`, gerado por um teste (`Mo2ListZip.WriteFixture`, desativado por padrão: `DISABLED_`) e versionado, com 3 mods `nexus` reais pequenos de Skyrim SE, um separador, um `bundled` e um INI. Os ids ficam escolhidos no Step 1 e anotados no `tests/fixtures/modlists/README.md`.

**Interfaces:**
- Consumes: Task 6 (`ExportCollector`, `buildExport`) e Task 3 (`writeMo2List`, `findLeaks`).
- Produces: `ExportDialog(OrganizerCore&, const Profile&, QWidget*)`.
  - Campos: nome (padrão = nome do perfil), autor, versão, README e "Include inactive mods as optional".
  - Uma tabela dos mods sem Nexus, com a escolha "Manual source / Made by me — include files / Leave out" (padrão Manual) e o link. "Include files" mostra o aviso "Only include files you made yourself. Redistributing other authors' files breaks their permissions."
  - A árvore do `overwrite`, toda desmarcada.
  - A lista de avisos (`ExportWarning`).
  - Antes de salvar, `findLeaks` em cada INI e em cada executável. Se houver vazamentos, mostra arquivo, linha e trecho, e pede confirmação.
  - O resumo "N mods (Nexus / manual / included / left out), package size" e Save.

- [ ] **Step 1: Implement** a janela, a ação `actionExportModlist` e o teste gerador do fixture. Gere o fixture uma vez (`--gtest_also_run_disabled_tests --gtest_filter=Mo2ListZip.DISABLED_WriteFixture`).
- [ ] **Step 2: Run** `./build.sh --only modorganizer --tests`. Esperado: compila e passa. `Mo2ListPackage::open` no fixture versionado funciona (acrescente `Mo2ListZip.FixtureOpens`, não desativado).
- [ ] **Step 3: Commit** `deps/modorganizer: modlists: export dialog; small.mo2list fixture`.

### Task 11: Abrir `.mo2list` pelo gerenciador de arquivos (se couber no mecanismo do `nxm://`)

**Files:**
- Modify: `deps/modorganizer/src/linux/nxmhandler_linux.h/.cpp`. Um segundo desktop file `org.modorganizer.ModOrganizer2.mo2list.desktop` (`MimeType=application/x-mo2list;`, `Exec=<launcher> %f`). Mais o pacote de MIME `~/.local/share/mime/packages/org.modorganizer.mo2list.xml` (glob `*.mo2list`), `update-mime-database ~/.local/share/mime` e `xdg-mime default`, pelo mesmo caminho do `vfsbridge` no host.
- Test: `deps/modorganizer/tests/test_nxmhandler.cpp` (casos novos)

**Interfaces:**
- Produces: `QString mo2listDesktopFileContent(const Environment&)` e `QString mo2listMimeXml()`. A Task 9 já recebe o caminho via `externalMessage`.

- [ ] **Step 1: Write the failing tests** (`NxmHandler.Mo2listDesktopFile`): o conteúdo tem `MimeType=application/x-mo2list;` e o `Exec` termina com `%f`, para AppImage, distrobox e executável. `mo2listMimeXml()` contém `<glob pattern="*.mo2list"/>`.
- [ ] **Step 2: Run, expect a compile failure.**
- [ ] **Step 3: Implement**, registrando junto com o handler do `nxm://` (mesmo momento e mesma regra de `decide`). Se o `update-mime-database` não existir no host, registre só um aviso no log e siga, porque o menu continua funcionando (§6.1).
- [ ] **Step 4: Run.** Esperado: `NxmHandler.*` passa.
- [ ] **Step 5: Commit** `deps/modorganizer: Linux: open .mo2list files with Mod Organizer`.

### Task 12: Documentação, checklist manual e estado do projeto

**Files:**
- Create: `docs/testing/modlists-manual-checklist.md`
- Modify: `docs/user/en/` e `docs/user/pt-BR/` (página nova `modlists.md` e link no índice, no padrão de `games.md`)
- Modify: `STATE.md`, `ROADMAP.md`, `README.md`/`README.pt-BR.md` (uma linha na lista de recursos)

**Interfaces:** nenhuma.

- [ ] **Step 1: Write** o checklist, com os itens do §9 do spec. Sem marcar nada, porque quem testa é o usuário:
  1. exportar um perfil real de Skyrim;
  2. sem nenhum vazamento de home ou de nome de usuário no `.mo2list` (`unzip -p … | grep -i <usuário>` vazio);
  3. importar numa instância nova com conta **sem Premium** (cliques guiados);
  4. comparar prioridade, plugins, INIs e escolhas de FOMOD com o original;
  5. jogar;
  6. importar num perfil existente com mods já presentes (reaproveitar e versão diferente);
  7. pacote com `.dll` em `bundled` (a confirmação aparece);
  8. `.mo2list` com `schemaVersion` 2 (recusado);
  9. fechar o MO2 no meio da importação numa instância nova e reabrir (oferece continuar, sem duplicar);
  10. abrir `.mo2list` pelo Dolphin (se a Task 11 entrou).
- [ ] **Step 2: Write** a documentação do usuário, incluindo o limite do §6.4 ("keep your edits in a separate mod or in overwrite") e a regra do `bundled`.
- [ ] **Step 3: Update** o `STATE.md` (parte 1 em teste manual), o `ROADMAP.md` (parte 2 de Collections, Wabbajack e IA depois) e o status do spec para "aprovado; parte 1 implementada".
- [ ] **Step 4: Run** `./build.sh --tests` completo (`pgrep -af ModOrganizer` vazio). Esperado: todos os alvos e testes passam.
- [ ] **Step 5: Commit** `docs: modlists part 1 — user docs, manual checklist, state`.
