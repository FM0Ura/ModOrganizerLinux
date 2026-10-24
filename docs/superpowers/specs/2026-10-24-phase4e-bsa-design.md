# Fase 4e — Criação de BSA/BA2 no Linux — Design

- **Data:** 2026-10-24
- **Status:** em revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md) (§3.3 e a tabela de dependências: "`libbsarch` (Delphi) — não existe para Linux"; este documento a substitui onde divergirem)
- **Branch:** `fase-4e-bsa` (a partir de `fase-4d2-basic-games`, no worktree `../ModOrganizerLinux-4e`; o PR mira `fase-4d2-basic-games` até as anteriores entrarem no `main`)

## 1. Objetivo

O MO2 do Linux cria BSA/BA2 como o do Windows: a ferramenta **BSA Packer** (plugin `bsapacker` do upstream) empacota os arquivos soltos de um mod num `.bsa`/`.ba2` na pasta do mod e, se configurado, cria o plugin vazio que faz o jogo carregar o arquivo e esconde os arquivos soltos. No Windows o `bsapacker` usa o `libbsarch` (Delphi, só Windows); no Linux ele usa o `libbsarchpp`, reimplementação em C++ do BSArchive do xEdit.

### 1.1 Escopo (decisão do usuário, 2026-10-24)

| Item | Nesta fase |
|---|---|
| Formatos | Todos os que o `bsapacker` suporta: Morrowind (TES3), Oblivion (TES4), Fallout 3/New Vegas e Skyrim LE/Enderal (FO3), Skyrim SE/Enderal SE (SSE), Fallout 4 (FO4 + FO4dds) e Starfield (SF + SFdds) |
| Validação real | Skyrim SE (o jogo do usuário); os demais formatos só pelos testes automáticos (§5), como "suportados, não validados" |
| Windows | Sem mudança: continua no `libbsarch` |
| BSA acima de 4 GiB, empacotamento com várias threads | Fora (§6) |

### 1.2 Decisões desta fase

| Tema | Decisão |
|---|---|
| Abordagem (A, decisão do usuário) | `bsapacker` do upstream por `git subtree` com patches Linux nossos; o port do Kaedras (`Kaedras/modorganizer-bsapacker`, que troca o `libbsarch` pelo `libbsarchpp` em todas as plataformas) serve de referência, não de fonte. Descartadas: importar o fork do Kaedras (o Windows deixaria de seguir o upstream; atualizações dependeriam do fork); escrever um gravador próprio no `bsatk` (a escrita dele só cobre o formato antigo, sem BA2 e sem ordenar por hash). |
| Biblioteca no Linux | `libbsarchpp` **1.0.7** (`Kaedras/libbsarchpp`, GPL-3.0, commit `211a681`, a versão que o fork usa), compilada como biblioteca estática dentro do plugin. |
| Importação | `git subtree add --prefix deps/bsapacker https://github.com/ModOrganizer2/modorganizer-bsapacker master --squash` (`master` em `de1d783`); hash em `deps/README.md`; patches `deps/bsapacker: Linux: …` (só arquivos do subtree). Código Windows idêntico ou em `#ifdef _WIN32`. |
| Wrapper | Nosso: cabeçalhos de compatibilidade do `libbsarch` e um `bs_archive_auto` sobre o `libbsarchpp` (§3.1, §3.2); do Kaedras, só a ideia (§1.3). |
| Issues no upstream | Nenhuma: o usuário não quer abrir issues (2026-10-24). |

### 1.3 Defeitos do port do Kaedras e do `libbsarchpp` 1.0.7

Lidos no código em 2026-10-24 (fork `master` e `libbsarchpp` 1.0.7):

1. `libbsarchppWrapper::save_to_disk` nunca adiciona arquivos ao `Bsa`: no laço sobre `files_` chama os próprios `add_file_from_memory`/`add_file_from_disk` do wrapper (que fazem `files_.emplace_back` durante a iteração: comportamento indefinido) em vez de `bsa.addFile(...)`.
2. `libbsarchppWrapper::set_dds_base_path` usa `m_archive`, que só existe depois de `load_from_disk`: na criação de FO4dds/SFdds é um ponteiro nulo.
3. `libbsarchpp`: o construtor de criação de 4 argumentos (`Bsa(path, type, fileList, ddsBasePath = nullopt)`) escreve `Bsa(...)` no corpo, criando um temporário e deixando `*this` sem inicializar. O construtor de 7 argumentos está correto.

O `bs_archive_auto` do Linux (§3.2) evita os três: guarda a pasta base do DDS no próprio wrapper, usa só o construtor de 7 argumentos e chama `Bsa::addFile` para cada arquivo.

## 2. Build

| Peça | Origem | Como entra |
|---|---|---|
| `bsapacker` | `ModOrganizer2/modorganizer-bsapacker` `master` (`de1d783`, licença MIT) | `deps/bsapacker` (subtree) + alvo `bsapacker` no `build.sh` |
| `libbsarchpp` 1.0.7 | `Kaedras/libbsarchpp` (GPL-3.0) | `build/libbsarchpp/build-libbsarchpp.sh` (padrão do `build/libloot/`): baixa a tag fixa, compila estática (`BUILD_SHARED_LIBS=OFF`, `-fPIC`) e instala o pacote CMake (`find_package(libbsarchpp CONFIG)`) em `build-out` |
| `boost-ext/di` v1.3.2 | cabeçalho único `boost/di.hpp` (BSL-1.0), não está no Fedora | baixado pelo mesmo script, na tag fixa, para um diretório de include de `build-out` (como o toml++ da 4b) |
| `gsl-lite-devel`, `tbb-devel` | Fedora 44 | `Containerfile` (CI) e box `mo2-run` (instalação com o OK do usuário); `lz4-devel`, `zlib-ng-compat-devel`, `openssl-devel` e `gtest-devel` já estão lá |

Saída: `install/bin/plugins/libbsa_packer.so`, com o `libbsarchpp` estático dentro (sem `.so` extra a localizar; dependências dinâmicas novas: `libtbb`, `liblz4`, `libz`, do sistema do box). O `Qt6::Concurrent` vem do sistema (o `install` dele para `bin/dlls` fica só no Windows). O plugin aparece no menu de ferramentas como **BSA Packer**. O CI do Windows continua compilando só `deps/modorganizer`.

## 3. Patches Linux (`deps/bsapacker`)

### 3.1 Cabeçalhos de compatibilidade do `libbsarch` (só no Linux)

Os fontes do `bsapacker` usam o `libbsarch` por poucos nomes: os tipos C de `<libbsarch/libbsarch.h>` (`bsa_archive_type_e`/`bsa_archive_type_t` com `baNone`…`baSFdds`, `bsa_archive_t`, `bsa_dds_info_t`, `bsa_file_dds_info_proc_t` e os `bsa_result_*` que o `qlibbsarch/QLibbsarch.h` usa) e, de `<libbsarch/bs_archive_auto.hpp>`, `libbsarch::disk_blob` e `libbsarch::bs_archive_auto` (`add_file_from_disk`, `set_compressed`, `set_share_data`, `set_dds_callback`, `save_to_disk`). No Linux, `deps/bsapacker/linux/include/libbsarch/` tem esses dois cabeçalhos, com esses nomes e assinaturas compatíveis, e `deps/bsapacker/linux/bs_archive_auto.cpp` implementa o `bs_archive_auto` sobre o `libbsarchpp`. O CMake põe `linux/include` antes dos demais includes. Assim os fontes do upstream compilam no Linux **sem renomear nada**: o código do Windows não muda, e o patch nos fontes do upstream fica em dois pontos (§3.3 e §3.4). Revisão de 2026-10-24, na escrita do plano: substitui o `ArchiveBackend.h` com aliases da primeira versão desta spec, que pedia trocar os tipos em ~30 arquivos.

### 3.2 `bs_archive_auto` sobre o `libbsarchpp` (`linux/bs_archive_auto.cpp`)

Guarda a lista de arquivos (raiz, caminho absoluto, caminho dentro do arquivo calculado como o `Bsa::addFile` calcula: o caminho absoluto sem a raiz), o tipo, a compressão, o `shareData` e a pasta base do DDS (a raiz que o `TextureArchiveBuilder` passa ao `set_dds_callback`; o callback em si é ignorado). Os caminhos em `std::wstring` que os builders passam viram UTF-8 (`QString::fromStdWString(…).toStdString()`), não a conversão de `std::filesystem::path` pelo locale. `save_to_disk`:

1. sem arquivos, lança `std::runtime_error` (nenhum arquivo criado, como o upstream);
2. monta o `fileList` com os caminhos dentro do arquivo;
3. cria `libbsarchpp::Bsa(caminho, tipo, fileList, ddsBasePath, compressed, shareData, /*multithreaded*/ false)` (o construtor de 7 argumentos);
4. `bsa.addFile(raiz, caminho absoluto)` para cada arquivo;
5. `bsa.save()`;
6. em qualquer exceção: registra no log `failed to write archive <caminho>: <mensagem>`, apaga o arquivo parcial e relança (o `ArchiveAutoService` do upstream captura e não lista o arquivo como criado).

Sem várias threads: o resultado é determinístico (o README do `libbsarchpp` avisa que o modo com threads muda a ordem dos dados).

### 3.3 DDS (FO4dds e SFdds)

No Windows, o `TextureArchiveBuilder` passa um callback que lê largura, altura e mipmaps pelo DirectXTex. O `#include "DirectXTex.h"` e o corpo do `DDSCallback` ficam em `#ifdef _WIN32` (no Linux a função existe vazia e nunca é chamada); no Linux o `libbsarchpp` lê o cabeçalho DDS no `addFileDDS`.

### 3.4 `CMakeLists.txt`

`src/CMakeLists.txt`: no Windows, como hoje (`mo2-libbsarch`, `mo2-dds-header`, `DirectXTex` e o `install` do `Qt6::Concurrent`). No Linux: `find_package(libbsarchpp CONFIG REQUIRED)`; os `BSArchive*.cpp` (o wrapper Qt do `libbsarch` C, que só o Windows tem) saem da lista de fontes que o `mo2_configure_plugin` junta; entram `linux/bs_archive_auto.cpp` e `linux/include`; o include do `boost/di.hpp` baixado (§2). O `CMakeLists.txt` raiz acrescenta, só no Linux e com `BUILD_TESTING`, `linux/tests` (§5.3). Os testes do upstream (`tests/`) entram no `ctest` do Linux.

### 3.5 O que já funciona sem patch

- `ArchiveNameService` usa `QDir::toNativeSeparators` (no Linux, `/`).
- Esconder os arquivos soltos renomeia as pastas para `.mohidden`, como no Windows; o MO2 do Linux já ignora `.mohidden`.
- O plugin vazio (`.esp`/`.esl`) e o `.override` do Fallout 3/NV são bytes escritos pelo `FileWriterService`: sem diferença de plataforma.
- O `ArchiveAutoService` já captura a exceção do `save_to_disk` e devolve falha.

## 4. Erros

- **Falha ao gravar** (disco cheio, permissão, arquivo que sumiu, exceção do `libbsarchpp`, como o limite de 4 GiB): o `bs_archive_auto` do Linux registra no log `failed to write archive <caminho>: <mensagem>` (o `catch` do upstream descarta a mensagem), apaga o arquivo parcial e relança; o `ArchiveAutoService` não lista o arquivo como criado.
- **Mod sem nada a empacotar** (tudo na lista negra ou pasta vazia): comportamento do upstream (nenhum arquivo criado).
- **Mod com arquivos que o jogo não aceita no formato** (ex.: nomes fora do código de página): a exceção do `libbsarchpp` segue o primeiro item.

## 5. Testes

1. **`libbsarchpp`** (no `build-libbsarchpp.sh`, com `LIBBSARCHPP_TESTING=ON` e as suítes por jogo desligadas: `TESTS_*_ENABLE=OFF`): `Misc`, `Hash`, `GSL`, `CreateArchive` e `Fast.*` (arquivos pequenos do próprio repositório para TES3, TES4, FO3, SSE, FO4, FO4dds e SF).
2. **Testes do upstream do `bsapacker`** (`tests/`, gtest/gmock, `bsa_packer_tests`) compilados e rodados no Linux.
3. **Teste nosso de ida e volta** (`deps/bsapacker/linux/tests/`, só no Linux): o `bs_archive_auto` do Linux empacota uma árvore temporária (pastas aninhadas, maiúsculas e minúsculas, arquivos comprimíveis e um já comprimido, um `.dds` real gerado no teste) em cada tipo (TES3, TES4, FO3, SSE, FO4, FO4dds, SF, SFdds), com e sem compressão onde o formato permite; reabre o arquivo (`libbsarchpp::Bsa(path)`) e confere a lista de arquivos e o conteúdo byte a byte. Para TES4, FO3, SSE e FO4, reabre também pelo **`bsatk`** (o leitor do MO2 na pré-visualização e no BSA Extractor) e confere a lista e o conteúdo extraído. Mais: erro de gravação (pasta de destino sem permissão) lança e não deixa arquivo parcial.
4. **Smoke:** o `libbsa_packer.so` carrega e o "BSA Packer" está entre os plugins carregados.

## 6. Limitações

- BSA acima de 4 GiB: o `libbsarchpp` recusa (o xEdit cria); o `bsapacker` já divide em mais arquivos acima de 2 GiB (4 GiB para texturas), então só um arquivo único maior que isso falha, com a mensagem no log.
- Os arquivos LZ4 (SSE, Skyrim VR, SFdds) diferem byte a byte dos do xEdit (o `libbsarchpp` usa um LZ4 atual); o jogo os lê.
- Fallout 4, Starfield, Oblivion, Fallout 3/NV, Skyrim LE e Morrowind: suportados, não validados num jogo real.

## 7. Riscos

| Risco | Mitigação |
|---|---|
| Outro defeito no `libbsarchpp` 1.0.7 além do §1.3 | Testes de ida e volta por formato, com o `bsatk` como leitor independente; teste manual no Skyrim SE. |
| Os patches mudam algo no Windows | Os cabeçalhos de compatibilidade só entram no include do Linux; nos fontes do upstream, só `#ifdef _WIN32` em volta do código atual (§3.3) e o CMake em `if (WIN32)`/`else()`; a revisão confere o diff por plataforma (o CI do Windows não compila o `bsapacker`). |
| `boost-ext/di` com GCC 16 / C++23 | O fork do Kaedras compila com GCC no Linux; se falhar, o patch fica no `BsaPacker.cpp`. |
| `QtConcurrent` e diálogos fora da thread principal | Mesmo fluxo do upstream (o `save_to_disk` roda em `QtConcurrent::run`, os diálogos ficam na thread da interface). |

## 8. Critérios de pronto

- Testes do `libbsarchpp` (§5.1), do `bsapacker` e de ida e volta passam; smoke com o "BSA Packer" carregado; `./build.sh --tests` e CIs verdes.
- No Bazzite, com o Skyrim SE: o BSA Packer cria o `.bsa` e o `.esp` vazio na pasta do mod e esconde os arquivos soltos; a aba Archives e a pré-visualização mostram o conteúdo; o BSA Extractor extrai o arquivo criado; o jogo abre pelo MO2 com o conteúdo vindo do BSA.
- README (criação de BSA/BA2), `deps/README.md` (subtree e patches), checklist manual `docs/testing/phase4e-manual-checklist.md`, ROADMAP e STATE atualizados.
