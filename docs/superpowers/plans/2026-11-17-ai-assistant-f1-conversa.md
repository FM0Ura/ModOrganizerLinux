# AI Assistant F1 — Conversa — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tornar o painel do AI Assistant um chat de verdade: memória entre turnos por provedor, histórico persistente por instância, Enter para enviar, fila, slash commands, desfecho das aprovações no turno, dock à esquerda, ícone e Ctrl+Shift+A.

**Architecture:** Um pacote novo `history/` (sem Qt) guarda cada conversa num JSON (mais um `index.json`) separado por instância. O `cli_runner` passa a montar comandos com sessão (Claude `--session-id`/`--resume`, Codex `exec --json`/`exec resume`) e devolve um `CliTurnResult` com o id de sessão; o `worker` repassa o id e o `thread_id` do LangGraph. O `ChatPanel` passa a ser dirigido por um objeto `Conversation`: toda mensagem exibida é acrescentada a ele, gravada e o transcript é redesenhado a partir dele.

**Tech Stack:** Python 3.14, PyQt6, `mobase`, `langgraph` + `langgraph-checkpoint-sqlite`, pytest. Claude Code CLI 2.1.326, Codex CLI 0.169, `agy`.

**Spec:** `docs/superpowers/specs/2026-11-17-ai-assistant-f1-conversa-design.md` (decisões do usuário em §4; decisões de design marcadas no texto). Roadmap: `docs/superpowers/specs/2026-11-17-ai-assistant-roadmap-design.md`.

## Global Constraints

- Branch `ai-assistant-f1` criada a partir de `ai-assistant-roadmap` (8647f7c), num worktree isolado `.claude/worktrees/ai-assistant-f1`. O PR leva os dois specs para a `main`.
- Código em `deps/plugin_ai_assistant/src/`; testes em `tests/ai-assistant/`. Importa-se o pacote como `ai_assistant` (ver `conftest.py`).
- Testes: `distrobox enter mo2-run -- env TMPDIR=/tmp/mo2t-ai ./build.sh --only ai_assistant --tests` a partir da raiz do worktree. Se o worktree não tiver `install/`, semeie com `cp -a --reflink=auto <repo>/install .` (como nas modlists). Para rodar um teste só, use o mesmo `PYTHONPATH` de `build_ai_assistant` em `build.sh` com `python3 -m pytest -p no:cacheprovider <arquivo>::<teste> -q` dentro da distrobox.
- Nada é criado no `$HOME` real durante `import` ou `init()` (o smoke test verifica); o store só cria pastas na primeira gravação.
- Tudo que toca o `organizer` roda na thread principal; o worker recebe dados prontos.
- Arquivos de conversa e índice: escrita atômica (`tempfile.mkstemp` na mesma pasta + `os.replace`), modo `0o600`; pastas `0o700`. Nenhum token MCP ou chave de API entra nesses arquivos.
- Pasta do histórico: `<pluginDataPath>/ai_assistant/conversations/<instance_key>/`, `instance_key` = primeiros 16 hex do SHA-256 de `os.path.normcase(os.path.realpath(basePath))`.
- Título: até 60 caracteres da 1ª mensagem do usuário, uma linha, cortado na última palavra inteira, com "…" quando cortado.
- Textos novos da UI em inglês, como o resto do plugin (ex.: "Queued", "History", "Turn interrupted: MO2 was closed."), passando por `self.tr`/`QCoreApplication.translate` onde o arquivo já faz isso. Os `*_en.ts` gerados pelo build são revertidos antes do commit.
- Commits terminam com as linhas de atribuição da sessão.
- O teste manual com o MO2 exige o MO2 do usuário fechado (smoke e AppImage local travam com ele aberto).

## Review Focus

1. **Conversa reaberta depois de "Approve All in Session"** — deve voltar a pedir confirmação no modo Safe → Task 5, `test_reopen_resets_session_approvals`.
2. **Resume de sessão inexistente** (CLI reinstalado, `~/.claude` limpo) — o turno não pode falhar; nota + sessão nova → Task 2, `test_lost_session_restarts_with_note`.
3. **`index.json` corrompido ou ausente / conversa JSON truncada** — o painel abre e a lista é reconstruída → Task 1, `test_index_rebuilt_when_corrupt`, `test_corrupt_conversation_skipped`.
4. **Mensagem com acentos, emoji e quebras de linha** (inclusive título com newline e mais de 60 caracteres sem espaço) — título e export corretos → Task 1, `test_title_single_line_unicode`.
5. **Enter durante composição de IME e Enter com o turno rodando** — IME não envia; turno rodando enfileira em vez de descartar → Task 6, `test_enter_during_turn_queues`, `test_enter_ignored_while_composing`.

---

### Task 1: Store de conversas e export Markdown

**Files:**
- Create: `deps/plugin_ai_assistant/src/history/__init__.py`
- Create: `deps/plugin_ai_assistant/src/history/store.py`
- Create: `deps/plugin_ai_assistant/src/history/export.py`
- Modify: `deps/plugin_ai_assistant/src/CMakeLists.txt` (se ele lista os arquivos/pastas instalados, incluir `history/`)
- Test: `tests/ai-assistant/test_history_store.py`

**Interfaces:**
- Produces (`ai_assistant.history.store`):
  - `ROLES = ("user", "assistant", "note", "error", "approval")`
  - `DECISIONS = ("approved", "approved_session", "auto", "rejected", "cancelled")`
  - `SESSION_KEYS: dict[str, str] = {"claude_cli": "claude_session_id", "codex_cli": "codex_session_id", "agy_cli": "agy_conversation_id"}`; qualquer outro provedor usa `"thread_id"`. `session_key(provider: str) -> str`.
  - `@dataclass Message(role: str, text: str, at: str, meta: dict | None = None)`
  - `@dataclass Conversation(id: str, provider: str, created_at: str, updated_at: str, title: str = "", title_custom: bool = False, session: dict[str, str] = {}, messages: list[Message] = [], draft: str = "", version: int = 1)` com
    - `Conversation.new(provider: str) -> Conversation` (uuid4, datas ISO-8601 UTC agora)
    - `add(role: str, text: str, meta: dict | None = None) -> Message` (valida `role`; atualiza `updated_at`; define o título na 1ª mensagem `user` se `not title_custom`)
    - `has_user_message() -> bool`, `to_dict() -> dict`, `Conversation.from_dict(d: dict) -> Conversation` (levanta `ValueError` se faltar campo obrigatório)
  - `make_title(text: str, limit: int = 60) -> str`, `instance_key(base_path: str) -> str`
  - `IndexEntry = dict` com chaves `id, title, provider, created_at, updated_at`
  - `class ConversationStore(data_dir: Path, instance_path: str)` com `folder: Path`, `save(conv) -> bool` (False e nada gravado se `not conv.has_user_message()`), `load(conv_id) -> Conversation | None`, `list() -> list[IndexEntry]` (por `updated_at` desc), `most_recent() -> Conversation | None`, `search(query: str) -> list[IndexEntry]`, `rename(conv_id, title) -> None` (grava `title_custom=True`), `delete(conv_id) -> None`.
- Produces (`ai_assistant.history.export`): `to_markdown(conv: Conversation) -> str`.

- [ ] **Step 1: Write the failing tests** em `test_history_store.py`:
  - `test_save_load_roundtrip(tmp_path)`: conversa `claude_cli` com mensagens de todos os papéis, `session={"claude_session_id": "abc"}`, `draft="x"` → `load` devolve igual (`to_dict()` iguais).
  - `test_empty_conversation_not_saved(tmp_path)`: `save(Conversation.new("openai"))` retorna `False` e a pasta não existe.
  - `test_permissions(tmp_path)`: depois de salvar, `stat` da pasta `0o700`, do arquivo e do `index.json` `0o600`.
  - `test_instances_are_separate(tmp_path)`: dois stores com `instance_path` diferentes no mesmo `data_dir` não veem as conversas um do outro; `instance.txt` contém o caminho legível.
  - `test_index_rebuilt_when_corrupt(tmp_path)`: escrever lixo em `index.json` (e, noutro caso, apagá-lo) → `list()` devolve as 2 conversas, ordenadas por `updated_at` desc, e regrava um índice válido.
  - `test_corrupt_conversation_skipped(tmp_path)`: um `<id>.json` truncado é ignorado por `list()`/`search()` e `load()` devolve `None`.
  - `test_title_single_line_unicode()`: `make_title("Olá 😀\nsegunda linha")` == `"Olá 😀 segunda linha"`; texto de 80 letras sem espaço → 60 caracteres + "…"; frase longa corta na palavra (`not title.endswith(" …")`, `len(title) <= 61`).
  - `test_rename_marks_custom_and_survives_new_messages(tmp_path)`.
  - `test_search_matches_title_and_body_case_insensitive(tmp_path)`.
  - `test_delete_removes_file_and_index_entry(tmp_path)`.
  - `test_most_recent(tmp_path)`.
  - `test_markdown_export(tmp_path)`: contém `# <título>`, uma seção por mensagem com papel e hora, linhas `✔ Approved: set_mod_active(...)` / `✖ Rejected: ...` / `✔ Automatic: ...` para `approval` conforme `meta.decision`.

- [ ] **Step 2: Run tests to verify they fail** — `ModuleNotFoundError: ai_assistant.history`.
- [ ] **Step 3: Implement** `store.py` e `export.py` com as interfaces acima. `index.json` é um cache: reconstruído lendo `*.json` (exceto `index.json`) quando ausente/ inválido. Escrita atômica 0600 numa função `_write_private(path, text)` local (mesmo padrão de `cli_runner._write_private`). Sem Qt, sem `organizer`.
- [ ] **Step 4: Run tests to verify they pass.**
- [ ] **Step 5: Commit** — `feat(ai_assistant): conversation store and Markdown export`.

### Task 2: Turnos de CLI com sessão

**Files:**
- Modify: `deps/plugin_ai_assistant/src/agent/cli_runner.py`
- Create: `tests/ai-assistant/fixtures/codex_exec_json.jsonl` (saída real)
- Test: `tests/ai-assistant/test_cli_sessions.py`; ajustar `test_mcp_and_cli.py::test_build_cli_command_supports_claude_codex_and_agy` às novas assinaturas.

**Interfaces:**
- Produces:
  - `@dataclass CliTurnResult(text: str, session_id: str | None, note: str | None = None)`
  - `build_cli_command(provider, prompt, bridge_path, endpoint_path, force_host_exec=None, *, session_id: str | None = None, resume: bool = False, system_prompt: str | None = None) -> list[str]`
  - `parse_codex_jsonl(stdout: str) -> tuple[str | None, str]` (id da thread, texto da última mensagem do agente)
  - `run_cli_turn(provider, user_message, system_prompt, bridge_path, endpoint_path, cwd=None, cancel_event=None, command_builder=build_cli_command, *, session_id: str | None = None, resume: bool = False, history: list[tuple[str, str]] | None = None) -> CliTurnResult` — `resume=False` é 1º turno (Claude: `session_id` obrigatório, gerado pelo worker; Codex: `session_id=None`, o id vem do JSONL); `resume=True` exige `session_id`.
  - `LOST_SESSION_MARKERS = ("No conversation found with session ID", "no rollout found for thread id")` (+ o do `agy`, se houver)

Comandos exatos (o prefixo `distrobox-host-exec` e os args do bridge continuam como hoje):

| Caso | Comando |
|---|---|
| Claude, 1º turno | `claude -p <prompt> --session-id <uuid> --append-system-prompt <system_prompt> --mcp-config <json> --allowedTools mcp__mo2__*` |
| Claude, seguinte | igual, com `--resume <uuid>` no lugar de `--session-id <uuid>` |
| Codex, 1º turno | `codex exec --json --skip-git-repo-check --sandbox read-only -c mcp_servers.mo2.command="python3" -c mcp_servers.mo2.args=<json> <prompt>` |
| Codex, seguinte | `codex exec resume --json --skip-git-repo-check -c sandbox_mode="read-only" -c mcp_servers.mo2.command="python3" -c mcp_servers.mo2.args=<json> <id> <prompt>` |

- Prompt do Claude: só `"Use the tools of the 'mo2' MCP server to inspect and change this Mod Organizer instance.\n\nUser request: " + user_message` (o contexto vai no `--append-system-prompt`). Codex e `agy`: contexto num bloco curto no começo do prompt, como hoje.
- Claude 1º turno: o worker gera o `uuid4`, emite `session_started` e chama `run_cli_turn(session_id=uuid, resume=False)`; `CliTurnResult.session_id` devolve o mesmo uuid (ou o novo, após sessão perdida).
- Sessão perdida: resume com código ≠ 0 (ou, no Claude, saída que contenha o marcador — o Claude sai com 0 e imprime "No conversation found with session ID: …") → roda de novo como 1º turno uma única vez e devolve `note="The <Provider> memory of this conversation was lost; continuing in a new session."`.
- `agy`: primeiro verificar `agy -p "say ok" --output-format json` (tem `--output-format text|json|stream-json` e `--conversation <id>`). Se a saída JSON trouxer um id de conversa, usar `--output-format json` + `--conversation <id>` e gravar o id; senão, montar o prompt com `history` (últimas mensagens `user`/`assistant`, das mais recentes para trás, até 8 000 caracteres, sob um cabeçalho "Conversation so far:"). Registrar no commit qual caminho foi adotado.

- [ ] **Step 1: Gravar a fixture** — `codex exec --json --skip-git-repo-check "Reply with the single word ok"` (fora do repo, ex.: em `/tmp`) e salvar o stdout em `fixtures/codex_exec_json.jsonl`. Esperado: um evento `{"type":"thread.started","thread_id":...}` e um `item.completed` com `item.type == "agent_message"`; se o formato for outro, o parser segue a fixture.
- [ ] **Step 2: Write the failing tests:**
  - `test_claude_first_turn_and_resume_args()`: argumentos exatos da tabela, com `--append-system-prompt` e sem o contexto dentro do prompt.
  - `test_codex_first_turn_and_resume_args()`: idem; resume sem `--sandbox` e com `-c sandbox_mode="read-only"`, `<id>` antes de `<prompt>`.
  - `test_parse_codex_jsonl_fixture()`: devolve o `thread_id` e `"ok"` (comparação sem espaços/caixa).
  - `test_run_cli_turn_returns_session_id(tmp_path)`: `command_builder` falso que roda `python3 -c` imprimindo a fixture → `CliTurnResult.session_id` == id da fixture; para Claude, o uuid gerado aparece no comando.
  - `test_lost_session_restarts_with_note(tmp_path)`: builder falso cuja 1ª chamada (resume) imprime o marcador do Claude e sai 0, a 2ª responde "hi" → `text == "hi"`, `note` preenchida, `session_id` novo ≠ antigo; mesmo teste para o Codex com código 1 e o marcador dele.
  - `test_cancel_still_raises_turn_cancelled(tmp_path)`.
  - Teste do `agy` conforme o caminho escolhido (args com `--conversation`, ou prompt com o histórico truncado em 8 000 caracteres mantendo as mais recentes).
- [ ] **Step 3: Run tests to verify they fail.**
- [ ] **Step 4: Implement** as interfaces acima em `cli_runner.py`.
- [ ] **Step 5: Run tests to verify they pass** (incluindo o teste antigo ajustado).
- [ ] **Step 6: Commit** — `feat(ai_assistant): resume CLI sessions across turns`.

### Task 3: Worker com sessão, `thread_id` e resposta tardia

**Files:**
- Modify: `deps/plugin_ai_assistant/src/agent/worker.py`
- Test: `tests/ai-assistant/test_worker_sessions.py`

**Interfaces:**
- Consumes: `run_cli_turn(..., session_id=, history=) -> CliTurnResult` (Task 2).
- Produces: `AgentTurnWorker(dispatcher, config, user_text, session_id: str | None, system_prompt, cwd, bridge_path, endpoint_path, sqlite_path=None, history: list[tuple[str, str]] | None = None)` — o parâmetro `thread_id` vira `session_id` (para APIs é o `thread_id`; se `None`, o worker gera um uuid4).
  - Sinais: `session_started = pyqtSignal(str)` (emitido assim que o id é conhecido, antes de `response_ready`; para Claude e APIs, logo no início do `run`), `response_ready = pyqtSignal(str)`, `note_ready = pyqtSignal(str)` (nota do `CliTurnResult`), `error_occurred = pyqtSignal(str)`, `late_response = pyqtSignal(str)` (turno de API cancelado que terminou com resposta).
  - Regra: `resume = session_id is not None`. Se `None` e provedor `claude_cli` (ou API), o worker gera o uuid4 e emite `session_started` antes de rodar; no Codex (e `agy` com id), `session_started` sai quando o `CliTurnResult` traz o id. Se o resultado trouxer um id diferente do enviado (sessão perdida), emite `session_started` de novo com o novo.

- [ ] **Step 1: Write the failing tests:**
  - `test_cli_worker_emits_session_then_response()`: monkeypatch de `run_cli_turn` devolvendo `CliTurnResult("hi", "sid-1", "note")` → ordem dos sinais `session_started("sid-1")`, `note_ready("note")`, `response_ready("hi")`.
  - `test_api_thread_id_reused_across_workers(tmp_path, fake_organizer)`: modelo falso (como em `test_langgraph_agent.py`) que responde com o número de mensagens `HumanMessage` no estado; dois workers com o mesmo `session_id` e o mesmo `sqlite_path` → a 2ª resposta indica 2 mensagens.
  - `test_cancelled_api_turn_emits_late_response()`: `cancel()` antes do fim do `invoke` → `response_ready` não é emitido e `late_response` é emitido com o texto.
- [ ] **Step 2: Run tests to verify they fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run tests to verify they pass.**
- [ ] **Step 5: Commit** — `feat(ai_assistant): worker carries session ids`.

### Task 4: Dispatcher avisa escritas executadas

**Files:**
- Modify: `deps/plugin_ai_assistant/src/tools/dispatcher.py`
- Test: `tests/ai-assistant/test_registry_and_dispatcher.py`

**Interfaces:**
- Produces: `MainThreadDispatcher.write_executed = pyqtSignal(str, dict, str)` — `(tool_name, args, decision)` com `decision` em `"approved" | "approved_session" | "auto"`; emitido para toda ferramenta `is_mutating` que de fato rodou (não para rejeitadas nem bloqueadas pela VFS). `auto` = rodou sem card (Autonomous ou sessão já aprovada). Emitido via thread principal (o `ChatPanel` conecta normalmente; Qt entrega com `QueuedConnection` quando vier do worker).

- [ ] **Step 1: Write the failing tests:** `test_write_executed_decisions(fake_organizer)` — Autonomous → `auto`; Safe + approve → `approved`; Safe + approve_session → `approved_session` e a chamada seguinte → `auto`; reject → nada emitido; VFS montada → nada emitido; ferramenta de leitura → nada emitido.
- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run to verify it passes.**
- [ ] **Step 5: Commit** — `feat(ai_assistant): report executed write tools`.

### Task 5: ChatPanel dirigido pela conversa (memória, histórico, aprovações)

**Files:**
- Modify: `deps/plugin_ai_assistant/src/ui/chat_panel.py`
- Modify: `deps/plugin_ai_assistant/src/ui/approval_card.py` (título com o número do turno)
- Test: `tests/ai-assistant/test_chat_conversation.py`

**Interfaces:**
- Consumes: `ConversationStore`, `Conversation`, `session_key` (Task 1); `AgentTurnWorker` e sinais (Task 3); `write_executed` (Task 4).
- Produces (usados pelas Tasks 6–9):
  - `ChatPanel(dispatcher, mcp_server, data_dir, parent=None, instance_path: str | None = None)` — `instance_path` default `organizer.basePath()` via dispatcher; o store só é criado na primeira gravação.
  - `conversation: Conversation` (atributo), `store: ConversationStore`
  - `new_conversation(provider: str | None = None) -> None` — grava a atual (com o rascunho), cria nova, `dispatcher.reset_session_approvals()`, limpa o transcript.
  - `open_conversation(conv_id: str) -> bool` — grava a atual, carrega, seleciona o provedor no combo **sem** disparar `new_conversation` (use `blockSignals` e atualize `config.provider`), redesenha o transcript, põe `draft` na caixa de entrada, `reset_session_approvals()`.
  - `open_most_recent() -> None`
  - `is_busy() -> bool`
  - `_record(role: str, text: str, meta: dict | None = None) -> None` — acrescenta à conversa, desenha no transcript e grava. **Toda** saída visível passa por ele (`_append_note`/`_append_error` viram chamadas a `_record`).
  - `render_transcript() -> None` — redesenha tudo a partir de `conversation.messages` (assistant em Markdown, os outros como texto).
- Comportamento:
  - Trocar o provedor no combo → `new_conversation(novo)` (opção A), com a nota do Antigravity como hoje.
  - `send_prompt`: passa `conversation.session.get(session_key(provider))` como `session_id` e, para `agy_cli`, `history`; `session_started` grava o id na conversa e salva.
  - Linhas de aprovação: decisão no card → `_record("approval", "✔ Approved: tool(args)" | "✔ Approved for this conversation: …" | "✖ Rejected: …", meta={"tool", "args", "decision"})`; `write_executed` com `auto` → `"✔ Automatic: …"`. Args resumidos: JSON compacto cortado em 120 caracteres. Fim/cancelamento do turno com cards pendentes → resolver como `reject` e gravar `cancelled` ("✖ Cancelled: …").
  - `late_response` → `_record("note", "Reply received after cancelling (the assistant remembers it):\n\n" + texto)`.

- [ ] **Step 1: Write the failing tests** (painel com `fake_organizer`, `data_dir=tmp_path`, worker substituído por um falso que emite os sinais):
  - `test_turn_persists_messages_and_session(tmp_path, fake_organizer)`: enviar "olá" → arquivo da conversa tem `user` + `assistant` e `session["claude_session_id"]`.
  - `test_second_turn_passes_session_id(...)`: o 2º worker recebe o id gravado no 1º.
  - `test_reopen_after_restart(...)`: novo `ChatPanel` no mesmo `data_dir` + `open_most_recent()` → transcript contém as duas mensagens e o próximo turno usa o mesmo id.
  - `test_provider_change_starts_new_conversation(...)` e `test_open_conversation_selects_provider_without_new(...)`.
  - `test_reopen_resets_session_approvals(...)`: `dispatcher._session_approved_all = True` → `open_conversation(...)` → `False`; idem para `new_conversation()`.
  - `test_approval_lines_recorded(...)`: card aprovado, rejeitado, approve_session e `write_executed(..., "auto")` geram as quatro linhas com `meta.decision` corretos.
  - `test_pending_cards_cancelled_at_turn_end(...)`.
  - `test_late_response_recorded_as_note(...)`.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run the whole `tests/ai-assistant` suite** — inclusive `test_ui_widgets.py`.
- [ ] **Step 5: Commit** — `feat(ai_assistant): conversation-driven chat panel with persistent history`.

### Task 6: Enter, fila e rascunho

**Files:**
- Modify: `deps/plugin_ai_assistant/src/ui/chat_panel.py`
- Test: `tests/ai-assistant/test_chat_input_queue.py`

**Interfaces:**
- Consumes: `ChatPanel` da Task 5.
- Produces: `ChatPanel.queue: list[str]` (somente leitura para a UI), `enqueue(text: str) -> None`, `remove_queued(index: int) -> None`, `draft_text() -> str` (caixa + fila unidas por `"\n\n"`, sem itens vazios), `submit(text: str) -> None` (ponto único de entrada: slash commands na Task 7, senão envia ou enfileira).

- Enter/Return sem Shift envia via `submit`; Shift+Enter quebra linha; com IME compondo (`inputMethodQuery`/preedit não vazio) Enter não envia. Implementar com `eventFilter` no `QPlainTextEdit`.
- Fila: widget de itens "⏳ Queued: <texto>" com botão ✕, entre o transcript/cards e a linha de status. Ao terminar um turno, o próximo da fila é enviado. Chips de ação rápida chamam `submit`.
- Cancelar: interrompe o turno e põe `draft_text()` na caixa de entrada, esvaziando a fila.
- `conversation.draft = draft_text()` gravado quando a fila muda e em `new_conversation`/`open_conversation`; limpo no envio.

- [ ] **Step 1: Write the failing tests:** `test_enter_sends_shift_enter_newline`, `test_enter_ignored_while_composing` (`QInputMethodEvent` com preedit), `test_enter_during_turn_queues`, `test_queue_runs_in_order`, `test_remove_queued`, `test_cancel_returns_queue_to_input` (texto final == caixa + fila em ordem, unidos por linha em branco), `test_draft_saved_and_restored`.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run to verify they pass.**
- [ ] **Step 5: Commit** — `feat(ai_assistant): Enter to send, message queue and drafts`.

### Task 7: Slash commands

**Files:**
- Create: `deps/plugin_ai_assistant/src/ui/slash_commands.py`
- Modify: `deps/plugin_ai_assistant/src/ui/chat_panel.py`
- Test: `tests/ai-assistant/test_slash_commands.py`

**Interfaces:**
- Produces: `PROVIDER_ALIASES = {"claude": "claude_cli", "codex": "codex_cli", "agy": "agy_cli", "ollama": "ollama", "gemini": "gemini", "openai": "openai", "anthropic": "anthropic"}`; `@dataclass SlashCommand(name: str, arg: str)`; `parse_slash(text: str) -> SlashCommand | str | None` — `None` = não é comando (texto normal); `str` = texto literal a enviar (caso `//texto` → `"/texto"`); `SlashCommand` para `/new`, `/history`, `/export`, `/help`, `/mode`, `/provider` e também para nomes desconhecidos (o painel trata). `HELP_TEXT: str` lista comandos e atalhos (Enter, Shift+Enter, Ctrl+Shift+A).
- Painel (`submit`): `/help` roda sempre na hora; com turno rodando, os demais comandos entram na fila como texto; desconhecido → nota "Unknown command; /help lists the commands." e nada é enviado; `/mode` sem arg mostra o atual, com `safe|autonomous` troca o combo; `/provider` sem arg lista os nomes, com nome válido troca o combo (→ nova conversa), inválido → nota; `/history` chama `show_history()` (Task 8; até lá, um stub que a Task 8 substitui); `/export` chama `export_current()` (Task 8).

- [ ] **Step 1: Write the failing tests:** parse de cada comando, maiúsculas no nome (`/NEW` = `/new`), `//x`, texto que só contém "/" , `"/ new"` (não é comando); no painel: desconhecido não chama o worker, `/provider codex` cria conversa nova com `codex_cli`, `/mode autonomous` muda o dispatcher, `/help` durante turno vira nota imediata.
- [ ] **Step 2–4:** falhar, implementar, passar.
- [ ] **Step 5: Commit** — `feat(ai_assistant): slash commands`.

### Task 8: Tela de histórico e export

**Files:**
- Create: `deps/plugin_ai_assistant/src/ui/history_view.py`
- Modify: `deps/plugin_ai_assistant/src/ui/chat_panel.py`
- Test: `tests/ai-assistant/test_history_view.py`

**Interfaces:**
- Consumes: `ConversationStore.list/search/rename/delete`, `to_markdown`, `ChatPanel.open_conversation`, `new_conversation`, `is_busy`.
- Produces: `HistoryView(store_getter: Callable[[], ConversationStore], parent=None)` com sinais `open_requested(str)`, `back_requested()`, `export_requested(str)`, `delete_requested(str)`, `rename_requested(str, str)`; método `refresh(query: str = "")`. `ChatPanel.show_history()`, `ChatPanel.export_current()`, `ChatPanel.export_conversation(conv_id: str, path: str | None = None)` (sem `path` abre `QFileDialog.getSaveFileName` com nome sugerido `<título>.md`).
- Layout conforme spec §5.5: `QStackedWidget` com o chat e o `HistoryView`; topo com busca (`QLineEdit`, filtra a cada digitação) e "← Back"; `QListWidget` com "título  ·  data/hora local curta · rótulo do provedor"; duplo clique/Enter abre; menu de contexto Rename (`QInputDialog`), Export .md, Delete (confirmação com `QMessageBox`; apagar a conversa aberta → `new_conversation()`).
- Barra do topo do chat: botões "+ New" (antigo "New Chat"), "History" e "Settings"; "+ New", "History" e o abrir do histórico ficam desabilitados enquanto `is_busy()`.

- [ ] **Step 1: Write the failing tests:** lista ordenada, busca filtra, abrir troca para o chat com a conversa certa, rename atualiza a lista e o título, delete da conversa aberta cria uma nova, export com `path` grava o Markdown de `to_markdown`, botões desabilitados durante o turno. Diálogos substituídos por monkeypatch (nunca exec modal no teste).
- [ ] **Step 2–4:** falhar, implementar, passar.
- [ ] **Step 5: Commit** — `feat(ai_assistant): conversation history view`.

### Task 9: Plugin — dock à esquerda, ícone, atalho, instância e encerramento

**Files:**
- Modify: `deps/plugin_ai_assistant/src/plugin.py`, `deps/plugin_ai_assistant/src/ui/dock_widget.py`
- Create: `deps/plugin_ai_assistant/src/ui/icons/ai_assistant.svg` (e incluir no `CMakeLists.txt` do plugin, se ele lista arquivos)
- Test: `tests/ai-assistant/test_config_and_plugin.py`, `tests/ai-assistant/test_ui_widgets.py`

**Interfaces:**
- Consumes: `ChatPanel.open_most_recent()`, `new_conversation()`, `draft_text()`, worker cancel.
- Produces: `AiAssistantDock.focus_input() -> None` (show, raise, `activateWindow`, foco na caixa de entrada); `ChatPanel.shutdown() -> None` (cancela o worker — CLI: kill —, grava `draft`, registra a nota "Turn interrupted: MO2 was closed." se havia turno, grava a conversa; idempotente).

- `addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, ...)` nos dois ramos de `_on_ui_initialized`; o MO2 restaura a posição salva pelo `objectName` quando existe (confirmar no teste manual).
- `icon()` devolve `QIcon` do SVG ao lado do módulo (`Path(__file__).parent / "ui" / "icons" / "ai_assistant.svg"`); um ícone simples monocromático de balão de conversa, coerente com os ícones Linux do PR #16.
- `QShortcut(QKeySequence("Ctrl+Shift+A"), main_window)` com `Qt.ShortcutContext.ApplicationShortcut` → `dock.focus_input()`; recriado sem duplicar quando `_on_ui_initialized` roda de novo (guardar e `deleteLater` o anterior). `display()` também chama `focus_input()`.
- Instância: o painel usa `organizer.basePath()`; ao rodar `_on_ui_initialized` de novo (troca de instância/`restart_once`) o painel chama `shutdown()` da conversa atual, troca o `instance_path`, `reset_session_approvals()` e `open_most_recent()`. Verifique com `MO2_SMOKE_RESTART_ONCE` qual ramo roda na troca de instância e cubra esse ramo.
- Encerramento: conectar `shutdown()` ao `closeEvent` do dock **e** a `QCoreApplication.aboutToQuit` sem usar QtWebEngine; idempotente. Lembrar do crash de QtWebEngine em `aboutToQuit` já diagnosticado no projeto: o handler só mexe em Python/arquivos.
- Ao abrir o MO2: `open_most_recent()` depois de criar o painel.

- [ ] **Step 1: Write the failing tests:** dock em `LeftDockWidgetArea` (`win.dockWidgetArea(dock)`); `plugin.icon().isNull() is False`; o atalho existe uma única vez depois de dois `_ui_callback`; `focus_input` dá foco à caixa; `test_shutdown_saves_draft_and_interrupt_note` (turno falso rodando + fila → arquivo com `draft` e a nota, worker cancelado); reinit com outro `basePath` abre o histórico da outra instância; ajustar `test_reinit_in_same_process_is_clean`.
- [ ] **Step 2–4:** falhar, implementar, passar.
- [ ] **Step 5: Run the full build + tests** (`./build.sh --only ai_assistant --tests` na distrobox) e o smoke test do projeto, se a máquina permitir (MO2 do usuário fechado).
- [ ] **Step 6: Commit** — `feat(ai_assistant): left dock, icon, Ctrl+Shift+A and clean shutdown`.

### Task 10: Checklist manual e PR

**Files:**
- Create: `docs/testing/ai-assistant-f1-manual-checklist.md`

- [ ] **Step 1: Escrever o checklist** com: o critério de pronto do roadmap (conversa de 3 turnos em Claude CLI, Codex e uma API em que o 3º turno usa o que foi dito no 1º; fechar/reabrir o MO2 e continuar); dock à esquerda numa instância nova e posição do usuário respeitada depois; Ctrl+Shift+A; Enter/Shift+Enter; fila + Cancelar; cada slash command; aprovação aprovada/rejeitada/Approve All/automática aparecendo no turno e no export; Approve All não vale depois de reabrir; troca de instância mostra outro histórico; Antigravity (experimental); sessão perdida (apagar a sessão do Claude em `~/.claude/projects/...` e continuar).
- [ ] **Step 2: AppImage local** para o teste manual do usuário (passos em `~/Documentos/AgentVault/projects/ModOrganizerLinux/local-build-smoke-needs-mo2-closed.md`; `MO2_VERSION` no `build-appimage`).
- [ ] **Step 3: Commit, push e abrir o PR** `ai-assistant-f1` → `main` (o CI está desabilitado; dizer isso no corpo do PR e listar os testes rodados localmente).
