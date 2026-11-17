# Assistente de IA — Roadmap de melhorias — Design

- **Data:** 2026-11-17
- **Status:** em revisão
- **Depende de:** PR #19 (`feature-ai-assistant`, plugin do assistente, base deste roadmap) e PR #18 (`modlists-parte1`, núcleo de modlists: plano, instalador em lote, FOMOD sem diálogo).
- **Spec anterior:** [`2026-10-31-ai-assistant-design.md`](2026-10-31-ai-assistant-design.md) (o que já existe).

Este documento não é a spec de uma funcionalidade: ele organiza **todas** as melhorias discutidas em 2026-11-17 em fases, com prioridade, escopo e dependências. **Cada fase vira uma spec própria** (`docs/superpowers/specs/AAAA-MM-DD-ai-assistant-<fase>-design.md`), um plano e um PR, na ordem da §6.

---

## 1. Objetivo e porquê

Visão do usuário (2026-11-17): *"tudo que eu faria manualmente ele pode fazer, e ainda atribuir funcionalidades a mais"*. Concretamente:

- **instalar mods do jeito certo**, perguntando sobre as configurações (opções de FOMOD) e aplicando-as na instalação;
- **instalar mods em lote**;
- **debugar crashes**;
- **identificar problemas antes de executar o jogo**.

Hoje o assistente (PR #19) é um chat que analisa e faz pequenas mudanças, uma ferramenta por vez. O roadmap o transforma num **operador do MO2**: ele conversa por vários turnos, mostra o que está fazendo, monta e aplica planos inteiros com desfazer, instala mods do Nexus com as escolhas certas e cuida da saúde do setup antes e depois de jogar.

### 1.1 Problemas atuais que motivaram (levantados no código em 2026-11-17)

| Problema | Onde |
|---|---|
| Enter não envia a mensagem | `ui/chat_panel.py`: `QPlainTextEdit` sem tratamento de tecla |
| Painel abre à direita | `plugin.py`: `RightDockWidgetArea` |
| **CLIs sem memória entre turnos**: cada turno é um `claude -p` / `codex exec` novo, sem histórico | `agent/cli_runner.py` |
| Sem histórico de conversas; "New Chat" descarta tudo; o transcript não é salvo | `ui/chat_panel.py` |
| Saída mal formatada: `QTextBrowser.insertMarkdown` sem estilo de código, tabelas pobres, cabeçalhos em `<b>` | `ui/chat_panel.py` |
| Sem streaming: até 600 s de "Thinking…" sem nada | `agent/worker.py`, `agent/cli_runner.py` |
| Ferramentas chamadas são invisíveis | idem |
| Mensagem enviada durante um turno é descartada em silêncio | `send_prompt` |
| "Check Mod Conflicts" não tem ferramenta de conflitos por trás | `tools/` |
| `install_mod_from_downloads` abre o instalador gráfico: o agente não escolhe opções de FOMOD | `tools/mod_tools.py` (`organizer.installMod`) |
| Cards de aprovação se acumulam sem vínculo com o turno | `ui/approval_card.py` |
| Plugin sem ícone, sem atalho | `plugin.py` |

---

## 2. Metas, não-metas, anti-metas

**Metas**
- Conversa de verdade: vários turnos com memória, histórico persistente, Enter para enviar, saída ao vivo e bem formatada.
- O agente pode **perguntar no meio do turno** (botões no chat) e **aplicar planos com desfazer**.
- Instalação de mods do Nexus, unitária e em lote, com requisitos resolvidos e escolhas de FOMOD decididas no estilo "recomenda e pergunta o ambíguo".
- Checagem antes de jogar e diagnóstico de crash automáticos.

**Não-metas**
- Contas Nexus **sem Premium** no fluxo do assistente (decisão do usuário, 2026-11-17: "só Premium"). Sem Premium, o assistente diz que a instalação pelo Nexus exige Premium e continua oferecendo o resto.
- Suporte completo ao **Antigravity CLI**: fica "experimental", recebe só o que for barato (§5).
- Voltar a usar QtWebEngine para o chat.
- Jogos além dos que o MO2 Linux já suporta; nada é amarrado a um jogo, mas o teste manual é em **Skyrim SE** (o jogo de teste das modlists).

**Anti-metas** (fracasso mesmo que "funcione")
- O assistente mudar o setup sem que o usuário consiga **desfazer**.
- Quebrar ou alterar o fluxo **manual** do MO2 (instalar à mão, FOMOD interativo, ordem de plugins) para quem não usa o assistente.
- Escritas enquanto a VFS está montada (a regra atual do dispatcher continua valendo).
- Tokens/credenciais em linha de comando ou em arquivo sem 0600 (regra atual continua valendo).

---

## 3. Restrições

- Toda chamada ao organizer continua passando pelo `MainThreadDispatcher` na thread principal; o worker nunca toca o organizer.
- Interface em inglês, como o resto do MO2 e do plugin; textos novos passam por `_tr` (a tradução em si é a §6, fase 8).
- Testes em `tests/ai-assistant/` (pytest, fakes do `conftest.py`); cada fase mantém todos verdes e acrescenta os seus.
- Cada fase cabe num PR revisável, em cima do anterior; o AppImage é gerado e testado à mão no fim de cada fase.
- `pluginDataPath()` é global (não por instância): dados por instância (histórico, snapshots) ficam em subpastas chaveadas pela instância (§6, fase 1).

---

## 4. Decisões tomadas

| Tema | Decisão | Quem |
|---|---|---|
| Nexus | Só Premium | Usuário, 2026-11-17 |
| Escolhas de FOMOD | **B**: o agente recomenda o óbvio (por setup e mods já instalados), pergunta só o de gosto/ambíguo, mostra um resumo antes de instalar e aceita "pergunta tudo deste mod" / "decide sozinho" | Usuário, 2026-11-17 |
| Provedores de primeira linha | **Claude Code CLI, APIs (LangGraph) e Codex CLI**; Antigravity experimental | Usuário, 2026-11-17 |
| Como instalar com escolhas | **A′**: reaproveitar o PR #18 (§5) | Usuário, 2026-11-17 |
| PR da base | PR #19 aberto e aprovado como base | Usuário, 2026-11-17 |
| Painel | Abre à **esquerda** por padrão | Usuário, 2026-11-17 |
| Formatação | Markdown → HTML em Python (`markdown-it-py`) + CSS próprio no `QTextBrowser` | Claude |
| Checagem pré-jogo | Gancho `onAboutToRun`; roda sozinha, avisa, o usuário pode seguir mesmo assim | Claude |
| Diagnóstico de crash | Gancho `onFinishedRun` + crash log novo → oferta proativa no painel | Claude |
| Organização | Uma spec + um plano + um PR por fase | Claude |

---

## 5. Abordagens

### 5.1 Instalação com escolhas de FOMOD (a decisão de maior risco)

- **A′ — Reaproveitar o PR #18 (escolhida).** O PR #18 já tem, em C++: replay de escolhas no `installer_fomod` (formato das Collections do Vortex, sem diálogo, falha alto com `setInstallerDetail`), `installerOptions()`/`batch` no `uibase`, o **plano de modlist** (`modlists/plan.h`, JSON) e o instalador em lote com fila de downloads. A própria spec das modlists diz que o assistente de IA viria depois e que o núcleo deve aceitá-lo "sem retrabalho", e registra que **um plugin Python não alcança a fila de downloads nem o FOMOD não interativo**. Portanto: o assistente **gera um plano de modlist** (fontes Nexus `modId`/`fileId`, escolhas de FOMOD, prioridade, ordem de plugins) e o entrega ao instalador do núcleo por um **ponto de entrada novo exposto ao Python**. Em Python fica só um **leitor** de `ModuleConfig.xml` (para entender as opções e decidir), não um instalador.
  - Prós: pouco código novo, instalação idêntica à das modlists/Collections, fluxo manual intacto.
  - Contra: depende do PR #18 na `main` (teste manual pendente) e de um binding novo.
- **A — Instalador Python próprio (`IPluginInstallerSimple` com prioridade > 110).** Interpretador de FOMOD completo em Python. Descartado: duplica o que o PR #18 já faz.
- **B — Instalador totalmente separado (7z + `createMod` + `meta.ini` à mão).** Descartado: diverge do MO2 (meta.ini, status do download, ids do Nexus).
- **C — Automatizar a janela do FOMOD.** Descartado: frágil, ruim no Wayland.

### 5.2 Demais partes

Caminho único, sem alternativas reais: memória dos CLIs por `claude -p --resume <session_id>` e `codex exec resume <id>` (LangGraph já tem `thread_id`); streaming por `claude -p --output-format stream-json`, `codex exec --json` e `graph.stream()`; perguntas no meio do turno por uma ferramenta `ask_user` no mesmo mecanismo dos cards de aprovação (funciona via MCP e via LangGraph).

---

## 6. Fases

Prioridades: **P0** = sem isso o resto não funciona; **P1** = o centro da visão; **P2** = forte, depende das anteriores; **P3** = extras, cada um pequeno.

```
F0 ─► F1 ─► F2 ─► F3 ─► F4 (instalação) ─► F7 (bisect)
                   │      ▲ PR #18 na main
                   ├────► F5 (saúde pré-jogo)
                   └────► F6 (crash) ─────────► F7
F8 (extras): qualquer ponto depois da F2
```

F5 e F6 não dependem do PR #18: se o teste dele atrasar, vêm antes da F4.

### F0 — Base publicada (feito / em andamento)
- PR #19 aberto (2026-11-17). Merge após revisão.
- **Teste manual e merge do PR #18** — pré-requisito da F4.

### F1 — Conversa (P0)
**Objetivo:** usar o chat como um chat de verdade.
- Enter envia; Shift+Enter quebra linha.
- Dock à esquerda por padrão (o estado salvo pelo usuário continua valendo se ele mover).
- **Memória entre turnos** nos três provedores de primeira linha: guardar o `session_id` do Claude CLI e o id de sessão do Codex por conversa e retomar; LangGraph continua por `thread_id`. Antigravity: reenviar um resumo do histórico no prompt (experimental).
- **Histórico persistente por instância**: cada conversa com título (gerado da primeira mensagem), data, provedor, mensagens e ids de sessão; lista com buscar, renomear, apagar e reabrir para continuar; exportar em Markdown.
- Fila de mensagens: o que é enviado durante um turno entra na fila e é mostrado como pendente.
- Slash commands básicos: `/new`, `/history`, `/export`, `/help`, `/mode`, `/provider`.
- Cards de aprovação aparecem dentro do turno que os pediu e mostram o desfecho (aprovado/rejeitado).
- Ícone do plugin e atalho para abrir/focar o painel (Ctrl+Shift+A).

**Fora:** streaming e formatação nova (F2).
**Pronto quando:** numa conversa de 3 turnos com Claude CLI, Codex e uma API, o 3º turno usa o que foi dito no 1º; fechar e reabrir o MO2 mostra a conversa no histórico e ela continua de onde parou.

### F2 — Saída ao vivo e interação (P0)
**Objetivo:** ver o agente trabalhando e responder a ele no meio do turno.
- Streaming do texto nos três provedores de primeira linha.
- Eventos de ferramenta no chat, recolhíveis: nome, argumentos resumidos, resultado resumido, duração.
- Status por etapa ("Calling list_mods…") no lugar do só "Thinking… N s".
- Formatação: `markdown-it-py` → HTML com CSS próprio; blocos visuais distintos para usuário, assistente, ferramenta, erro e nota; code blocks monoespaçados com botão copiar; tabelas e listas legíveis; cores que seguem o tema claro/escuro do MO2.
- Copiar mensagem e "tentar de novo" (regenerar a última resposta).
- **Ferramenta `ask_user`**: pergunta com opções (botões) e/ou texto livre; o turno espera a resposta; cancelar o turno cancela a pergunta. Base para F3, F4 e F7.
- Mensagem clara quando o CLI não existe ou não está autenticado, com atalho para Settings.

**Pronto quando:** num pedido que chama 3 ferramentas, o usuário vê cada chamada e o texto chegando aos poucos; um `ask_user` de teste mostra botões e o agente continua com a resposta.

### F3 — Planos e desfazer (P1)
**Objetivo:** o agente faz mudanças grandes com segurança.
- **Snapshot do perfil** antes de aplicar: `modlist.txt`, `plugins.txt`, `loadorder.txt`, INIs do perfil e a lista de mods que o plano criar.
- Ferramenta `propose_plan`: o agente descreve um conjunto de mudanças; o chat mostra um **card de plano** com o diff (mods +/−, prioridade antes→depois, plugins ativados/desativados, INIs) e "Aplicar" / "Rejeitar".
- Aplicação em ordem, parando no primeiro erro, com o snapshot restaurável.
- **"Desfazer tudo"** no card depois de aplicado: restaura o snapshot e remove os mods que o plano instalou. Snapshots listados por conversa, com limite de quantidade.
- No modo Safe, escritas dentro de um plano aprovado não pedem aprovação uma a uma; fora de plano, a aprovação por ferramenta continua como hoje.
- No modo Autônomo, o plano é aplicado sem confirmar, mas o card com o diff e o "Desfazer tudo" aparece do mesmo jeito (decisão do Claude).
- "Desfazer tudo" não apaga os arquivos baixados em Downloads, só os mods (decisão do Claude).

**Pronto quando:** um plano que ativa 3 mods, move 1 e desativa 2 plugins é aplicado e desfeito, e o perfil volta byte a byte ao estado anterior.

### F4 — Instalação inteligente (P1)
**Depende de:** PR #18 na `main`, F2 (`ask_user`), F3 (plano/desfazer).
**Primeira tarefa: spike descartável** — expor ao Python a instalação de um plano de modlist (ou de um arquivo com escolhas de FOMOD) e instalar um FOMOD de teste com escolhas, sem diálogo. Se não der, a spec volta ao usuário antes de seguir.
- Cliente Nexus para o assistente: busca de mods, arquivos de um mod, requisitos, versão mais recente, link de download Premium. Reaproveita a conta/OAuth do MO2 (fase 4a); nada de chave nova.
- Ferramentas: `search_nexus_mods`, `get_nexus_mod_files`, `get_nexus_requirements`, `inspect_fomod` (lê o `ModuleConfig.xml` do arquivo baixado: passos, grupos, opções, condições), `install_mods` (monta o plano de modlist e entrega ao núcleo).
- Fluxo: resolver mods e requisitos (avisar o que falta e oferecer incluir) → `propose_plan` com mods, versões, requisitos e ordem → baixar pela fila do MO2 → decidir FOMOD no estilo B (recomendar, `ask_user` no ambíguo, resumo antes) → instalar pelo núcleo → prioridade/separador → checagem rápida (F5, se já existir) → resumo com sucessos, falhas e "Desfazer tudo".
- Instalação de arquivos que já estão em Downloads também passa por esse fluxo (substitui o `install_mod_from_downloads` atual para arquivos com FOMOD).
- Sem Premium: mensagem clara e nada é baixado.

**Pronto quando:** "instala o SkyUI e o USSEP" num Skyrim SE limpo detecta o SKSE faltando, pergunta, instala os três com as escolhas mostradas no resumo, e "Desfazer tudo" remove os três.

### F5 — Saúde do setup / checagem pré-jogo (P1)
- Ferramentas novas: `get_mod_conflicts` (arquivos vencedores/perdedores por mod), `get_missing_masters`, `find_case_conflicts` (mesmo caminho com maiúsculas diferentes entre mods, problema específico do Linux), `inspect_overwrite`, `get_loot_messages` (mensagens da masterlist: plugins sujos, incompatibilidades, patches; via `libloot`), `list_mod_files`, `read_profile_ini`.
- Checagem pré-jogo no `onAboutToRun`: roda as checagens rápidas e, se houver problema, mostra um aviso com "Ver no assistente" e "Jogar mesmo assim". Liga/desliga em Settings. Não pode atrasar o lançamento de forma perceptível quando está tudo bem.
- Chips de ação rápida passam a depender do contexto ("Diagnose crash" só com crash log recente etc.).

**Pronto quando:** com um master faltando e um conflito de maiúsculas criados de propósito, abrir o jogo pelo MO2 mostra o aviso, e o assistente explica os dois e propõe um plano de correção.

### F6 — Diagnóstico de crash (P1)
- `onFinishedRun` + detecção de crash log novo (Crash Logger SSE/AE, Buffout 4, .NET Script Framework): o painel abre com a oferta "o jogo fechou com crash, quer que eu analise?".
- **Parser determinístico** antes do LLM: módulos (DLLs), plugins e form IDs do stack, cruzados com `find_file_owner` → lista de suspeitos com o mod dono. O agente recebe os suspeitos e o trecho relevante, não o log inteiro.
- Ferramenta `analyze_crash_log` que devolve essa estrutura.

**Pronto quando:** um crash log real de cada formato suportado gera suspeitos corretos (fixtures nos testes) e a oferta aparece sozinha depois de um crash.

### F7 — Bisect guiado (P2)
**Depende de:** F2 (`ask_user`), F3 (snapshot), F6 (suspeitos).
- O agente divide os mods suspeitos (ou todos os não essenciais) ao meio, desativa uma metade via plano, pede "abra o jogo e diga se crashou" (`ask_user`; com F6, detecta o crash sozinho), repete até isolar e no fim restaura o snapshot e relata o culpado.

### F8 — Extras (P3; cada item é uma spec pequena, em qualquer ordem depois da F2)
| Item | Nota |
|---|---|
| Diagnóstico Linux/Proton | versão do Proton, DLL overrides (ENB/ReShade), carregamento do SKSE/F4SE, winetricks, erros de VFS/FUSE |
| Ordenação pelo LOOT | aplicar a ordem do `libloot` como plano (F3) |
| Atualizações do Nexus | "estes N mods têm versão nova", atualizar como plano |
| Documentação do mod | ler README/changelog/INIs dentro da pasta do mod |
| Memória por instância | notas do setup que o agente lê e atualiza |
| "Ask AI" no menu de contexto e `@menções` | mods/plugins da lista do MO2 viram contexto da conversa |
| Barra do topo | ícones, toggle Safe/Autônomo com badge, seletor de modelo (lista do Ollama), "Testar conexão" |
| Receitas | slash commands definidos pelo usuário |
| Badges na lista de mods | ⚠️ do assistente com tooltip |
| Comparar perfis | diff e cópia de mudanças entre perfis |
| Tradução da interface | `.ts` do plugin |
| Custo/tokens | por conversa, nos provedores de API |
| MO2 como servidor MCP | documentar/expor para agentes fora do MO2 |
| Antigravity | melhorias de melhor esforço |

---

## 7. Matriz de provedores

| Funcionalidade | Claude CLI | APIs (LangGraph) | Codex CLI | Antigravity |
|---|---|---|---|---|
| Memória entre turnos | `--resume` | `thread_id` | `exec resume` | resumo no prompt |
| Streaming | `stream-json` | `graph.stream()` | `exec --json` | não |
| Eventos de ferramenta | sim | sim | sim | não |
| `ask_user` / planos | via MCP | via ferramenta | via MCP | se o MCP funcionar |
| Testado em cada fase | sim | sim (uma API + Ollama) | sim | não |

---

## 8. Riscos e o que o construtor confirma antes de planejar cada fase

- **F1/F2:** formato real de `claude -p --output-format stream-json` (onde vem o `session_id`, eventos de ferramenta MCP) e de `codex exec --json` / `codex exec resume` nas versões instaladas no host; se `resume` funciona com `-p` e com o MCP por sessão.
- **F2:** `ask_user` via MCP bloqueia a chamada da ferramenta enquanto espera: confirmar que Claude CLI e Codex aceitam uma ferramenta MCP que demora minutos (timeouts do cliente MCP).
- **F4:** se a busca de mods existe na API GraphQL v2 do Nexus e como; se o PR #18 já cobre download Premium pela API (ou só a fila guiada); o formato do ponto de entrada Python (binding no `plugin_python` vs método no `IOrganizer`); cobertura do leitor de FOMOD (condições por flags e por arquivo/plugin instalado).
- **F5:** semântica do `onAboutToRun` no lançador Linux (pode bloquear? roda na thread principal?) e custo das checagens em listas grandes (centenas de mods).
- **F6:** obter crash logs reais de cada formato para fixtures.

Divergências que mudem o comportamento visível voltam ao usuário.

---

## 9. Deixado ao construtor de cada fase

Layout exato dos widgets, formato do arquivo de histórico e de snapshot, nomes internos, limites (quantas conversas/snapshots guardar), textos da interface, e a estrutura de módulos dentro de `deps/plugin_ai_assistant/src/`.
