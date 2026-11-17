# Assistente de IA — F1: Conversa — Design

- **Data:** 2026-11-17
- **Status:** em revisão
- **Roadmap:** [`2026-11-17-ai-assistant-roadmap-design.md`](2026-11-17-ai-assistant-roadmap-design.md), fase F1 (P0).
- **Base:** `main` em e838273 (PR #19, plugin `deps/plugin_ai_assistant`; PR #18 e #20).

## 1. Intenção e porquê

O usuário quer que o assistente seja um operador do MO2 ("tudo que eu faria manualmente ele pode fazer"). Pedidos assim têm vários passos e várias idas e voltas; hoje cada turno de CLI é um processo novo sem memória, o transcript some ao clicar "New Chat" ou fechar o MO2, Enter não envia e uma mensagem enviada durante um turno é descartada em silêncio. A F1 transforma o painel num chat de verdade e é a base das fases F2–F8.

## 2. Metas, não-metas, anti-metas

**Metas**
- Enter envia; Shift+Enter quebra linha.
- Dock à esquerda na primeira abertura.
- Memória entre turnos em Claude CLI, Codex CLI e nas APIs (LangGraph); Antigravity com o melhor esforço (§5.4).
- Histórico persistente separado por instância: listar, buscar, reabrir e continuar, renomear, apagar, exportar em Markdown.
- Fila de mensagens enviadas durante um turno.
- Slash commands `/new`, `/history`, `/export`, `/help`, `/mode`, `/provider`.
- Desfecho das aprovações registrado no turno que as pediu.
- Ícone do plugin e atalho Ctrl+Shift+A.

**Não-metas (ficam para a F2)**
- Streaming, eventos de ferramenta visíveis, formatação nova (markdown-it-py/CSS), cards dentro do transcript, `ask_user`.
- Trocar de provedor dentro de uma conversa (decisão do usuário, §4).

**Anti-metas**
- Perder o que já foi dito: fechar o MO2 ou cancelar um turno não pode apagar mensagens já mostradas nem mensagens na fila.
- Escrever dados do assistente dentro da pasta da instância (entraria em backups e exportações de modlist).
- Token do MCP ou chaves de API dentro dos arquivos de conversa ou em linha de comando (regras atuais continuam).
- Alterar o comportamento do MO2 para quem não usa o assistente.

## 3. Restrições

- `organizer.pluginDataPath()` é **global** no Linux (`<AppLocalData>/plugins/data`, `organizercore.cpp`); não muda com a instância. A separação por instância é feita pelo plugin (§5.2).
- O Claude CLI só retoma uma sessão a partir do mesmo `cwd`; o `cwd` do turno já é `organizer.basePath()` e deve continuar sendo.
- `codex exec resume` (0.169) aceita `-c`, `--json`, `--skip-git-repo-check`, mas **não** `--sandbox`: o modo somente leitura vai por `-c sandbox_mode="read-only"`.
- `QTextBrowser` não hospeda widgets: os cards de aprovação continuam fora do transcript na F1.
- Tudo que toca o `organizer` roda na thread principal (regra do PR #19); o worker só recebe dados prontos.
- Escritas de arquivo: atômicas (`tempfile` + `os.replace`) e 0600, como `config.json`.
- Python 3.14, PyQt6, testes em `tests/ai-assistant/` com pytest; execução e build na distrobox `mo2-run` (como no plano do PR #19).

## 4. Abordagens consideradas

**Provedor e conversa** — o usuário escolheu **A: a conversa fica presa ao provedor**. Trocar o provedor (combo ou `/provider`) abre uma conversa nova; reabrir uma conversa do histórico seleciona o provedor dela. Descartada B (troca com resumo): memória aproximada e mais casos de teste.

**Armazenamento do histórico** — o usuário escolheu **1: um arquivo JSON por conversa** mais um índice.
- 2 (tabelas no `checkpoints.sqlite`): busca rápida, mas mistura com o LangGraph, migração de esquema própria e um arquivo corrompido leva tudo. Descartada.
- 3 (ler o histórico dos próprios CLIs): formatos internos que mudam entre versões, um leitor por provedor, nada equivalente nas APIs. Descartada.

## 5. Design

### 5.1 Modelo de conversa

```text
Conversation
  id: str (uuid4)          title: str            title_custom: bool
  provider: str            created_at / updated_at: ISO-8601 UTC
  session: {               # só o campo do provedor da conversa é usado
    claude_session_id?: str, codex_session_id?: str,
    agy_conversation_id?: str, thread_id?: str }
  messages: [ {role: user|assistant|note|error|approval, text: str, at: ISO-8601,
               meta?: {tool, args, decision}} ]
               # decision: approved | approved_session | auto | rejected | cancelled
  draft: str               # texto não enviado (caixa de entrada + fila) — ver §5.8
  version: 1
```

- **Título:** os primeiros ~60 caracteres da primeira mensagem do usuário (uma linha, cortado na palavra, com "…"); sem chamar a IA. Renomear grava `title_custom = true`.
- Uma conversa só aparece no histórico depois da primeira mensagem do usuário (conversas vazias não são gravadas).
- Mensagens `note` gerados pela UI (ex.: aviso do Antigravity) são gravadas como as outras.

### 5.2 Onde e como grava

```
<pluginDataPath>/ai_assistant/conversations/<instance_key>/
    index.json            # [{id, title, provider, created_at, updated_at}], ordenado por updated_at
    <conversation id>.json
```

- `instance_key` = primeiros 16 hex do SHA-256 do caminho absoluto normalizado de `organizer.basePath()`. Um arquivo `instance.txt` na pasta guarda o caminho legível (para diagnóstico).
- Gravação depois de cada mensagem acrescentada (e de renomear/apagar), atômica e 0600; pasta 0700.
- `index.json` é um cache: se faltar ou estiver inválido, é reconstruído lendo os arquivos de conversa; arquivo de conversa inválido é ignorado na lista (e registrado no log), nunca derruba o painel.
- **Busca:** por título e pelo texto das mensagens (case-insensitive, substring); lê os arquivos sob demanda. Aceitável para algumas centenas de conversas.
- **Apagar:** remove o arquivo e a entrada do índice. Não apaga a sessão do lado do CLI nem o checkpoint do LangGraph (ficam órfãos, inofensivos).
- **Exportar Markdown:** `QFileDialog` para escolher o destino; título como `#`, uma seção por mensagem com papel e hora; aprovações como linhas "✔ Aprovado: tool(args)" / "✖ Rejeitado: …".

Um módulo novo, `src/history/store.py` (`ConversationStore`), concentra isso e não depende de Qt nem do `organizer` (recebe o caminho da instância), para ser testável isoladamente.

### 5.3 Memória por provedor

| Provedor | 1º turno | Turnos seguintes |
|---|---|---|
| Claude CLI | gera uuid4, grava em `claude_session_id` antes de rodar; `claude -p <prompt> --session-id <uuid> ...` | `claude -p <prompt> --resume <uuid> ...` |
| Codex CLI | `codex exec --json ...`; lê o evento de início de thread na saída JSONL e grava o id | `codex exec resume <id> --json -c sandbox_mode="read-only" ...` |
| APIs (LangGraph) | gera `thread_id` (uuid4) e grava | mesmo `thread_id`; o `SqliteSaver` em `checkpoints.sqlite` já guarda as mensagens |
| Antigravity | §5.4 | §5.4 |

- Com `--json` o Codex deixa de devolver texto puro: a resposta é a última mensagem do agente nos eventos (ou `-o/--output-last-message <arquivo>` num arquivo temporário 0600 — escolha do implementador, desde que coberta por teste com saída gravada de verdade).
- **System prompt:** continua sendo reconstruído a cada turno (o estado do MO2 muda entre turnos). No Claude, passar por `--append-system-prompt` em vez de concatenar ao prompt do usuário, para não repetir o contexto inteiro dentro do histórico da sessão; no Codex e no Antigravity, que não têm opção equivalente, o contexto vai num bloco curto no começo do prompt de cada turno.
- **Sessão perdida:** se o resume falhar porque a sessão não existe mais (CLI reinstalado, arquivos apagados), o turno mostra uma nota "A memória desta conversa no <provedor> foi perdida; continuando numa sessão nova", grava o novo id e reenvia o pedido sem resume. O transcript local continua intacto.
- Um turno cancelado no 1º turno do Codex (sem id ainda) deixa a conversa sem `codex_session_id`; o próximo turno começa uma sessão nova.

### 5.4 Antigravity (experimental)

O `agy` tem `--conversation <id>`. O implementador verifica se o id pode ser obtido de forma estável (saída, flag de formato ou arquivo de estado):
- **Se sim:** mesma estratégia do Codex, gravando `agy_conversation_id`.
- **Se não:** reenviar no prompt as últimas mensagens `user`/`assistant` da conversa (até ~8 000 caracteres, das mais recentes para trás), marcadas como histórico.

Em ambos os casos o Antigravity continua rotulado como experimental; a F1 não é bloqueada por ele.

### 5.5 Painel

```
┌ AI Assistant ─────────────────────────────────────┐
│ [Claude Code ▾] [Safe ▾] [+ Nova] [Histórico] [⚙] │
│ ─ "Por que o SKSE não carrega…" ─                 │
│ Você: …                                           │
│ AI Assistant: …                                   │
│ ✔ Aprovado: set_mod_active(SkyUI, true)           │
│ ⏳ Na fila: "e agora o load order?"          [✕]  │
│ Pensando… 12 s — Claude Code            [Cancelar]│
│ ┌──────────────────────────────────────┐ [Enviar] │
│ └──────────────────────────────────────┘          │
└───────────────────────────────────────────────────┘
Histórico (QStackedWidget: substitui o transcript no mesmo painel)
│ [buscar…                               ] [← Voltar] │
│ Por que o SKSE não carrega…        hoje 14:02 · Claude │
│ Instalar ENB…                       12/11 · Codex   │
  duplo clique / Enter: reabrir · botão direito: Renomear · Exportar .md · Apagar
```

- **Entrada:** `QPlainTextEdit` com filtro de tecla — Enter/Return (sem Shift) envia; Shift+Enter insere quebra; durante composição de IME, Enter não envia.
- **Nova conversa:** botão "+ Nova", `/new`, ou trocar o provedor. A conversa atual já está gravada.
- **Reabrir:** carrega a conversa, redesenha o transcript a partir das mensagens gravadas, seleciona o provedor dela no combo (sem disparar "nova conversa") e continua de onde parou. Reabrir ou começar outra conversa durante um turno: o botão/comando fica desabilitado enquanto um turno roda.
- **Ao abrir o MO2 / trocar de instância:** abre a conversa mais recente da instância, se houver; senão, painel vazio.
- **Apagar a conversa aberta:** confirma num diálogo e vai para uma conversa nova vazia.
- **Fila:** mensagem enviada durante um turno vira item "⏳ Na fila" (abaixo do transcript, acima da linha de status) com ✕. Ao terminar o turno, o próximo da fila é enviado. Itens da fila não são gravados no histórico até serem enviados. **Cancelar** interrompe o turno atual e devolve o texto dos itens da fila para a caixa de entrada (unidos por linha em branco), esvaziando a fila. Os chips de ação rápida também entram na fila.
- **Aprovações:** o card continua abaixo do transcript (limitação do `QTextBrowser`). Ao decidir, o card some e uma linha `approval` é acrescentada ao turno ("✔ Aprovado: tool(args resumidos)" ou "✖ Rejeitado: …") e gravada. Se o turno terminar ou for cancelado com cards pendentes, eles são rejeitados e registrados como "✖ Cancelado". Cards trazem no título o número do turno a que pertencem.
  - *(decisão de design)* Escritas que rodam **sem card** (modo Autonomous ou depois de "Approve All in Session") também geram uma linha `approval` ("✔ Automático: tool(args)", `decision: auto`); isso exige um sinal novo no `MainThreadDispatcher` (ex.: `write_executed(tool, args, decision)`), emitido na thread principal. "Approve All in Session" grava `approved_session` na linha do card que o concedeu. Esse registro é a base do desfazer da F3.
  - *(decisão de design, segurança)* "Approve All in Session" vale **só para a conversa aberta**: `reset_session_approvals()` é chamado em nova conversa (botão, `/new`, troca de provedor), ao reabrir uma conversa do histórico e ao trocar de instância. Uma conversa reaberta sempre volta a pedir confirmação no modo Safe.
- **Dock:** `plugin.py` usa `LeftDockWidgetArea` no `addDockWidget`. Como o `objectName` é fixo, o MO2 restaura a posição salva pelo usuário quando ela existe; o implementador confirma isso no teste manual.
- **Ícone:** um ícone SVG/PNG embutido no plugin (`icon()` deixa de devolver `QIcon()`), coerente com os ícones Linux do PR #16.
- **Atalho Ctrl+Shift+A:** `QShortcut` na janela principal (`Qt.ShortcutContext.ApplicationShortcut`) que mostra o dock, o levanta e põe o foco na caixa de entrada. Não há outro Ctrl+Shift+A no MO2 (verificado em `deps/modorganizer/src`).

### 5.6 Slash commands

Interpretados localmente quando a mensagem começa com `/` seguida de um nome conhecido; nunca enviados à IA. Nome desconhecido → nota "Comando desconhecido; /help lista os comandos" (o texto não é enviado). `//texto` envia "/texto" literal.

| Comando | Efeito |
|---|---|
| `/new` | nova conversa |
| `/history` | abre a lista do histórico |
| `/export` | exporta a conversa atual em Markdown |
| `/help` | nota com a lista de comandos e atalhos |
| `/mode safe\|autonomous` | troca o modo (sem argumento: mostra o atual) |
| `/provider <nome>` | troca o provedor e abre conversa nova; nomes aceitos: `claude`, `codex`, `agy`, `ollama`, `gemini`, `openai`, `anthropic` (sem argumento: lista) |

Durante um turno, só `/help` roda na hora; os outros entram na fila como uma mensagem comum.

### 5.8 Fechar o MO2, cancelar e coerência com a memória do provedor *(decisões de design)*

- **Rascunho:** o texto da caixa de entrada e os itens da fila (unidos por linha em branco) são gravados no campo `draft` da conversa quando a fila muda, ao trocar/reabrir conversa e ao fechar o MO2; ao reabrir a conversa, o `draft` volta para a caixa de entrada (não é reenviado sozinho) e o campo é limpo no envio. Isso resolve a anti-meta "nada se perde" sem reenviar nada automaticamente.
- **Fechar o MO2 durante um turno:** um gancho de encerramento (`QApplication.aboutToQuit` ou `closeEvent` do dock — o implementador escolhe, lembrando do crash de QtWebEngine em `aboutToQuit` já diagnosticado no projeto) cancela o worker (CLI: `kill`), grava o rascunho e acrescenta a nota "Turno interrompido: o MO2 foi fechado." Ao reabrir, o turno **não** é reenviado; o usuário decide.
- **Turno de API cancelado:** hoje o worker é abandonado e o `graph.invoke` continua, gravando no checkpoint. Para o transcript não divergir da memória: o envio continua bloqueado ("Cancelando…") até o worker terminar, e se ele terminar com resposta depois do cancelamento, ela é acrescentada como nota "Resposta recebida depois do cancelamento (o assistente se lembra dela):" + texto. Escritas feitas nesse intervalo já aparecem pelas linhas `approval` acima.
- **CLI morto no meio do turno:** a sessão do CLI pode guardar parte do turno; aceito. A nota "Cancelado." já diz ao usuário que o turno foi interrompido.

### 5.7 Organização do código (sugerida)

- `src/history/store.py` — `Conversation`, `ConversationStore` (sem Qt).
- `src/history/export.py` — Markdown.
- `src/agent/cli_runner.py` — comandos com sessão (`build_cli_command` recebe o estado da sessão e devolve o id novo quando houver); parse do JSONL do Codex.
- `src/agent/worker.py` — recebe a conversa/ids e emite o id de sessão obtido (`session_started(str)`).
- `src/ui/chat_panel.py` — fila, slash commands, Enter, linhas de aprovação; desenhar o transcript a partir das mensagens.
- `src/ui/history_view.py` — lista com busca e menu de contexto.
- `src/plugin.py` — dock à esquerda, ícone, atalho, caminho da instância para o store.

## 6. Testes

Automáticos (pytest, `tests/ai-assistant/`):
- Store: criar/gravar/reabrir; índice reconstruído quando falta ou está corrompido; conversa corrompida ignorada; permissões 0600/0700; separação por instância; busca; renomear; apagar; título a partir da 1ª mensagem.
- Export Markdown com todos os papéis.
- Comandos dos CLIs: 1º turno e resume para Claude e Codex (argumentos exatos, incluindo `-c sandbox_mode`); parse do JSONL do Codex com uma saída real gravada como fixture; sessão perdida → nova sessão + nota.
- LangGraph: `thread_id` gravado e reutilizado entre duas instâncias do painel (memória no 2º turno com um modelo falso).
- UI: Enter envia / Shift+Enter quebra; fila (envio em ordem, ✕, cancelar devolve à caixa); slash commands (conhecido, desconhecido, `//`); linha de aprovação gravada; reabrir conversa seleciona o provedor sem criar conversa nova.

Manual (critério de pronto do roadmap): numa conversa de 3 turnos com Claude CLI, Codex e uma API, o 3º turno usa o que foi dito no 1º; fechar e reabrir o MO2 mostra a conversa no histórico e ela continua de onde parou. Mais: dock à esquerda numa instância nova e posição do usuário respeitada depois; Ctrl+Shift+A; troca de instância mostra outro histórico.

## 7. O que fica com o implementador

- Formato exato de leitura do JSONL do Codex vs. `--output-last-message` (com fixture real).
- Estratégia do Antigravity (§5.4), após verificar o `agy`.
- Desenho do ícone.
- Detalhes visuais do item da fila e da lista do histórico, dentro do esboço da §5.5.
