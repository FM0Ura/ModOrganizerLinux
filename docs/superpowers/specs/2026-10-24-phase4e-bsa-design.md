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
| Wrapper | Nosso (`LinuxArchive`, §3.2), com a interface do wrapper do Kaedras mas não a implementação (§1.3). |
| Issues no upstream | Nenhuma: o usuário não quer abrir issues (2026-10-24). |

### 1.3 Defeitos do port do Kaedras e do `libbsarchpp` 1.0.7

Lidos no código em 2026-10-24 (fork `master` e `libbsarchpp` 1.0.7):

1. `libbsarchppWrapper::save_to_disk` nunca adiciona arquivos ao `Bsa`: no laço sobre `files_` chama os próprios `add_file_from_memory`/`add_file_from_disk` do wrapper (que fazem `files_.emplace_back` durante a iteração: comportamento indefinido) em vez de `bsa.addFile(...)`.
2. `libbsarchppWrapper::set_dds_base_path` usa `m_archive`, que só existe depois de `load_from_disk`: na criação de FO4dds/SFdds é um ponteiro nulo.
3. `libbsarchpp`: o construtor de criação de 4 argumentos (`Bsa(path, type, fileList, ddsBasePath = nullopt)`) escreve `Bsa(...)` no corpo, criando um temporário e deixando `*this` sem inicializar. O construtor de 7 argumentos está correto.

O `LinuxArchive` (§3.2) evita os três: guarda a pasta base do DDS no próprio wrapper, usa só o construtor de 7 argumentos e chama `Bsa::addFile` para cada arquivo.

## 2. Build

| Peça | Origem | Como entra |
|---|---|---|
| `bsapacker` | `ModOrganizer2/modorganizer-bsapacker` `master` (`de1d783`, licença MIT) | `deps/bsapacker` (subtree) + alvo `bsapacker` no `build.sh` |
| `libbsarchpp` 1.0.7 | `Kaedras/libbsarchpp` (GPL-3.0) | `build/libbsarchpp/build-libbsarchpp.sh` (padrão do `build/libloot/`): baixa a tag fixa, compila estática (`BUILD_SHARED_LIBS=OFF`, `-fPIC`) e instala o pacote CMake (`find_package(libbsarchpp CONFIG)`) em `build-out` |
| `boost-ext/di` v1.3.2 | cabeçalho único `boost/di.hpp` (BSL-1.0), não está no Fedora | baixado pelo mesmo script, na tag fixa, para um diretório de include de `build-out` (como o toml++ da 4b) |
| `gsl-lite-devel`, `tbb-devel` | Fedora 44 | `Containerfile` (CI) e box `mo2-run` (instalação com o OK do usuário); `lz4-devel`, `zlib-ng-compat-devel`, `openssl-devel` e `gtest-devel` já estão lá |

Saída: `install/bin/plugins/libbsa_packer.so`, com o `libbsarchpp` estático dentro (sem `.so` extra a localizar; dependências dinâmicas novas: `libtbb`, `liblz4`, `libz`, do sistema do box). O `Qt6::Concurrent` vem do sistema (o `install` dele para `bin/dlls` fica só no Windows). O plugin aparece no menu de ferramentas como **BSA Packer**. O CI do Windows continua compilando só `deps/modorganizer`.

## 3. Patches Linux (`deps/bsapacker`)

### 3.1 Ponto único de troca: `src/bsapacker/ArchiveBackend.h` (novo)

```cpp
#ifdef _WIN32
#include <libbsarch/bs_archive_auto.hpp>    // libbsarch, como hoje
namespace BsaPacker {
  using Archive     = libbsarch::bs_archive_auto;
  using ArchiveType = bsa_archive_type_e;
}
#else
#include <bsapacker/LinuxArchive.h>
namespace BsaPacker {
  using Archive     = LinuxArchive;
  using ArchiveType = libbsarchpp::ArchiveType;
  // constantes com os nomes do libbsarch: baTES3, baTES4, baFO3, baSSE,
  // baFO4, baFO4dds, baSF, baSFdds, baNone
}
#endif
```

O resto do `bsapacker` (builders, `ArchiveBuilderFactory`, `ArchiveNameService`, `DummyPluginLogic`, serviços de plugin vazio, `ArchiveAutoService`, `BsaPackerWorker`, interfaces em `src/bsapacker/` e os mocks dos testes) troca `bsa_archive_type_e`/`bs_archive_auto` por `ArchiveType`/`Archive` e inclui `ArchiveBackend.h`. No Windows os aliases resolvem para os tipos originais: o código compilado é o mesmo. Os nomes exatos das constantes e dos cabeçalhos do `libbsarch` seguem o que o subtree usa (o plano confere).

### 3.2 `LinuxArchive` (só no Linux: `src/LinuxArchive.cpp`, `src/bsapacker/LinuxArchive.h`)

Mesma API que os builders usam no `bs_archive_auto`: `add_file_from_disk(disk_blob)` (com `disk_blob(raiz, caminho absoluto)`), `set_compressed(bool)`, `set_share_data(bool)`, `set_dds_callback`/`set_dds_base_path` (§3.3) e `save_to_disk(caminho)`. Guarda a lista de arquivos (raiz, caminho no arquivo, caminho absoluto) e as opções. `save_to_disk`:

1. monta o `fileList` com os caminhos dentro do arquivo;
2. cria `libbsarchpp::Bsa(caminho, tipo, fileList, ddsBasePath, compressed, shareData, /*multithreaded*/ false)` (o construtor de 7 argumentos);
3. `bsa.addFile(raiz, caminho absoluto)` para cada arquivo;
4. `bsa.save()`.

Exceções (`std::runtime_error`) sobem para o `ArchiveAutoService` (§4). Sem várias threads: o resultado é determinístico (o README do `libbsarchpp` avisa que o modo com threads muda a ordem dos dados).

### 3.3 DDS (FO4dds e SFdds)

No Windows, o `TextureArchiveBuilder` passa um callback que lê largura, altura e mipmaps pelo DirectXTex; o callback e o `#include "DirectXTex.h"` ficam em `#ifdef _WIN32`. No Linux o builder passa a pasta raiz do mod (`set_dds_base_path`) e o `libbsarchpp` lê o cabeçalho DDS no `addFileDDS`.

### 3.4 `CMakeLists.txt`

No Windows, como hoje (`mo2-libbsarch`, `mo2-dds-header`, `DirectXTex`, os `BSArchive*.cpp` e `qlibbsarch/`, e o `install` do `Qt6::Concurrent`). No Linux: `find_package(libbsarchpp CONFIG REQUIRED)`, sem os arquivos do `libbsarch` e do DirectXTex, com `LinuxArchive.cpp`, e o include do `boost/di.hpp` baixado (§2). Os testes (`tests/`) entram no `ctest` do Linux.

### 3.5 O que já funciona sem patch

- `ArchiveNameService` usa `QDir::toNativeSeparators` (no Linux, `/`).
- Esconder os arquivos soltos renomeia as pastas para `.mohidden`, como no Windows; o MO2 do Linux já ignora `.mohidden`.
- O plugin vazio (`.esp`/`.esl`) e o `.override` do Fallout 3/NV são bytes escritos pelo `FileWriterService`: sem diferença de plataforma.

## 4. Erros

- **Falha ao gravar** (disco cheio, permissão, arquivo que sumiu, exceção do `libbsarchpp`, como o limite de 4 GiB): o `ArchiveAutoService` já captura e não lista o arquivo como criado. No Linux (`#ifndef _WIN32`), o `catch` registra no log `failed to write archive <caminho>: <mensagem>` (o upstream descarta a mensagem) e apaga o arquivo parcial, se existir.
- **Mod sem nada a empacotar** (tudo na lista negra ou pasta vazia): comportamento do upstream (nenhum arquivo criado).
- **Mod com arquivos que o jogo não aceita no formato** (ex.: nomes fora do código de página): a exceção do `libbsarchpp` segue o primeiro item.

## 5. Testes

1. **`libbsarchpp`** (no `build-libbsarchpp.sh`, com `LIBBSARCHPP_TESTING=ON` e as suítes por jogo desligadas: `TESTS_*_ENABLE=OFF`): `Misc`, `Hash`, `GSL`, `CreateArchive` e `Fast.*` (arquivos pequenos do próprio repositório para TES3, TES4, FO3, SSE, FO4, FO4dds e SF).
2. **Testes do upstream do `bsapacker`** (`tests/`, gtest/gmock, `bsa_packer_tests`) compilados e rodados no Linux.
3. **Teste nosso de ida e volta** (`deps/bsapacker/tests/LinuxArchiveFacts.cpp`, só no Linux): o `LinuxArchive` empacota uma árvore temporária (pastas aninhadas, maiúsculas e minúsculas, arquivos comprimíveis e um já comprimido, um `.dds` real gerado no teste) em cada tipo (TES3, TES4, FO3, SSE, FO4, FO4dds, SF, SFdds), com e sem compressão onde o formato permite; reabre o arquivo (`libbsarchpp::Bsa(path)`) e confere a lista de arquivos e o conteúdo byte a byte. Para TES4, FO3, SSE e FO4, reabre também pelo **`bsatk`** (o leitor do MO2 na pré-visualização e no BSA Extractor) e confere a lista e o conteúdo extraído. Mais: erro de gravação (pasta de destino sem permissão) lança e não deixa arquivo parcial.
4. **Smoke:** o `libbsa_packer.so` carrega e o "BSA Packer" está entre os plugins carregados.

## 6. Limitações

- BSA acima de 4 GiB: o `libbsarchpp` recusa (o xEdit cria); o `bsapacker` já divide em mais arquivos acima de 2 GiB (4 GiB para texturas), então só um arquivo único maior que isso falha, com a mensagem no log.
- Os arquivos LZ4 (SSE, Skyrim VR, SFdds) diferem byte a byte dos do xEdit (o `libbsarchpp` usa um LZ4 atual); o jogo os lê.
- Fallout 4, Starfield, Oblivion, Fallout 3/NV, Skyrim LE e Morrowind: suportados, não validados num jogo real.

## 7. Riscos

| Risco | Mitigação |
|---|---|
| Outro defeito no `libbsarchpp` 1.0.7 além do §1.3 | Testes de ida e volta por formato, com o `bsatk` como leitor independente; teste manual no Skyrim SE. |
| Os aliases do §3.1 mudam algo no Windows | Só aliases para os tipos originais; o CI do Windows não compila o `bsapacker`, então a revisão confere o diff por plataforma. |
| `boost-ext/di` com GCC 16 / C++23 | O fork do Kaedras compila com GCC no Linux; se falhar, o patch fica no `ArchiveBackend.h`/`BsaPacker.cpp`. |
| `QtConcurrent` e diálogos fora da thread principal | Mesmo fluxo do upstream (o `save_to_disk` roda em `QtConcurrent::run`, os diálogos ficam na thread da interface). |

## 8. Critérios de pronto

- Testes do `libbsarchpp` (§5.1), do `bsapacker` e de ida e volta passam; smoke com o "BSA Packer" carregado; `./build.sh --tests` e CIs verdes.
- No Bazzite, com o Skyrim SE: o BSA Packer cria o `.bsa` e o `.esp` vazio na pasta do mod e esconde os arquivos soltos; a aba Archives e a pré-visualização mostram o conteúdo; o BSA Extractor extrai o arquivo criado; o jogo abre pelo MO2 com o conteúdo vindo do BSA.
- README (criação de BSA/BA2), `deps/README.md` (subtree e patches), checklist manual `docs/testing/phase4e-manual-checklist.md`, ROADMAP e STATE atualizados.
