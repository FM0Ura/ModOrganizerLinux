# Fase 6b — Nexus Collections — Design

- **Data:** 2026-10-25
- **Status:** em revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md)
- **Depende de:** Fase 6a (Stellar Blade, [`2026-10-25-phase6a-stellar-blade-design.md`](2026-10-25-phase6a-stellar-blade-design.md)); Fase 4a (Nexus, `nxm://`, keyring).
- **Divisão (decisão do usuário, 2026-10-25):** fase separada, depois da 6a.
- **Branch (decisão do Claude):** `fase-6b-nexus-collections`, a partir de `fase-6a-stellar-blade`.

## 1. Objetivo e porquê

O MO2 não instala Nexus Collections — hoje, um link de Collection mostra "Collections Not Supported" (`downloadmanager.cpp`), e só o Vortex as instala. O usuário quer instalar uma Collection do Stellar Blade pelo MO2 **como o autor a montou**: mesma prioridade entre os mods e as mesmas opções de instalação que o autor escolheu. E quer **revisar a lista antes de baixar**, porque pode já ter vários desses mods e não quer instalá-los de novo.

### 1.1 Fluxo pedido pelo usuário

1. Copiar a URL da Collection no site da Nexus e colar no MO2.
2. Revisar a lista: escolher o que instalar (desmarcar o que já tem ou não quer), escolher o destino — **instância nova** do MO2 com esses mods (o MO2 pergunta o nome) ou **perfil atual** (decisão do usuário, 2026-10-25: instância nova em vez de perfil novo; o motivo provável, palpite do Claude, é a Collection não se misturar com os mods compartilhados da instância atual).
3. Esperar o download e a instalação.
4. Abrir o jogo com tudo como o autor deixou.

### 1.2 Escopo

| Item | Nesta fase |
|---|---|
| Jogo | **Só Stellar Blade** (decisão do usuário: "foca em Stellar Blade"). O código não precisa ser amarrado ao jogo, mas só o Stellar Blade é suportado e testado. |
| Conta Nexus | **Só Premium** (decisão do usuário). Sem Premium, o MO2 explica e não continua. |
| Ordem de plugins `.esp`/`.esm` (`loadOrder` da Collection) | Fora (Bethesda; não se aplica ao Stellar Blade). |
| Revisões novas da Collection | Dentro (§6). |
| Criar/publicar Collections a partir do MO2 | Fora. |
| Mods de fora da Nexus na Collection (links externos, "browse") | Fora como download automático: aparecem na lista como "baixar manualmente", com o link (decisão do Claude). |

### 1.3 Anti-metas

- A prioridade final no MO2 diferente do que o autor definiu, **sem aviso**.
- Reinstalar por cima, sem perguntar, um mod que o usuário já tinha (perder as escolhas dele).
- Apagar ou desativar mods do usuário por conta própria (no perfil atual, ou numa atualização de revisão).
- Abrir o assistente de FOMOD para cada mod quando o autor já registrou as escolhas.

## 2. O que uma Collection contém (a confirmar pelo construtor)

Uma revisão de Collection é obtida pela API GraphQL v2 da Nexus (`https://api.nexusmods.com/v2/graphql`, consulta de `collectionRevision` por `slug` + `revision` + domínio do jogo). Ela dá os metadados (nome, autor, revisão, tamanho, lista de arquivos com `modId`/`fileId`/opcional) e um `downloadLink` para o **pacote da Collection** (`.7z`), que contém o `collection.json` e os arquivos empacotados pelo autor. Pelo conhecimento atual (Vortex), o `collection.json` tem:

- `mods[]`: nome, versão, `optional`, `source` (`type: nexus`, `modId`, `fileId`, `md5`, `fileSize`, `logicalFilename`, `updatePolicy`), `choices` (escolhas de FOMOD: páginas → grupos → opções), `hashes`/`patches`, `phase`, `fileOverrides`, `instructions`.
- `modRules[]`: regras entre mods (`before`/`after`, `requires`, `conflicts`), referenciando mods por MD5/nome lógico/versão.
- Arquivos empacotados pelo autor (configs, `.ini`, patches).

**Collection de referência (escolhida pelo usuário):** [`ftoobp`](https://www.nexusmods.com/games/stellarblade/collections/ftoobp) ("DEGEN: An NSFW AIO pack", por daedaddy). Consultada pelo GraphQL público em 2026-10-25: revisão 23, **314 arquivos de mod**, ~15,1 GB, sem recursos externos, várias dezenas de opcionais; inclui o **Stellar Blade UE4SS** (1.3), o framework **CNS - Custom Nanosuit System** e dezenas de mods que dependem dele, e um **"Mod Pak Chunk ID Patcher - exe"** (um executável — o construtor verifica se a Collection pede para rodá-lo depois de instalar, e se sim, volta ao usuário antes de implementar). A escala (314 linhas, 15 GB) vale para a janela (filtro, rolagem, totais) e para os downloads (retomada, falhas parciais). O `collection.json` (e se há `choices` de FOMOD, `phase`, `patches`) só é lido com a conta do usuário — **primeira tarefa do plano**: baixar o pacote dessa revisão com a conta Premium e guardar o `collection.json` como fixture; se não houver nenhum FOMOD com escolhas, o modo não interativo do FOMOD (§5.2) sai desta fase e volta ao usuário como "fica para depois".

**O construtor confirma o esquema real** (introspecção do GraphQL e um `collection.json` real de uma Collection do Stellar Blade) antes de planejar; onde divergir deste documento, vale o real, e as divergências que mudarem comportamento visível voltam ao usuário.

## 3. Entrada

- Item novo no MO2 (decisão do Claude: menu de downloads/ferramentas, "Instalar Nexus Collection…") com um campo para colar a URL (`https://www.nexusmods.com/games/stellarblade/collections/<slug>`, com ou sem `/revisions/<n>`).
- **Decisão do Claude:** o link `nxm://stellarblade/collections/<slug>/revisions/<n>` (botão "Download with mod manager" do site) abre a mesma janela, em vez do aviso "Collections Not Supported".
- URL de outro jogo: aviso "só Stellar Blade é suportado nesta versão". Conta não Premium: aviso, sem continuar.

## 4. Janela de revisão (mockup aprovado pelo usuário, 2026-10-25)

Mockup: `.superpowers/brainstorm/*/content/collection-review.html` (local, não versionado). Conteúdo:

- **Cabeçalho:** nome, autor, seletor de revisão (padrão: a mais recente), número de mods, total a baixar, quantos já estão instalados.
- **Destino:** ◉ instância nova (nome sugerido: o da Collection, editável) ou ○ perfil atual (decisão do usuário; substitui o "perfil novo" do mockup).
- **Ferramentas da lista:** filtro, marcar tudo, desmarcar opcionais, ocultar já instalados.
- **Lista**, uma linha por mod: caixa de marcar, ordem (a prioridade calculada, §5.3), nome (com "(opcional)"), versão, tamanho, situação e ação.
  - Opcionais vêm **desmarcados**.
  - **Já instalado, mesma versão/arquivo:** "✔ Já instalado", ação "Reaproveitar" (não baixa).
  - **Já instalado, versão diferente:** "⚠ Você tem a X", com escolha **por mod** entre "Baixar a versão da Collection" e "Usar a minha" (decisão do usuário). "Baixar a versão da Collection" **instala ao lado** (mod novo com o sufixo `[<Collection>]` no nome) e **desativa o antigo no perfil atual**; o antigo continua instalado e intacto para os outros perfis (decisão do usuário).
  - **Novo:** "Novo", com "FOMOD com escolhas do autor" quando houver.
  - **Arquivos do autor:** linha própria, "Incluído na Collection".
  - **Fora da Nexus:** "Baixar manualmente", com o link.
- **Rodapé:** selecionados de N, total a baixar, "Conta Premium ✔", Cancelar / **Baixar e instalar**.

**Comparação com destino "instância nova"** (decisão do Claude): a lista compara com a instância atual mesmo assim. Um mod "já instalado" ali não é copiado; o MO2 reaproveita o arquivo baixado (`downloads` da instância atual, se ainda existir) para não baixar de novo, e instala na instância nova. "Versão diferente" não se aplica (a instância nova recebe a versão da Collection).

**"Já instalado"** (decisão do Claude): reconhecido pelo `modId` + `fileId` da Nexus que o MO2 grava no `meta.ini` de cada mod; MD5 do arquivo como desempate quando o `meta.ini` não tiver os ids. Mesmo `modId` com outro `fileId`/versão = "versão diferente". O construtor decide o que fazer com o mesmo arquivo instalado com outras escolhas de FOMOD (tratar como "versão diferente" é a sugestão).

## 5. Instalação

### 5.1 Download

- Pela fila normal de downloads do MO2, com os links Premium da API (`download_link` do v1, ou o equivalente do v2), respeitando o limite de downloads simultâneos que o MO2 já tem.
- Conferência de MD5/tamanho quando a Collection trouxer.

### 5.2 Instalação de cada mod

- Na ordem de `phase` da Collection (todas as de uma fase antes da próxima).
- **FOMOD:** as escolhas do autor são reaplicadas **sem abrir o assistente**. O `installer_fomod` hoje só tem modo interativo; esta fase adiciona um modo não interativo que recebe as escolhas (páginas/grupos/opções por nome) e falha alto se uma escolha não existir no `ModuleConfig.xml`. Se não houver escolhas registradas, o assistente abre normalmente para aquele mod.
- Mods sem FOMOD passam pelos instaladores normais (incluindo o verificador da 6a).
- **Layout do Vortex (decisão do Claude):** a Collection registra o tipo de mod do Vortex de cada mod; caminhos de FOMOD e de arquivos do autor podem ser relativos à pasta desse tipo (ex.: `~mods`, `Win64`), não à raiz do jogo. A 6b traduz cada tipo de mod do Vortex para Stellar Blade (o construtor levanta a lista na extensão do Vortex para o jogo) no destino equivalente da 6a, prefixando o caminho antes de instalar; isso pode ficar no núcleo, só nesta fase. Tipo desconhecido: o mod falha alto e vai para o resumo (§5.4).
- **Sem interrupções (decisão do Claude):** a instalação roda sem diálogos no meio do lote. Um mod que precisaria de decisão manual (verificador `INVALID`, mais de um instalador possível, FOMOD sem escolhas registradas) é deixado de lado e aparece no resumo do fim com "Instalar manualmente", que abre o fluxo interativo de hoje para ele. Isso substitui o "o assistente abre normalmente" do item de FOMOD acima.
- Nome do mod no MO2: o nome da Collection para aquele mod (decisão do Claude); `meta.ini` com `modId`, `fileId`, versão e a origem (`collection=<slug>`, `collectionRevision=<n>`) para as §4 e §6.
- **Arquivos do autor:** viram um mod próprio, `[<Collection>] Arquivos do autor` (decisão do Claude).
- `patches`/`hashes` (arquivos que o Vortex corrige depois de instalar): **a confirmar** se aparecem em Collections do Stellar Blade; se aparecerem, o construtor volta ao usuário antes de implementar.

### 5.3 Prioridade

- A Collection não guarda uma lista numerada; a ordem vem das `modRules` (`before`/`after`) e dos `fileOverrides`. O MO2 faz uma ordenação topológica estável e aplica no perfil de destino. Sem regra entre dois mods, vale a ordem de listagem da Collection.
- **Instância nova:** só os mods da Collection, nessa ordem, ativos (opcionais desmarcados ficam de fora). O MO2 cria a instância do Stellar Blade (mesma pasta do jogo, pastas da instância novas) e, no fim, oferece trocar para ela. **Atenção:** trocar de instância reinicia o MO2, e o reinício tem uma falha conhecida (QtWebEngine em `aboutToQuit`, pendência dos testes manuais de 2026-10-24, a corrigir depois da Fase 5); se ainda não estiver corrigida, o plano a corrige antes ou a contorna (a instalação na instância nova roda depois do reinício, retomável).
- **Perfil atual:** os mods da Collection entram no fim (acima dos do usuário), na ordem calculada; os mods do usuário não mudam de posição (decisão do Claude). Mods reaproveitados (já instalados) são movidos para a posição que a Collection pede **só se** o usuário confirmar num aviso no fim.
- **Ciclo nas regras, regra para um mod desmarcado ou ausente:** o MO2 avisa qual regra não pôde ser cumprida e segue com a melhor ordem possível, em vez de chutar calado (anti-meta).

### 5.4 Falhas (decisão do Claude)

Um mod que falha (removido da Nexus, download interrompido, MD5 errado, escolha de FOMOD inexistente) não para os outros. No fim, um resumo lista o que entrou, o que foi reaproveitado e o que falhou, com "Tentar de novo os que falharam".

## 6. Revisões novas

Decisão do usuário: quando o autor publicar uma revisão nova de uma Collection instalada, o MO2 **avisa e pergunta se quer atualizar**. Se sim, abre a mesma janela de revisão, comparando com o que já está instalado (já instalado / versão diferente / novo), com a mesma escolha por mod.

- **Quando checar** (decisão do Claude): ao abrir o MO2 e ao trocar para um perfil ligado a uma Collection, no máximo uma vez por sessão por Collection; o aviso não bloqueia.
- **Destino da atualização** (decisão do usuário): **perguntar na hora** — atualizar a mesma instância/perfil ou criar uma instância nova com a revisão nova (a antiga continua jogável).
- **Ligação com a Collection:** a instância (criada pela Collection) ou o perfil (instalação no perfil atual) guarda `slug` + revisão instalada. No perfil atual, pode haver mais de uma Collection.
- **Mods que saíram da Collection na revisão nova** (decisão do Claude): aparecem como "Removido na nova revisão", com a opção de desativar (padrão: desativar numa instância criada pela Collection, manter no perfil atual do usuário); nunca apagados sem confirmação.
- A prioridade é recalculada para os mods da Collection (§5.3).

## 7. Onde fica o código (a decidir pelo construtor, com estas restrições)

- O cliente GraphQL v2 e a ligação com a conta (o núcleo usa OAuth, `nexusoauth*.cpp`, com o token no keyring pela 4a; a checagem de Premium usa o que o MO2 já obtém da conta).
- O modo não interativo do FOMOD em `deps/installer_fomod`.
- A janela de revisão, a fila e a prioridade no `deps/modorganizer` (o núcleo já tem `NexusInterface`, `DownloadManager`, `InstallationManager`, perfis). Um plugin Python não alcança o FOMOD não interativo nem a fila com links Premium, por isso a sugestão é C++ no núcleo; o construtor pode propor outra divisão com justificativa.
- Patches nos `deps/` seguem a convenção do projeto (`deps/<x>: Linux: …` quando específicos; este recurso não é específico do Linux, então o construtor decide se o Windows ganha também ou fica atrás de `#ifndef _WIN32`).

## 8. Testes

- **Automáticos:** parser do `collection.json` (fixture real de uma Collection do Stellar Blade, guardado em `tests/`); ordenação por regras (ciclos, regras para mods ausentes, sem regras); casamento "já instalado / versão diferente / novo" com `meta.ini` falsos; FOMOD não interativo com um `ModuleConfig.xml` de teste (escolhas válidas e inexistentes); detecção de revisão nova.
- **Manual no Bazzite** (`docs/testing/phase6b-manual-checklist.md`), com a Collection `ftoobp` (ou um recorte dela, desmarcando opcionais) e a conta Premium do usuário: instalar numa instância nova; instalar no perfil atual com alguns mods já presentes (um igual, um em versão diferente — conferir que o novo fica ao lado e o antigo desativado); desmarcar um opcional; conferir a prioridade contra a página da Collection; jogar; simular revisão nova (instalar a revisão anterior e abrir o MO2).

## 9. Fica com o construtor

- Esquema exato do GraphQL e do `collection.json` (§2), e o tratamento dos campos não citados aqui.
- Layout fino da janela além do mockup, textos, ícones.
- Onde o perfil guarda a ligação com a Collection.
- Paralelismo de downloads e instalação.
