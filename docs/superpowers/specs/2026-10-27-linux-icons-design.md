# Ícones de arquivos e executáveis no Linux — Design

- **Data:** 2026-10-27
- **Status:** aguardando aprovação do usuário
- **Branch (decisão do Claude):** `fix-linux-icons`, a partir de `main` (2485a9f). Separado da aba Nexus (`docs/superpowers/specs/2026-10-27-nexus-page-panel-design.md`), que vem primeiro (sugestão do Claude; o usuário pode inverter).

## 1. Objetivo e porquê

No Linux, **todos os ícones aparecem iguais** (relato do usuário) na aba Data e na lista de executáveis. No Windows, cada tipo de arquivo tem seu ícone e cada `.exe` mostra o próprio ícone. O usuário quer que isso funcione no Linux, inclusive com **ícones próprios para os tipos de arquivo de jogo** (decisão do usuário).

## 2. Causas (verificadas no código)

1. **Aba Data (`IconFetcher`, `deps/modorganizer/src/iconfetcher.cpp`).** Para arquivos que não são `.exe/.lnk/.ico`, o ícone é pedido por extensão: `getPixmapIcon(QFileInfo(ext))` com `ext` = `".dds"`, `".txt"`… No Linux, `QFileIconProvider` trata `.dds` como um arquivo oculto sem extensão, e tudo cai no ícone genérico.
2. **Executáveis (`MOBase::iconForExecutable`, `deps/uibase/src/linux/utility_linux.cpp`).** No Linux, a função sempre devolve `QIcon::fromTheme("application-x-executable")`. Ela é usada na barra/lista de executáveis (`mainwindow.cpp:781,1881`), no ícone do jogo (`game_bethesda`: `gamegamebryo.cpp:73`, Enderal, Enderal SE) e nos plugins Python (`getIconForExecutable`). Na aba Data, os `.exe` passam por `QFileIconProvider` com o caminho real, que também devolve o genérico.

## 3. O que muda

### 3.1 Ícone por tipo de arquivo (aba Data)

- No Linux, o ícone por extensão passa a ser resolvido assim, em ordem:
  1. **Tabela própria de tipos de jogo** (§3.2). Se a extensão estiver nela, usa esse ícone.
  2. **Banco de tipos MIME** (`QMimeDatabase::mimeTypeForFile("x" + ext, QMimeDatabase::MatchExtension)`): `QIcon::fromTheme(mime.iconName())`, depois `QIcon::fromTheme(mime.genericIconName())`.
  3. Ícone genérico de arquivo (como hoje).
- Pastas continuam com o ícone genérico de pasta.
- O cache por extensão e a thread do `IconFetcher` ficam como estão. Só muda a função que produz o ícone de uma extensão.
- Windows: sem mudança (código atrás de `#ifndef _WIN32` ou no arquivo Linux).

### 3.2 Ícones próprios para tipos de jogo (decisão do usuário; mapeamento do Claude)

| Extensões | Significado | Ícone |
|---|---|---|
| `.esp` `.esm` `.esl` | plugin do jogo | ícone de "plugin/extensão" |
| `.bsa` `.ba2` | pacote de arquivos do jogo | ícone de arquivo compactado/pacote |
| `.nif` `.kf` `.hkx` | modelo 3D / animação | ícone de modelo 3D |
| `.pex` `.psc` | script compilado / fonte de script | ícone de script |
| `.dds` | textura | ícone de imagem (o MIME `image/x-dds` já cobre; entra na tabela por garantia) |
| `.ini` | configuração | ícone de configuração/texto |
| `.dll` `.asi` | biblioteca nativa (SKSE, ASI loaders) | ícone de biblioteca |
| `.pak` `.utoc` `.ucas` | pacote Unreal (Stellar Blade etc.) | ícone de pacote |

- Cada linha usa um **nome de ícone do tema** (Breeze/freedesktop) com **reserva em recurso Qt próprio** do MO2, para funcionar com qualquer tema. Os nomes exatos e os arquivos de reserva ficam a critério do construtor. Requisito: os ícones devem ser **distinguíveis entre si** no tema escuro Breeze a 16 px.
- A tabela fica em um só lugar no código, fácil de estender.
- **AppImage:** não é garantido que `QIcon::fromTheme` encontre os ícones Breeze dentro do AppImage. O construtor verifica no AppImage local. Se o tema não for encontrado, os ícones de reserva (§3.2) e um conjunto mínimo de reserva para os tipos MIME comuns (imagem, texto, compactado) precisam aparecer; o teste manual 1 é feito **no AppImage**, não só no build local.

### 3.3 Ícone de dentro do `.exe`

- No Linux, `iconForExecutable(path)` passa a **ler o ícone embutido no arquivo PE** (seção de recursos: `RT_GROUP_ICON` + `RT_ICON`), sem Wine nem ferramentas externas:
  1. Ler cabeçalhos PE (32 e 64 bits), achar a seção de recursos.
  2. Pegar o primeiro grupo de ícones (o mesmo que o Explorer do Windows mostra).
  3. Montar um `.ico` em memória com as imagens do grupo e carregar com `QImage`/`QIcon` (o plugin ICO do Qt; imagens PNG dentro do ICO também). Montar um `QIcon` com todos os tamanhos.
- Se não houver ícone, o arquivo for inválido ou a leitura falhar: volta ao ícone genérico de executável (comportamento de hoje). **Nunca** deve travar ou derrubar o MO2 com arquivo malformado (limites de tamanho e de deslocamento checados em toda leitura).
- Cache em memória por caminho + data de modificação, para não reler o mesmo `.exe` a cada repintura.
- A aba Data usa a mesma função para `.exe`. **`.dll` usa o ícone da tabela §3.2, não o embutido** (decisão do Claude após a revisão: busca barata por extensão, sem ler cada DLL do disco, e o ícone de biblioteca fica distinguível). Arquivos `.ico` mostram a própria imagem (carregados direto com `QIcon(path)`).
- O código fica em `deps/uibase` (onde `iconForExecutable` vive), para servir ao MO2, aos plugins C++ e aos Python.
- Já existe um leitor da seção de recursos PE no Linux: `deps/uibase/src/linux/peversion_linux.cpp` (usado por `getFileVersion`). O leitor de ícones deve **reaproveitar/estender** esse código, não duplicá-lo. Testes vão junto de `deps/uibase/tests/test_linux_*.cpp`.

### 3.4 Desempenho e threads

- Ler ícones de `.exe` acontece na thread do `IconFetcher` para a aba Data. A lista de executáveis chama `iconForExecutable` na thread da interface; com o cache, cada `.exe` é lido uma vez por sessão. Se a leitura de um `.exe` grande ficar lenta, o construtor pode limitar a leitura à seção de recursos (sem ler o arquivo inteiro).
- Fora da thread da interface, gerar `QImage` (seguro) e converter para `QPixmap`/`QIcon` na thread da interface, se o código atual já não fizer de forma segura. O construtor verifica como o `IconFetcher` faz hoje e mantém o mesmo padrão.
- Arquivos dentro do VFS montado: ler o `.exe` pelo caminho real (`item.realPath()`), não pelo caminho dentro da montagem (ver o travamento de 24/10 com o helper do VFS parado).

## 4. Fora do escopo

- Ícones de atalhos `.lnk` (não existem no Linux de forma útil; ficam genéricos).
- Miniaturas (prévia de imagens/texturas na aba Data).
- Mudar ícones da lista de mods ou de plugins.

## 5. Testes

- Unitários: leitor PE com (a) um `.exe` pequeno com ícone (fixture gerada no teste ou binário pequeno de domínio público), (b) `.exe` sem ícone, (c) arquivo truncado/malformado → genérico sem crash; resolução por extensão (tabela própria, MIME, genérico).
- Manuais:
  1. Aba Data com Skyrim SE: `.esp/.esm`, `.bsa`, `.nif`, `.dds`, `.pex`, `.ini`, `.txt`, `.dll` com ícones diferentes entre si.
  2. Lista de executáveis: SKSE, LOOT, BodySlide, o jogo — cada um com o próprio ícone.
  3. Ícone do jogo (Skyrim SE) na barra/instância.
  4. Stellar Blade: `.pak/.utoc/.ucas` com ícone de pacote.
  5. Abrir a aba Data com muitos arquivos: sem travamento perceptível.

## 6. Fica a critério do construtor

- Nomes de ícones do tema e arquivos de reserva.
- Implementação do leitor PE (própria, pequena; sem dependência nova de sistema).
- Onde a tabela de tipos fica (MO2 ou uibase).
