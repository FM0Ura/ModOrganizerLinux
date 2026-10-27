# Página da Nexus no painel lateral — Design

- **Data:** 2026-10-27
- **Status:** aguardando aprovação do usuário
- **Branch (decisão do Claude):** `worktree-nexus-page-panel`, a partir de `main` (44ca78a).
- **Depende de:** Fase 4a (Nexus, login/API key no keyring).

## 1. Objetivo e porquê

Hoje, para ver a imagem e ler a descrição de um mod da Nexus, o usuário precisa abrir a janela de informações do mod (aba Nexus) ou ir ao navegador. Ele quer ver isso **na tela principal, sem sair da lista de mods**: a imagem principal do mod e a documentação (descrição) da página da Nexus, na lateral esquerda.

### 1.1 Fluxo pedido pelo usuário

1. Na lista de mods, ao lado do checkbox de ativo/inativo, há um **ícone de arquivo**.
2. Clicar no ícone abre a página do mod **na lateral esquerda** da janela principal.
3. A lateral esquerda passa a ter **abas**: **Filtros** (o painel de filtros/categorias que já existe) e **Nexus** (a página do mod). A aba Nexus **substitui a visualização** dos Filtros, mas **não altera o que foi filtrado**: os filtros ativos continuam aplicados à lista.

### 1.2 Escopo

| Item | Nesta entrega |
|---|---|
| Imagem principal do mod (`picture_url`) | Dentro |
| Descrição completa da página (BBCode → HTML) | Dentro |
| Outras informações da Nexus (endorsements, downloads, arquivos, changelog, comentários…) | **Fora** (decisão do usuário: "apenas isso"). O cabeçalho mínimo de §3.3 (nome/autor/versão, que o MO2 já tem localmente) é decisão do Claude. |
| Mods sem ID da Nexus, separadores, Overwrite, mods estrangeiros/backup | Sem ícone (decisão do Claude) |
| Buscar dados para a lista inteira em lote | Fora (limite de API, §5.3) |

### 1.3 Anti-metas

- Trocar de aba (Filtros ↔ Nexus) **mudar ou limpar** os filtros aplicados à lista.
- Travar a interface esperando a rede: o painel sempre mostra primeiro o que já está salvo.
- Gastar a cota de API da Nexus em segundo plano sem o usuário pedir (ex.: varrer todos os mods ao iniciar).
- Links da descrição navegarem **dentro** do painel (a página da Nexus inteira carregada ali, com login, anúncios etc.).
- Gravar imagens dentro da pasta do mod (iriam para o jogo pelo VFS).

## 2. O que já existe (pontos de partida no código)

- `ModInfoRegular` já guarda no `meta.ini`: `nexusDescription`, `lastNexusQuery`, `nexusLastModified` (`modinforegular.cpp:99,175-180,272-278`).
- `ModInfoRegular::nxmDescriptionAvailable` (`modinforegular.cpp:354`) recebe a resposta de `GET /v1/games/{game}/mods/{id}` (via `NexusBridge::requestDescription`); a resposta contém `description`, `picture_url`, `updated_timestamp`, mas hoje `picture_url` é descartado.
- `ModInfoRegular::needsDescriptionUpdate()` (`modinforegular.cpp:436`) já implementa a regra "consultar de novo se a última consulta tem mais de 1 dia".
- A aba Nexus da janela de informações (`modinfodialognexus.cpp:223-316`) já converte BBCode para HTML (`BBCode::convertToHTML`), monta o HTML com CSS e mostra num `QWebEngineView` com `NexusTabWebpage`, que intercepta cliques em links (`acceptNavigationRequest`).
- Lateral esquerda: `categoriesSplitter` contém `categoriesGroup` (painel de filtros) e o `splitter` (lista de mods + abas da direita) em `mainwindow.ui`. O botão `displayCategoriesBtn` mostra/esconde `categoriesGroup`; a visibilidade é salva em `MainWindow` (`mainwindow.cpp:2183,2252,3781-3789`).
- Lista de mods: `ModList::data` (`modlist.cpp`) devolve o checkbox em `Qt::CheckStateRole` na coluna 0 (`COL_NAME`); `ModListStyledItemDelegate` (`modlistview.cpp:42`) desenha os itens.

## 3. Interface

### 3.1 Ícone na lista de mods

- Na coluna de nome, **logo depois do checkbox**, um ícone de "arquivo/documento" (16 px, do tema, com fallback em recurso Qt).
- Aparece só para mods com **ID da Nexus > 0** e que sejam `ModInfoRegular` (não separador, Overwrite, foreign, backup).
- Clicar no ícone **não** altera a seleção nem o estado do checkbox; só abre o painel. Tooltip: "Ver página na Nexus".
- O ícone do mod que está aberto no painel fica destacado (ex.: versão "ativa" do ícone), para o usuário saber qual está vendo (decisão do Claude).
- Implementação (a critério do construtor): `Qt::DecorationRole` na coluna 0 + tratamento de clique em `editorEvent` do delegate, ou desenho próprio no delegate. O clique deve acertar o retângulo do ícone, não a linha inteira.

### 3.2 Lateral esquerda com abas

- `categoriesGroup` passa a ficar **dentro de um `QTabWidget`** (nome sugerido `leftPanelTabs`) com duas abas: **Filtros** e **Nexus**.
- O `QTabWidget` toma o lugar de `categoriesGroup` no `categoriesSplitter`. O conteúdo dos Filtros não muda; só muda o contêiner. Os filtros continuam valendo com qualquer aba visível.
- `displayCategoriesBtn` passa a mostrar/esconder o **painel lateral inteiro** (as duas abas). Ao mostrar pelo botão, abre na última aba usada.
- Clicar no ícone de um mod: mostra o painel (se escondido), seleciona a aba **Nexus** e carrega o mod.
- Clicar no ícone do **mesmo** mod que já está aberto, com a aba Nexus visível: esconde o painel (decisão do Claude; o usuário pode fechar também pelo botão de sempre).
- Salvar/restaurar: visibilidade do painel (já existe), aba atual e largura (pelo splitter, já existe). O mod aberto **não** é restaurado ao reiniciar (decisão do Claude).
- Sem mod escolhido, a aba Nexus mostra: "Clique no ícone de documento ao lado de um mod para ver a página dele na Nexus."

### 3.3 Conteúdo da aba Nexus

De cima para baixo:

1. **Imagem principal**, ajustada à largura do painel e mantendo a proporção. Enquanto baixa, aparece um espaço reservado; se não houver imagem (ou se falhar), a área some.
2. **Cabeçalho**: nome do mod, autor, versão instalada (dados locais do MO2).
3. **Descrição** formatada (mesmo HTML/CSS da aba Nexus da janela de informações), com rolagem.
4. **Rodapé/barra**: botão **Atualizar**, botão **Abrir na Nexus** (abre a URL do mod no navegador) e o texto de estado: "Atualizado na Nexus em {nexusLastModified} · consultado {lastNexusQuery}" (datas no formato local; "hoje"/"ontem" quando couber).

- Links da descrição abrem no **navegador do sistema** (mesmo comportamento de `NexusTabWebpage::linkClicked`).
- Imagens embutidas na descrição carregam direto da web, como já acontece na janela de informações (sem cache próprio).
- Sugestão: tudo (imagem + cabeçalho + descrição) num único `QWebEngineView`, com a imagem como `<img src="file://…cache…">`; assim a rolagem é uma só. A escolha é do construtor.
- Reaproveitar a montagem de HTML da aba Nexus: extrair de `modinfodialognexus.cpp` uma função comum (ex.: `nexusDescriptionHtml(mod)`) usada pelos dois lugares, em vez de duplicar.

### 3.4 Estados e erros

| Situação | O que o painel mostra |
|---|---|
| Há descrição salva | Mostra na hora; se a consulta tem mais de 1 dia, busca em segundo plano (§5.1) |
| Nada salvo, usuário logado | "Carregando da Nexus…" e então o conteúdo |
| Nada salvo, não logado | "Faça login na Nexus (Configurações → Nexus) para ver a página deste mod." + botão Abrir na Nexus |
| Erro de rede/API | Mantém o que tiver salvo e mostra no rodapé "Não foi possível atualizar: {erro}" |
| Mod apagado/renomeado enquanto aberto | Limpa o painel (volta à mensagem inicial) |
| Resposta chega depois de o usuário trocar de mod | Atualiza os dados salvos daquele mod, mas **não** troca o que está na tela |

### 3.5 Erros e login: como o painel conversa com a Nexus (decisões do Claude, após a revisão)

O caminho de requisições que existe hoje não serve ao painel do jeito que está:

- `ModInfoRegular::nxmRequestFailed` (`modinforegular.cpp:411-424`) abre um `MessageDialog` **modal** quando há janela ativa e emite `modDetailsUpdated(false)`, que não traz o texto do erro.
- `NexusInterface::nextRequest` (`nexusinterface.cpp:841-859`), sem login, com token expirado ou com a validação ainda pendente, **deixa a requisição na fila ou a descarta** com só uma linha de log, sem sinal de falha. Num dos casos emite `needLogin()`, que pode abrir o pedido de login.

Decisões:

1. **Sem popup para requisições do painel.** As requisições que o painel inicia (automáticas e pelo botão Atualizar) levam uma marca em `userData` (ex.: `QVariantMap{{"source","nexusPanel"}}`). `nxmRequestFailed` não abre o diálogo para elas e emite um sinal novo com o texto do erro (ex.: `ModInfo::nexusRequestFailed(QString message)`), que o painel mostra no rodapé. A janela de informações e os outros caminhos continuam com o popup como hoje.
2. **O painel verifica o login antes de pedir.** Se o `NexusInterface` não tiver uma API key validada (ou a validação ainda estiver em andamento), o painel **não envia** a requisição e mostra o estado "Faça login…" (§3.4). Com isso nunca dispara `needLogin()` nem abre o pedido de login sozinho.
3. **Reage ao login.** Quando a validação da conta termina com sucesso (sinal que o `NexusInterface`/`APIUserAccount` já emite), se a aba Nexus estiver mostrando "Faça login…" para um mod, o painel tenta de novo.
4. **Tempo limite.** Se nenhuma resposta (sucesso ou falha) chegar em 30 s, o painel sai de "Carregando…", reabilita o Atualizar e mostra no rodapé "A Nexus não respondeu". Se a resposta chegar depois, os dados são salvos normalmente e, se ainda for o mesmo mod, a tela é atualizada.

## 4. Onde os dados ficam

### 4.1 `meta.ini` do mod

Campos existentes continuam iguais. Campo novo:

- `nexusPictureUrl`: o `picture_url` da última resposta (string; vazio se não houver).

`ModInfo` ganha `getNexusPictureUrl()`/`setNexusPictureUrl()` (implementação vazia nos tipos que não são `ModInfoRegular`, como já acontece com `getNexusDescription`). `nxmDescriptionAvailable` passa a gravar `picture_url`. O mesmo vale para o outro ponto que trata essa resposta (`mainwindow.cpp:3442`).

### 4.2 Cache de imagens

- Pasta: `<pasta da instância>/webcache/nexus-images/` (decisão do Claude; a pasta da instância já guarda outros dados de cache do MO2. O construtor confirma o local exato que o MO2 usa hoje e o segue).
- Nome: `<gameName>_<modId>.<ext>` (extensão da URL; `jpg` por padrão).
- Ao lado, guardar de que URL veio a imagem (ex.: `<gameName>_<modId>.url` com a URL, ou chave no `meta.ini` como `nexusPictureCachedUrl`; o construtor escolhe). A imagem só é baixada de novo quando `nexusPictureUrl` difere da URL de origem.
- Download com `QNetworkAccessManager` (sem API key; é CDN pública), assíncrono. Grava em arquivo temporário e renomeia ao concluir.
- Sem limpeza automática nesta entrega (arquivos pequenos, um por mod consultado). Se o mod for removido, a imagem fica órfã; aceitável (decisão do Claude).

## 5. Atualização

### 5.1 Regra

- Ao abrir um mod no painel: mostra o que está salvo e, se `needsDescriptionUpdate()` for verdadeiro (última consulta há mais de 1 dia), chama `updateNXMInfo()` em segundo plano. Quando `modDetailsUpdated` chega, o painel recarrega **se ainda for o mesmo mod**.
- Botão **Atualizar**: força `requestDescription` na hora, ignorando a regra de 1 dia.
- Se o autor mudou a descrição, a nova resposta sobrescreve `nexusDescription`, `nexusLastModified` (`updated_timestamp`) e `nexusPictureUrl`. Se a URL da imagem mudou, baixa a imagem nova (§4.2).

### 5.2 Fora da regra

- A verificação de atualizações de versão do MO2 (botão "Check for updates") continua como está. Esta entrega **não** passa a buscar descrições por causa dela.

### 5.3 Limite da API

A Nexus limita as chamadas (aprox. 2.500/dia, e depois 100/hora). Por isso só há chamada quando o usuário abre um mod no painel (no máximo uma vez por dia por mod, mais os cliques em Atualizar). Cliques repetidos em Atualizar enquanto uma consulta está em andamento são ignorados (botão desabilitado até a resposta).

## 6. Riscos técnicos

- **QtWebEngine na janela principal:** hoje o `QWebEngineView` só existe dentro da janela de informações. Um na janela principal fica vivo durante toda a sessão. Já houve um crash de QtWebEngine no reinício interno do MO2 (Change Game/instância; ver `main.cpp:84`). O construtor deve **criar o `QWebEngineView` só na primeira vez** que a aba Nexus abre e testar o reinício interno (trocar de instância) com o painel aberto e fechado.
- **Cliques no ícone x drag-and-drop/seleção da lista:** a lista de mods tem arrastar para reordenar, seleção múltipla e menu de contexto. O clique no ícone não pode iniciar arrasto nem mudar a seleção.
- **Indentação de separadores/coluna de nome:** a área de clique do checkbox já é tratada no delegate (`modlistview.cpp:911`); o ícone deve respeitar a mesma lógica.

## 7. Testes

- Unitários (onde der): leitura/gravação de `nexusPictureUrl` no `meta.ini`; decisão de baixar a imagem de novo (URL igual/diferente/arquivo ausente); montagem do HTML com e sem imagem.
- Manuais (checklist em `docs/testing/`):
  1. Ícone aparece só em mods com ID da Nexus; não aparece em separadores/Overwrite.
  2. Clique no ícone abre o painel na aba Nexus com imagem, cabeçalho e descrição; não muda a seleção nem o checkbox.
  3. Filtros aplicados continuam valendo ao trocar para a aba Nexus e voltar.
  4. Clicar no mesmo ícone de novo esconde o painel; o botão de mostrar/esconder painel continua funcionando.
  5. Link na descrição abre no navegador.
  6. Sem login: mensagem de login. Sem rede: mantém o conteúdo salvo e mostra erro no rodapé.
  7. Atualizar: busca de novo; com descrição alterada na Nexus (ou `lastNexusQuery` editado no `meta.ini` para 2 dias atrás), o conteúdo é atualizado.
  8. Reiniciar o MO2 e trocar de instância com o painel aberto: sem crash.

## 8. Fica a critério do construtor

- Como desenhar e detectar o clique no ícone (DecorationRole + `editorEvent` ou pintura própria).
- Um `QWebEngineView` para tudo ou `QLabel` para a imagem + view para a descrição.
- Local exato do cache dentro da instância e onde guardar a URL de origem da imagem.
- Ícone exato (tema/recurso).
- Textos de interface em inglês no código, com tradução pt-BR seguindo o fluxo de traduções do projeto.
