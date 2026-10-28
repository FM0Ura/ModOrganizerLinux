# Modlists — núcleo comum, Nexus Collections e formato próprio — Design

- **Data:** 2026-10-28
- **Status:** em revisão
- **Substitui:** a spec da Fase 6b ([`2026-10-25-phase6b-nexus-collections-design.md`](2026-10-25-phase6b-nexus-collections-design.md)), que não tinha plano nem código. As decisões do usuário registradas nela continuam valendo, salvo onde este documento diz o contrário (§1.4).
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md)
- **Depende de:** Fase 4a (Nexus, `nxm://`, keyring), Fase 4b (LOOT), Fase 6a (Stellar Blade).
- **Branch (decisão do Claude):** um por parte (§10), a partir de `main`.

## 1. Objetivo e porquê

O usuário quer que o MO2 Linux **monte uma modlist inteira a partir de uma lista**, e que ele possa **compartilhar a própria montagem com outras pessoas** (decisão do usuário, 2026-10-28: o propósito da exportação é compartilhar, não backup). Três entradas para a mesma coisa:

1. **Nexus Collections** (o que a 6b desenhava), agora também para **Skyrim**.
2. **Formato próprio do nosso MO2** (decisão do usuário): exportar a montagem de um perfil e importar em outra máquina.
3. **Wabbajack** e o **assistente de IA** vêm depois, em specs próprias (§1.3). Este documento só garante que o núcleo os aceite sem retrabalho.

A ideia central (decisão do Claude, apresentada ao usuário em 2026-10-28): todas as entradas produzem o mesmo **plano de modlist** (§3), e um único instalador consome o plano — janela de revisão, fila de downloads, FOMOD sem assistente, prioridade, ordem de plugins e resumo de falhas são escritos uma vez só.

### 1.1 Fluxo de quem importa

1. Cola a URL de uma Collection, abre um link `nxm://…/collections/…`, ou abre um arquivo `.mo2list` (§6).
2. **Escolhe o destino** (decisão do usuário, 2026-10-28: ao importar, o MO2 pergunta): **instância nova** ou **um perfil** desta instância (§5.0).
3. Revisa a lista: desmarca o que já tem ou não quer.
4. Baixa: automático com Premium; **sem Premium, o MO2 guia um clique por mod** (§5.1; decisão do usuário, 2026-10-28).
5. Espera a instalação e recebe o resumo (o que entrou, o que foi reaproveitado, o que falhou).
6. Abre o jogo com a montagem como o autor deixou: mesmos mods, prioridade, escolhas de FOMOD, ordem de plugins e configurações.

### 1.2 Fluxo de quem exporta

1. No perfil pronto, "Exportar modlist…".
2. Revisa o que vai no pacote (§6.2) e o que fica de fora, com avisos.
3. Salva um `.mo2list` e manda para quem quiser.

### 1.3 Escopo

| Item | Neste documento |
|---|---|
| Jogos | **Skyrim Special Edition** (inclui a Anniversary Edition, mesma instalação) e **Stellar Blade** (decisão do usuário: Skyrim; o Stellar Blade vem da 6b). Skyrim LE/VR, Fallout e outros: fora (decisão do Claude); o código não amarra ao jogo, mas só esses dois são testados. |
| Núcleo: plano de modlist + instalador | Dentro |
| Gravação das escolhas de FOMOD na instalação | Dentro (§4) |
| Importar Nexus Collection | Dentro (§7) |
| Exportar e importar `.mo2list` | Dentro (§6) |
| Conta não Premium | Dentro, com cliques guiados (§5.1). **Muda a 6b**, que exigia Premium. |
| Ordem de plugins `.esp`/`.esm`/`.esl` | Dentro (§5.4). **Muda a 6b**, que a deixava fora. |
| Wabbajack | Fora; spec própria depois. O plano (§3) já prevê fontes que não são da Nexus. |
| Assistente de IA (sugestões, debug) | Fora; spec própria. As sugestões futuras viram mudanças num plano e passam pela mesma janela de revisão. |
| Criar ou publicar Collections na Nexus | Fora |
| Patches binários de arquivos de mod editados (como o Wabbajack faz) | Fora (§6.3) |
| Checagem de revisões novas de um `.mo2list` | Fora: o arquivo é um retrato; uma versão nova é um arquivo novo. As revisões de Collection continuam dentro (§7.4). |

### 1.4 O que muda em relação à 6b

- Não exige mais Premium (§5.1).
- Cobre Skyrim SE, com ordem de plugins (§5.4).
- O que a 6b descrevia como a instalação de uma Collection agora é o instalador genérico do plano (§5); a Collection vira um importador (§7).
- Sai da 6b o "modo não interativo do FOMOD só se a Collection tiver escolhas": ele é obrigatório, porque o `.mo2list` depende dele (§4).
- O resto (janela de revisão, "já instalado"/"versão diferente", instalar ao lado e desativar o antigo, revisões novas, falhas sem parar o lote) continua como a 6b decidiu.

### 1.5 Anti-metas

As da 6b continuam:

- Prioridade final diferente da que o autor definiu, **sem aviso**.
- Reinstalar por cima, sem perguntar, um mod que o usuário já tinha.
- Apagar ou desativar mods do usuário por conta própria.
- Abrir o assistente de FOMOD quando as escolhas estão registradas.

Novas:

- Um `.mo2list` levar **dados pessoais**: caminhos com o nome do usuário, pasta do jogo, prefixo do Proton, tokens, chave de API, saves.
- Um `.mo2list` levar **arquivos de mods de terceiros** (da Nexus ou não): redistribuir viola as permissões dos autores.
- Importar e descobrir no jogo, sem aviso, que algo não veio (escolha de FOMOD ausente, mod manual não baixado, plugin sem posição): tudo que não sai igual ao autor aparece no resumo.

## 2. O que já existe (pontos de partida no código)

- `DownloadManager` recusa links de Collection com "Collections Not Supported" (`deps/modorganizer/src/downloadmanager.cpp:740`).
- O `meta.ini` de cada mod guarda `modid`, `fileid` (lista de pares), `version`, `installationFile`, `repository` (`modinforegular.cpp:267-306`, `installationmanager.cpp:537-554`). É a base do "já instalado".
- O `installer_fomod` **não grava as escolhas** feitas no assistente (verificado em 2026-10-28: nada em `deps/installer_fomod/src` persiste as opções). Só há modo interativo.
- A conta Premium já é conhecida (`apiuseraccount.cpp`, `nexusinterface.cpp`); a ligação OAuth/keyring vem da 4a.
- LOOT integrado na 4b (`lootcli`), para ordenar plugins.
- O reinício do MO2 ao trocar de instância, que a 6b citava como risco, foi corrigido na Fase 5 (`Qt::AA_ShareOpenGLContexts`); falta só a confirmação manual da Fase 5.

## 3. O plano de modlist (núcleo)

Um modelo em memória, serializável em JSON (é o `manifest.json` do `.mo2list`, §6.1), que descreve **o que instalar e como deixar o perfil**. Campos (o construtor fecha os nomes e tipos):

- **Cabeçalho:** nome, autor, versão da lista, jogo (identificador do MO2 + domínio da Nexus), origem (`collection` com `slug`/revisão, ou `mo2list`), versão do esquema.
- **Entradas de mod**, em ordem de prioridade (da mais baixa para a mais alta, como a lista do MO2):
  - nome no MO2, versão, `optional`;
  - **fontes**, uma ou mais, em ordem de instalação na mesma pasta (decisão do Claude: um mod do MO2 pode ter vários arquivos da Nexus — principal + atualização — listados em `installedFiles` do `meta.ini`); cada fonte é uma de:
    - `nexus`: `modId`, `fileId`, MD5 e tamanho quando conhecidos;
    - `manual`: link para a página, nome do arquivo esperado, MD5 e tamanho (mods de fora da Nexus; quem importa baixa e aponta o arquivo, ou o MO2 acha pelo MD5 na pasta de downloads);
    - `bundled`: arquivos dentro do próprio pacote (só os criados pelo autor da lista, §6.2);
    - extensível para as fontes do Wabbajack depois (decisão do Claude: o tipo de fonte é um campo aberto; um importador que não conhece um tipo falha alto);
  - escolhas de FOMOD, no formato de §4, **por fonte**, quando houver;
  - para Collections: `phase`, tipo de mod do Vortex (tradução de caminho da 6b, §5.2 dela);
  - ativo ou não no perfil.
- **Separadores**, na posição da lista.
- **Regras de ordem** (`before`/`after`) quando a origem as tem em vez de uma lista numerada (Collections); o instalador as resolve numa ordem (§5.3).
- **Ordem de plugins** (§5.4): lista de plugins ativos e inativos na ordem, ou "usar LOOT".
- **Configuração do perfil:** INIs do perfil (`Skyrim.ini`, `SkyrimPrefs.ini`, `SkyrimCustom.ini` quando o perfil usa INIs próprios), opções do perfil (INIs locais, saves locais — o valor da opção, não os saves).
- **Executáveis** (SKSE, xEdit, BodySlide, etc.): nome, caminho relativo à instância ou ao jogo, argumentos, e as opções de Proton que o MO2 Linux grava para cada um (Fase 3). Caminhos absolutos viram marcadores (§6.2).

## 4. Escolhas de FOMOD

- **Gravar:** ao terminar o assistente de FOMOD, o `installer_fomod` grava no `meta.ini` do mod as escolhas feitas: por página, por grupo, as opções marcadas, **por nome** (como as Collections do Vortex), ligadas ao arquivo instalado (`modid`/`fileid`), já que um mod pode receber mais de um arquivo. O MO2 passa também a gravar a **ordem** em que os arquivos foram instalados no mod (decisão do Claude). Mods instalados antes disso ficam sem escolhas nem ordem gravadas; o export avisa (§6.2).
- **Reaplicar:** modo não interativo do `installer_fomod` que recebe essas escolhas e instala sem abrir o assistente. Falha alto se uma página, grupo ou opção não existir no `ModuleConfig.xml` daquele arquivo (por exemplo, quem importa baixou uma versão diferente); a falha vai para o resumo com "Instalar manualmente" (fluxo interativo de hoje).
- O formato é o mesmo para as escolhas que vêm de uma Collection e para as que vêm de um `.mo2list` (decisão do Claude).

## 5. Instalador do plano

### 5.0 Destino

A primeira tela do import (de `.mo2list` ou de Collection), antes da janela de revisão, pergunta onde instalar, **sem opção pré-marcada** (decisão do usuário, 2026-10-28):

- **Instância nova:** o MO2 pede o nome (sugestão: o nome da lista) e cria uma instância do mesmo jogo, apontando para a mesma pasta do jogo, com pastas de mods, downloads, perfis e `overwrite` novas. O perfil criado já sai com **INIs locais e saves locais ligados** (decisão do usuário, 2026-10-28), para não mexer nos INIs nem nos saves das outras instâncias do mesmo jogo, que ficam no prefixo do Proton.
- **Um perfil desta instância:** o usuário escolhe um perfil existente ou cria um perfil novo (decisão do Claude: "perfil novo" é a opção mais segura para quem não quer misturar com a montagem que já tem). Um perfil novo criado pelo import também sai com INIs e saves locais ligados. Num perfil existente, as opções do perfil do usuário não mudam sem perguntar (§5.5).

Nas seções seguintes, "perfil atual" quer dizer o perfil escolhido aqui. Um perfil novo é tratado como uma instância nova quanto à prioridade e à ordem de plugins (só os mods da lista ativos, na ordem do plano), mas os mods que a instância já tem continuam instalados para os outros perfis.

### 5.1 Downloads

- **Premium:** como na 6b: links diretos da API, pela fila normal, respeitando o limite de downloads simultâneos; conferência de MD5/tamanho quando o plano tiver.
- **Sem Premium (decisão do usuário, 2026-10-28: "cliques em cada um"):** uma janela de downloads guiados lista os mods pendentes e, um por vez, abre no navegador a página **do arquivo certo** na Nexus (aba de arquivos, com o `fileId`). O usuário clica em "Mod Manager Download"; o link `nxm://` chega ao MO2, que o casa com o item pendente pelo `modId`/`fileId`, põe na fila e abre a página do próximo. Na janela: progresso (N de M), "Abrir de novo", "Pular", e a lista do que falta.
  - Um `nxm://` de um arquivo que não é o pendente: se for de outro item da lista, aceita e marca aquele; se não for da lista, segue o fluxo normal de download do MO2 e não conta como item.
  - O construtor confirma como o link de arquivo da Nexus abre a página no ponto do botão de download; se não houver URL que leve direto ao botão, a página do arquivo basta.
- **Antes de baixar:** os downloads não são compartilhados entre instâncias. Por isso o MO2 procura, na pasta de downloads da instância atual, um arquivo com o mesmo MD5 (ou com o mesmo `modId`/`fileId` no `.meta` do download) e o reaproveita, copiando-o para a instância nova quando esse for o destino (confirmado pelo usuário em 2026-10-28; vale para Premium e não Premium, e reduz os cliques de quem já tem parte da lista).
- Mods `manual`: aparecem na mesma janela guiada com "Abrir página" e "Escolher arquivo baixado…"; conferência por MD5.

### 5.2 Instalação de cada mod

Como na 6b (§5.2 dela): em ordem de `phase`; FOMOD com escolhas reaplicadas (§4); demais pelos instaladores normais; tradução dos tipos de mod do Vortex para Collections; **sem diálogos no meio do lote** — o que pediria decisão manual vai para o resumo com "Instalar manualmente"; `meta.ini` com `modid`/`fileid`/versão e a origem (`modlist=<nome>` e, para Collections, `collection=<slug>`, `collectionRevision=<n>`).

### 5.3 Prioridade

Como na 6b (§5.3 dela): lista numerada do `.mo2list` aplicada como está; regras de Collection resolvidas por ordenação topológica estável; ciclos e regras para mods ausentes avisados no resumo. Instância nova: só os mods da lista. Perfil atual: os mods da lista entram acima dos do usuário, sem mexer nos dele; mods reaproveitados só mudam de posição se o usuário confirmar.

### 5.4 Ordem de plugins (Skyrim)

- **Instância nova:** o `plugins.txt`/`loadorder.txt` do perfil sai igual à ordem do plano. Plugins que o plano lista mas não foram instalados (mod desmarcado ou falho) ficam de fora e vão para o resumo.
- **Perfil atual:** os plugins da lista entram na ordem do plano; os plugins que o usuário já tinha mantêm a ordem relativa entre si. Onde encaixar os da lista em relação aos do usuário fica com o construtor, com esta restrição: nada é decidido em silêncio — o resumo oferece "Ordenar com LOOT" (4b) e diz que a ordem do usuário foi mexida, se foi.
- **Plano sem ordem de plugins** (Collection que não registra, ou "usar LOOT"): o MO2 roda o LOOT no fim, com aviso no resumo.
- **Masters faltando** depois da instalação: listados no resumo, com o plugin que os pede.
- Stellar Blade: não se aplica.

### 5.5 Configuração do perfil e executáveis

- INIs do perfil e opções do perfil: aplicados no perfil de destino. **No perfil atual** (decisão do Claude), o MO2 pergunta antes de sobrescrever INIs que o usuário já tem, mostrando quais arquivos mudam.
- Executáveis: adicionados à instância com os marcadores (§6.2) resolvidos para a máquina de quem importa. Um executável cujo alvo não existe depois da instalação (ex.: xEdit que veio como mod e falhou) aparece no resumo.
- Arquivos `bundled`: **cada mod `bundled` do `.mo2list` volta como um mod próprio**, com o nome, a posição e o estado do original (decisão do Claude; juntar tudo num mod mudaria a prioridade entre eles). Os arquivos do `overwrite` marcados no export voltam como o mod `[<lista>] Overwrite`, no topo da prioridade (que é onde o `overwrite` fica). Os arquivos do autor de uma Collection seguem a 6b: o mod `[<Collection>] Arquivos do autor`.

### 5.6 Janela de revisão e resumo

A janela da 6b (§4 dela, mockup aprovado em 2026-10-25), para qualquer origem, com duas mudanças:

- Rodapé: "Conta Premium ✔" ou "Sem Premium: você vai clicar em N downloads na Nexus" (N já descontando o que será reaproveitado).
- Linhas `manual` mostram "Baixar manualmente" com o link; linhas `bundled` mostram "Incluído na lista".

O resumo do fim (§5.4 da 6b) passa a incluir também: escolhas de FOMOD que falharam, plugins sem posição ou com masters faltando, INIs não aplicados, executáveis sem alvo.

## 6. Formato próprio `.mo2list`

### 6.1 Arquivo

- Um ZIP com extensão `.mo2list` (nome: decisão do Claude), contendo:
  - `manifest.json`: o plano (§3), com `schemaVersion`;
  - `profile/`: INIs do perfil, `plugins.txt`, `loadorder.txt`;
  - `bundled/<mod>/…`: arquivos dos mods `bundled`;
  - `README.md` opcional, escrito pelo autor no momento do export (instruções para quem importa).
- Importar um `schemaVersion` maior que o suportado: o MO2 recusa e pede para atualizar.
- Jogo diferente do da instância atual: só aceita com destino "instância nova" desse jogo (decisão do Claude).
- Abrir: menu ("Importar modlist…") e associação de arquivo `.mo2list` ao MO2 na instalação do AppImage, se o mecanismo de `nxm://` da Fase 5 permitir com o mesmo esforço (decisão do Claude; senão, só o menu).

### 6.2 Exportar

Janela de export a partir do perfil atual:

- **Quais mods (decisão do Claude):** só os **ativos** no perfil, por padrão. Uma opção "Incluir mods inativos como opcionais" leva os inativos como `optional`, que chegam desmarcados na janela de revisão de quem importa. Mods que existem na instância mas só servem a outros perfis nunca entram sem essa opção, para não inflar downloads e cliques.
- **Mods com vários arquivos da Nexus:** viram uma entrada com várias fontes (§3). Sem ordem gravada (mod antigo), segue a ordem do `installedFiles` e avisa.
- **Mods com origem na Nexus** (`modid`/`fileid` no `meta.ini`): entram como fonte `nexus`. Sem MD5 conhecido, o MO2 calcula a partir do arquivo em `downloads`, se ainda existir; senão segue sem MD5.
- **Mods sem origem na Nexus:** o autor escolhe, por mod: "Fonte manual" (pede o link e usa o MD5 do arquivo em `downloads`, se houver), "Criado por mim — incluir arquivos" (`bundled`) ou "Deixar fora". Padrão: "Fonte manual". A opção "incluir arquivos" mostra o aviso de que só vale para conteúdo do próprio autor.
- **Pasta `overwrite`:** listada arquivo por arquivo, desmarcada por padrão; o que for marcado vira `bundled` (decisão do Claude: o `overwrite` costuma ter saída de ferramentas como xEdit, BodySlide, Nemesis, que quem importa pode gerar de novo, e às vezes arquivos grandes).
- **Escolhas de FOMOD:** entram as gravadas (§4). Mods com FOMOD sem escolhas gravadas: aviso "quem importar vai escolher as opções à mão", com a lista.
- **Separadores, prioridade, ativo/inativo, ordem de plugins, INIs do perfil, opções do perfil, executáveis:** sempre entram.
- **Sanitização (anti-meta):** em INIs e executáveis, caminhos absolutos da instância, do jogo, do home e do prefixo do Proton viram marcadores (`%INSTANCE%`, `%GAME%`, `%HOME%`, `%PROTON_PREFIX%`; nomes: decisão do construtor). O export procura o nome do usuário e o home no conteúdo final e, se sobrar algum, mostra onde antes de salvar. Nunca entram: saves, `ModOrganizer.ini`, tokens, chave de API, logs.
- Antes de salvar, um resumo: N mods (Nexus / manual / incluídos / fora), tamanho do pacote, avisos.

### 6.3 Importar um `.mo2list` de outra pessoa (decisão do Claude)

O arquivo vem de terceiros, então:

- Antes da janela de revisão, o MO2 valida o ZIP: caminhos que saem da pasta de destino (`..`, absolutos, links simbólicos) fazem o import ser recusado.
- A janela de revisão mostra uma seção **"Conteúdo executável"**, que precisa de confirmação explícita para continuar quando houver: arquivos `bundled` executáveis ou nativos (`.dll`, `.exe`, `.bat`, `.sh`, `.ps1`, scripts, binários ELF); executáveis do plano (com caminho e argumentos completos), principalmente os que apontam para conteúdo `bundled`; INIs que mudam caminhos ou carregam DLLs. Nada é recusado só por ser executável, porque listas legítimas trazem plugins do SKSE.
- O import **nunca executa nada** por conta própria: os executáveis só são adicionados à lista do MO2.

### 6.4 Limite conhecido

Edições feitas **dentro** da pasta de um mod da Nexus (ex.: um `.ini` do mod editado à mão) não vão no pacote: quem importa recebe o arquivo original. O export não tenta detectar isso nesta versão (decisão do Claude; detectar exige o arquivo original baixado, e corrigir exige patches binários, fora do escopo). A documentação do usuário recomenda manter edições num mod separado ou no `overwrite`.

## 7. Importador: Nexus Collections

### 7.1 Entrada

Como na 6b (§3 dela): "Instalar Nexus Collection…" com campo de URL, e o link `nxm://<jogo>/collections/<slug>/revisions/<n>` abre a mesma janela em vez de "Collections Not Supported". Jogos aceitos: Skyrim SE e Stellar Blade; outro jogo recebe o aviso de não suportado.

### 7.2 Conversão para o plano

- Revisão obtida pela API GraphQL v2 (`collectionRevision`); pacote da Collection (`.7z`) com o `collection.json` e os arquivos do autor.
- `mods[]` → entradas `nexus` (ou `manual` para fontes externas), com `choices` → escolhas de FOMOD (§4), `phase`, tipo de mod do Vortex, `optional`.
- `modRules[]` → regras de ordem.
- Ordem de plugins do Skyrim: o construtor confirma no `collection.json` real onde o Vortex guarda a ordem de plugins e as regras/grupos de plugin (há referência a regras de plugin e a `loadOrder` em Collections de jogos Bethesda); se a Collection só tiver regras de LOOT/grupos, o plano vira "usar LOOT" + essas regras aplicadas como `userlist` do LOOT, se o `lootcli` aceitar com esforço razoável (decisão do Claude; se não, só LOOT e aviso).
- Arquivos do autor → entrada `bundled`.
- `patches`/`hashes` (arquivos que o Vortex corrige depois de instalar): Collections de Skyrim usam isso com mais frequência que as de Stellar Blade. **O construtor confere numa Collection de Skyrim real**; se houver patches, a primeira versão marca esses mods como "instalado sem o patch do autor" no resumo e volta ao usuário antes de implementar a aplicação dos patches.
- **Download do pacote da Collection sem Premium:** o construtor confirma se a API entrega o pacote a contas grátis; se não, o pacote entra como o primeiro clique guiado (§5.1).

### 7.3 Collections de referência

- Stellar Blade: `ftoobp` (da 6b; 314 mods, ~15 GB).
- Skyrim SE: o construtor escolhe uma Collection pequena (até ~30 mods) com FOMOD com escolhas e ordem de plugins, para fixture e teste manual, e uma grande para a escala; o usuário pode trocar.

### 7.4 Revisões novas

Como na 6b (§6 dela), sem mudança.

## 8. Onde fica o código (a decidir pelo construtor, com estas restrições)

- Plano, instalador, janela de revisão, janela de downloads guiados, export/import: `deps/modorganizer`, C++ (como a 6b concluiu: plugin Python não alcança a fila de downloads nem o FOMOD não interativo).
- Gravação e reaplicação de escolhas: `deps/installer_fomod`.
- Cliente GraphQL v2: núcleo, com a conta da 4a.
- O recurso não é específico do Linux; o construtor decide se fica atrás de `#ifndef _WIN32` (como a 6b deixou em aberto).
- Patches nos `deps/` seguem a convenção do projeto.

## 9. Testes

- **Automáticos:**
  - plano: ida e volta JSON, `schemaVersion` maior recusado, tipo de fonte desconhecido falha alto;
  - FOMOD: gravar escolhas de um `ModuleConfig.xml` de teste e reaplicá-las; escolha inexistente falha;
  - export: sanitização (nenhum caminho de home/instância/jogo/Proton nem nome de usuário no pacote), `overwrite` desmarcado por padrão, `ModOrganizer.ini`/saves nunca entram;
  - import de um `.mo2list` de teste: prioridade, separadores, plugins, INIs;
  - export: só ativos por padrão, inativos como `optional` com a opção; mod com vários arquivos vira uma entrada com várias fontes, na ordem;
  - import: ZIP com `..`, caminho absoluto ou link simbólico recusado; conteúdo executável exige confirmação; cada mod `bundled` volta como mod próprio na posição certa;
  - downloads guiados: casar `nxm://` com o item pendente, `nxm://` de outro item da lista, `nxm://` de fora da lista; reaproveitamento por MD5;
  - Collection: parser com fixtures reais (Stellar Blade e Skyrim), regras → ordem (ciclos, mods ausentes), "já instalado / versão diferente / novo".
- **Manual no Bazzite** (`docs/testing/modlists-manual-checklist.md`):
  - Skyrim: exportar um perfil real, importar numa instância nova **com uma conta sem Premium** (cliques guiados), comparar prioridade, plugins, INIs e escolhas de FOMOD com o original, jogar;
  - Skyrim: instalar a Collection pequena em instância nova e no perfil atual com mods já presentes;
  - Stellar Blade: o roteiro da 6b com `ftoobp`.

## 10. Divisão em partes (decisão do Claude)

Cada parte é útil sozinha e tem plano, branch e PR próprios:

1. **Núcleo + `.mo2list`:** plano, gravação e reaplicação de FOMOD, instalador, janela de revisão, downloads Premium e guiados, export e import. Primeiro porque se testa sem depender da API de Collections, e porque é o que o usuário quer para compartilhar.
2. **Collections:** importador (§7), Stellar Blade e Skyrim, e revisões novas.

## 11. Fica com o construtor

- Nomes e tipos exatos do plano e do `manifest.json`.
- Esquema real do GraphQL e do `collection.json` (Skyrim e Stellar Blade); divergências que mudem comportamento visível voltam ao usuário.
- Onde encaixar os plugins da lista no perfil atual (§5.4), dentro da restrição dada.
- Nomes dos marcadores de caminho, layout fino das janelas, textos e ícones.
- Paralelismo de downloads e instalação.
