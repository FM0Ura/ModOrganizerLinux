# AI Assistant, F1 (conversa) — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-11-17-ai-assistant-f1-conversa-design.md`](../superpowers/specs/2026-11-17-ai-assistant-f1-conversa-design.md) · Roadmap: [`../superpowers/specs/2026-11-17-ai-assistant-roadmap-design.md`](../superpowers/specs/2026-11-17-ai-assistant-roadmap-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 do **AppImage local** gerado do `install/` do worktree `ai-assistant-f1` (`appimage-out/ModOrganizer-2.5.2.5-*-x86_64.AppImage`) ou iniciado pelo box (`distrobox enter mo2-run -- <pasta do build>/bin/ModOrganizer`). O MO2 de uso diário precisa estar **fechado** durante o teste (só há uma instância do MO2 por vez).

## 0. Pré-requisitos

- [ ] Claude Code CLI (`claude`) e Codex CLI (`codex`) instalados e **logados** (`claude -p "oi"` e `codex exec "oi"` respondem no terminal).
- [ ] Uma chave de API de **um** provedor de API (OpenAI, Anthropic ou Gemini) ou um Ollama local funcionando, configurada em **Settings**.
- [ ] Antigravity (`agy`) instalado e logado, só para a seção 12 (opcional).
- [ ] Uma instância de Skyrim Special Edition com alguns mods e plugins (a mesma dos outros testes). Para as seções 1 e 10, também uma **segunda instância** (pode ser nova e vazia).
- [ ] Anotar o caminho do histórico da instância: `<pasta de dados dos plugins>/ai_assistant/conversations/<chave>/` (o arquivo `instance.txt` dentro dela mostra a qual instância pertence).

## 1. Dock, ícone e atalho

- [ ] Abrir o MO2 numa **instância nova** (sem layout salvo). Esperado: o painel "AI Assistant" aparece como dock na **lateral esquerda**, com um ícone próprio (não o ícone em branco).
- [ ] Arrastar o dock para outro lugar (por exemplo, a direita), fechar o MO2 e abrir de novo. Esperado: o dock volta **onde o usuário o deixou**, não na esquerda.
- [ ] Com o dock oculto (ou com o foco em outra janela do MO2), pressionar **Ctrl+Shift+A**. Esperado: o dock aparece, vem para a frente e o cursor fica na caixa de entrada.
- [ ] Com o dock já visível, **Ctrl+Shift+A** de novo apenas devolve o foco à caixa de entrada (não esconde o dock nem abre nada duplicado).

## 2. Caixa de entrada

- [ ] Digitar uma linha e pressionar **Enter**. Esperado: a mensagem é enviada.
- [ ] Digitar duas linhas separadas por **Shift+Enter**. Esperado: a quebra de linha é inserida e nada é enviado até o **Enter**.
- [ ] Com um método de entrada (IME, por exemplo acentos mortos ou composição), confirmar a composição com **Enter**. Esperado: o Enter confirma a composição e **não** envia a mensagem.
- [ ] Texto só com espaços ou vazio: **Enter** e **Send** não enviam nada.

## 3. Conversa de 3 turnos com memória (critério de pronto)

Repetir esta sequência **uma vez por provedor**: **Claude Code**, **Codex** e **uma API** (a que foi configurada). Cada provedor precisa de uma conversa nova (**+ New**).

1. Turno 1: "Meu apelido neste teste é Falcão-42. Só responda 'ok'."
2. Turno 2: "Quantos mods ativos eu tenho? Responda em uma linha."
3. Turno 3: "Qual é o meu apelido neste teste?"

- [ ] **Claude Code:** o 3º turno responde "Falcão-42" (usa o que foi dito no 1º).
- [ ] **Codex:** o 3º turno responde "Falcão-42".
- [ ] **API:** o 3º turno responde "Falcão-42".
- [ ] Em cada provedor, o título da conversa no histórico é a 1ª mensagem do usuário, cortada em até 60 caracteres (com "…" quando cortada).
- [ ] Fechar o MO2 e abri-lo de novo na mesma instância. Esperado: o painel abre **a conversa mais recente** da instância, com o transcript completo.
- [ ] Continuar essa conversa reaberta com "E qual era o meu apelido, mesmo?" (Claude, Codex e API, um por vez, reabrindo cada conversa pelo **History**). Esperado: a IA ainda se lembra de "Falcão-42" (a memória sobreviveu ao reinício do MO2).

## 4. Fila e Cancelar

- [ ] Enviar uma pergunta longa (por exemplo, "Liste e explique todos os mods ativos, um por um") e, enquanto "Thinking… N s" aparece, digitar e enviar mais duas mensagens. Esperado: cada uma vira um item "⏳ Queued: …" com **✕**, abaixo do transcript e acima da linha de status. Os itens da fila não aparecem no histórico até serem enviados.
- [ ] Clicar em **✕** de um item da fila. Esperado: o item some e não é enviado.
- [ ] Deixar o turno terminar. Esperado: o item restante da fila é enviado automaticamente, em ordem.
- [ ] Os chips (**Diagnose Crash**, **Check Mod Conflicts**, **Review Load Order**) clicados durante um turno também entram na fila.
- [ ] Repetir com duas mensagens na fila e clicar em **Cancel**. Esperado: o turno atual é interrompido, aparece a nota "Cancelled." e o texto dos itens da fila **volta para a caixa de entrada**, unidos por uma linha em branco; a fila fica vazia.
- [ ] **Durante o "Cancelling…"** (status mostra "Cancelling" enquanto o worker termina, principalmente numa API), enviar mensagens. Esperado: não são enviadas; voltam para a caixa de entrada e o envio só é liberado quando o cancelamento termina.
- [ ] **Resposta tardia (API):** cancelar um turno de API e esperar. Se a API responder depois do cancelamento, a resposta aparece como **nota** "Reply received after cancelling (the assistant remembers it):" seguida do texto.
- [ ] **`/new` enfileirado:** durante um turno, enviar `/new` e depois uma mensagem comum. Esperado: ambos entram na fila; quando o turno atual termina, `/new` inicia a conversa nova e a mensagem seguinte vai para **a conversa nova** (não para a antiga).

## 5. Slash commands

Cada comando é tratado localmente e **nunca** é enviado à IA (conferir que nenhuma resposta da IA aparece).

- [ ] `/help` lista os comandos e os atalhos (Enter, Shift+Enter, Ctrl+Shift+A) numa nota. Durante um turno em andamento, `/help` roda **na hora** (sem fila).
- [ ] `/new` abre uma conversa nova vazia (a anterior fica no histórico).
- [ ] `/history` abre a lista do histórico.
- [ ] `/export` abre o seletor de arquivo e salva a conversa atual em `.md`; ao final, uma nota "Exported to …".
- [ ] `/mode` (sem argumento) mostra o modo atual; `/mode autonomous` e `/mode safe` trocam o modo e o combo acompanha; `/mode xyz` mostra "Unknown mode 'xyz'; use safe or autonomous."
- [ ] `/provider` (sem argumento) lista os provedores; `/provider codex` troca o provedor **e abre uma conversa nova**; `/provider xyz` mostra "Unknown provider 'xyz'. Available: …".
- [ ] `/qualquercoisa` (nome desconhecido) mostra "Unknown command; /help lists the commands." e **não** envia o texto.
- [ ] `//oi` envia a mensagem literal "/oi" para a IA.
- [ ] Durante um turno, `/new`, `/history`, `/mode` etc. entram na fila como mensagem comum (só `/help` roda na hora).

## 6. Provedor e conversa nova

- [ ] **+ New** abre uma conversa vazia; a conversa anterior (se tinha mensagem do usuário) aparece no histórico.
- [ ] Trocar o provedor no combo abre uma conversa nova (a conversa é presa ao provedor).
- [ ] Uma conversa nova **sem nenhuma mensagem do usuário** não aparece no histórico nem cria arquivo em `conversations/`.
- [ ] Durante um turno, os botões **+ New** e **History** (e reabrir uma conversa) ficam indisponíveis.

## 7. Histórico

- [ ] **History** substitui o transcript pela lista (título, data e provedor), da mais recente para a mais antiga; **← Back** volta ao transcript.
- [ ] A busca ("Search conversations...") filtra por título e pelo **texto das mensagens**, sem diferenciar maiúsculas de minúsculas; sem resultado mostra "No conversations found.".
- [ ] Duplo clique (ou Enter) numa conversa a **reabre**: o transcript é redesenhado, o provedor dela é selecionado no combo **sem** criar conversa nova, e dá para continuar de onde parou.
- [ ] Botão direito → **Rename**: o novo título aparece na lista e **não** é sobrescrito por mensagens seguintes.
- [ ] Botão direito → **Export .md**: o arquivo tem o título como `#`, uma seção por mensagem com papel e hora, e as aprovações como linhas "✔ Approved: …" / "✖ Rejected: …" (ver seção 8).
- [ ] Botão direito → **Delete**: pede confirmação ("Delete "…"? This cannot be undone."); confirmar remove da lista e o arquivo some da pasta do histórico.
- [ ] **Apagar a conversa aberta mantém o texto digitado:** com a conversa aberta e um texto não enviado na caixa de entrada, apagá-la pelo histórico. Esperado: vai para uma conversa nova vazia e o texto digitado **continua** na caixa.
- [ ] As permissões do histórico: `ls -l` na pasta mostra arquivos `-rw-------` e pastas `drwx------`; abrir um `.json` e conferir que **não** há chave de API nem token MCP.
- [ ] Corromper de propósito um `<id>.json` (apagar a última chave) e abrir o histórico. Esperado: a conversa corrompida é ignorada e o painel não cai. Apagar o `index.json` e reabrir o histórico: a lista é reconstruída.

## 8. Aprovações no turno e no export

Em **Safe Mode**, pedir uma escrita (por exemplo, "Desative o mod X" ou "Ative o mod Y").

- [ ] **Card e número do turno:** o card de aprovação aparece abaixo do transcript e traz o número do turno no título.
- [ ] **Approve:** clicar em **Approve**. Esperado: o card some, a escrita acontece e uma linha "✔ Approved: tool(args)" aparece no transcript, na ordem do turno.
- [ ] **Reject:** repetir e rejeitar. Esperado: o card some, nada é alterado e aparece "✖ Rejected: tool(args)".
- [ ] **Approve All in Session:** pedir uma escrita e clicar em **Approve All in Session**. Esperado: aparece "✔ Approved for this conversation: …"; uma segunda escrita na mesma conversa **não** pede card e aparece como "✔ Automatic: tool(args)".
- [ ] **Autonomous Mode:** trocar o modo e pedir uma escrita. Esperado: sem card; a linha "✔ Automatic: tool(args)" aparece.
- [ ] **Cancelar com cards pendentes:** pedir uma escrita e, com o card aberto, clicar em **Cancel**. Esperado: o card some e aparece "✖ Cancelled: tool(args)".
- [ ] **Export:** exportar a conversa (botão direito → **Export .md** ou `/export`). Esperado: todas as linhas acima aparecem no `.md` como "✔ Approved", "✔ Approved for this conversation", "✔ Automatic", "✖ Rejected" e "✖ Cancelled".
- [ ] **Reabrir:** fechar e reabrir o MO2 (ou reabrir pelo History). Esperado: as linhas de aprovação aparecem no transcript redesenhado, iguais às de antes.

## 9. Approve All vale só para a conversa aberta

- [ ] Em Safe Mode, dar **Approve All in Session** numa conversa A e confirmar que uma 2ª escrita vira "✔ Automatic".
- [ ] **+ New** e pedir uma escrita. Esperado: **volta a pedir card** (também após `/new` e após trocar o provedor).
- [ ] Voltar à conversa A pelo **History** e pedir uma escrita. Esperado: **volta a pedir card** (a aprovação da sessão não sobrevive à reabertura).
- [ ] Dar **Approve All in Session** de novo, trocar de instância (seção 10), voltar e pedir uma escrita na conversa reaberta. Esperado: **volta a pedir card**.

## 10. Troca de instância

- [ ] Com histórico na instância A (Skyrim SE), usar **File → Change Game / Switch Instance** (ou equivalente) para a instância B (nova). Esperado: o painel mostra **outro histórico** (vazio, se B for nova) e nenhuma conversa da instância A.
- [ ] Criar uma conversa na instância B, voltar à A. Esperado: o histórico da A está intacto e a conversa da B não aparece nela.
- [ ] Ao voltar à A, o painel abre a conversa **mais recente** da A.
- [ ] `ls <pasta de dados dos plugins>/ai_assistant/conversations/`: há uma pasta por instância (chaves diferentes), cada uma com seu `instance.txt`.

## 11. Rascunho e fechar o MO2 durante um turno

- [ ] **Rascunho:** digitar um texto na caixa de entrada **sem enviar** e pôr uma mensagem na fila durante um turno. Fechar o MO2 e abrir de novo. Esperado: o texto da caixa e da fila voltam **para a caixa de entrada** (unidos por linha em branco) e **nada é reenviado sozinho**.
- [ ] **Fechar durante um turno:** iniciar um turno longo (Claude Code e, depois, uma API) e fechar o MO2 enquanto ele roda. Esperado: o MO2 fecha **sem travar nem abrir diálogo de crash**, e não sobra nenhum processo `claude`/`codex` órfão (`pgrep -af 'claude|codex'`).
- [ ] Ao reabrir, a conversa mostra a nota **"Turn interrupted: MO2 was closed."**; o turno **não** é reenviado; o rascunho (caixa e fila) está na caixa de entrada.
- [ ] O fechamento normal do MO2 (sem turno em andamento) continua rápido e sem erro no terminal.

## 12. Antigravity (experimental)

- [ ] Selecionar "Antigravity (CLI)" no combo (recurso experimental nesta fase). Esperado: aparece a nota explicando que o `agy` não tem MCP por sessão e que é preciso usar **Settings → "Register in Antigravity"** uma vez.
- [ ] Clicar em **Register in Antigravity** em **Settings**. Esperado: o registro conclui sem erro (`agy mcp list` mostra o servidor `mo2`).
- [ ] Conversa de 3 turnos (como na seção 3): o Antigravity usa `--conversation <id>`, então o 3º turno deve lembrar o que foi dito no 1º (**memória real**, ainda experimental). Registrar o resultado, mesmo que falhe: a F1 não depende dele.
- [ ] Fechar e reabrir o MO2, reabrir a conversa pelo **History** e continuar: a memória continua (o id da conversa do `agy` foi gravado no arquivo da conversa).

## 13. Sessão perdida

- [ ] **Claude Code:** fazer uma conversa de 2 turnos. Com o MO2 fechado, apagar a sessão em `~/.claude/projects/…` (o arquivo `<id da sessão>.jsonl`; o id está em `claude_session_id` no `<id>.json` da conversa). Abrir o MO2, reabrir a conversa e continuar. Esperado: aparece a nota "The Claude memory of this conversation was lost; continuing in a new session.", a resposta chega (o pedido é reenviado numa sessão nova) e o transcript antigo continua intacto.
- [ ] Depois da nota, mais um turno na mesma conversa funciona normalmente (o id novo foi gravado).
- [ ] **Codex (opcional):** o mesmo com a sessão do Codex em `~/.codex/sessions/…` apagada; a nota diz "The Codex memory …".

## 14. Regressões gerais

- [ ] Settings abre, salva e fecha sem erro; o modo e o provedor escolhidos persistem após reiniciar o MO2.
- [ ] Uma pergunta de leitura (por exemplo, "Quais plugins estão ativos?") funciona nos três tipos de provedor, com as ferramentas do MO2.
- [ ] Nenhum erro/traceback do plugin `ai_assistant` no terminal ou em `logs/mo_interface.log` durante todo o teste.
- [ ] Nada foi criado fora de `<pasta de dados dos plugins>/ai_assistant/` pelo plugin (nenhuma pasta nova no `$HOME` além das de dados do MO2 já existentes).
