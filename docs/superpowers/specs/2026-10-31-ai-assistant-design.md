# Assistente de IA Integrado (LangGraph + Ponte MCP para CLIs) — Design

- **Data:** 2026-10-31
- **Status:** em revisão
- **Depende de:** Fase 4d.1 (`plugin_python` com Python 3.14 isolado e PyQt6) e pasta de dados gravável de plugins (`~/.local/share/ModOrganizer/plugins/data`, v2.5.2.4).
- **Branch:** `feature-ai-assistant`, a partir de `main` (`80754ca`, tag `v2.5.2.4`), em worktree isolado (`.claude/worktrees/ai-assistant`).

---

## 1. Objetivo e porquê

Integrar ao Mod Organizer 2 Linux um **Agente de IA** acessível diretamente por um painel acoplável (`QDockWidget`) na janela principal do aplicativo. O agente deve ser capaz de conversar com o usuário, diagnosticar falhas e crashes lendo os logs da instância e do prefixo Proton, inspecionar a árvore virtual de dados (`Data`) para descobrir qual mod fornece arquivos conflitantes ou problemáticos, e executar operações completas de CRUD sobre mods, plugins e configurações INI.

O sistema precisa atender a duas formas de conexão com modelos de IA:
1. **Agentes de Linha de Comando (CLIs) já autenticados no host:** **Claude Code (`claude`)**, **OpenAI Codex (`codex`)** e **Antigravity CLI (`agy`)**, aproveitando as assinaturas que o usuário já possui na máquina sem exigir chaves de API avulsas.
2. **Provedores via API e Modelos Locais (via LangChain + LangGraph):** **Ollama / LM Studio** (execução 100% local), **Google Gemini API**, **OpenAI / OpenRouter API** e **Anthropic API**.

---

## 2. Metas, não-metas e anti-metas

### Metas
- **Painel de Chat nativo em PyQt6 (`QDockWidget`):** acoplável na janela principal do MO2 (como aba lateral ou painel direito) com renderização em Markdown, histórico persistente por instância/perfil, seletor de backend e seletor de nível de autonomia.
- **Catálogo unificado de ferramentas sobre o `mobase`:** dividido estritamente em ferramentas de leitura (`safe_tools`) e ferramentas de escrita/mutação (`write_tools`).
- **Dois perfis de permissão configuráveis em tempo real:**
  - **Modo Seguro (`safe`):** ferramentas de leitura executam automaticamente; ferramentas de escrita pausam a execução e exibem um Card de Aprovação visual no chat com os botões **[Aprovar]**, **[Aprovar Tudo na Sessão]** e **[Rejeitar]**.
  - **Modo Autônomo (`autonomous`):** tanto ferramentas de leitura quanto de escrita são executadas diretamente (sempre despachadas na thread principal do Qt) e relatadas no histórico.
- **Suporte nativo a LangChain + LangGraph:** grafo de estados (`StateGraph`) com persistência em SQLite (`SqliteSaver`) e suporte a `interrupt_before=["write_tools"]`.
- **Ponte MCP (Model Context Protocol) local integrada:** servidor JSON-RPC/HTTP local escutando apenas em `127.0.0.1` com token de autenticação de sessão, expondo as mesmas ferramentas do `mobase` para os agentes CLI (`claude`, `codex`, `agy`) tanto quando invocados pelo próprio painel do MO2 quanto quando usados de um terminal externo.
- **Arquitetura extensível (Tool Registry):** adicionar novas capacidades ao agente exige apenas registrar uma nova função Python decorada em `tools/`, ficando disponível simultaneamente para o grafo LangGraph e para o servidor MCP.

### Não-metas
- **Treinamento ou fine-tuning local de modelos:** o plugin consome modelos existentes via CLI, servidor local (Ollama/LM Studio) ou API em nuvem.
- **Edição binária de plugins `.esp`/`.esm` ou malhas `.nif`:** o agente gerencia a ordem de carga, ativação, instalação de pacotes, arquivos `.ini` e diagnóstico de logs, mas não edita registros internos de plugins Bethesda.
- **Execução automática do jogo ou de ferramentas pesadas sem ação do usuário:** nesta fase inicial, o agente diagnostica e prepara a lista de mods/plugins, mas não inicia o executável do jogo sozinho.

### Anti-metas (seria um fracasso mesmo funcionando)
- **Travar a interface gráfica (GUI) do MO2:** nenhuma chamada de rede, invocação de subprocesso CLI ou leitura de log grande pode bloquear a thread principal do Qt (`QApplication`).
- **Modificar mods ou ordem de carga na thread secundária:** toda mutação na API C++ (`IModList`, `IPluginList`, `IInstallationManager`) deve ocorrer exclusivamente na thread principal do Qt via fila de sinais/eventos para evitar condições de corrida ou corrupção de memória.
- **Alterar mods enquanto o VFS (`mo2-vfs`) estiver montado:** se houver um jogo ou ferramenta em execução com o VFS montado, todas as `write_tools` devem recusar a operação imediatamente.
- **Vazar chaves de API:** chaves de API nunca podem aparecer em `mo2.log`, em relatórios de erro ou em pacotes exportados `.mo2list`.

---

## 3. Arquitetura Geral e Empacotamento

### 3.1 Estrutura do Plugin (`deps/plugin_ai_assistant`)
O plugin é um módulo Python do MO2 (`IPluginTool`) construído pelo sistema CMake existente (`mo2_configure_python(ai_assistant MODULE)` em `deps/cmake_common/mo2_python.cmake`) e instalado em `install/bin/plugins/ai_assistant/`.

```text
deps/plugin_ai_assistant/
├── CMakeLists.txt
├── plugin-requirements.txt       # Dependências instaladas em lib/ via pip --target
└── src/
    ├── CMakeLists.txt            # mo2_configure_python(ai_assistant MODULE)
    ├── __init__.py               # site.addsitedir("lib") e createPlugin()
    ├── plugin.py                 # AiAssistantPlugin (mobase.IPluginTool)
    ├── config.py                 # Leitura/escrita de configurações e credenciais (0600)
    ├── agent/
    │   ├── graph.py              # StateGraph do LangGraph (agent, safe_tools, write_tools)
    │   ├── llm_factory.py        # Instanciação de modelos (OpenAI/Ollama/Gemini/Anthropic)
    │   ├── cli_runner.py         # Subprocesso assíncrono para claude, codex e agy (host)
    │   ├── mcp_server.py         # Servidor MCP local (127.0.0.1) com portão de aprovação
    │   └── worker.py             # QThread que coordena a execução fora da thread da GUI
    ├── tools/
    │   ├── registry.py           # Registro central (converte para @tool LangChain e MCP)
    │   ├── dispatcher.py         # Invocador seguro na thread principal do Qt (BlockingQueued)
    │   ├── mod_tools.py          # CRUD de mods, prioridades, instalação e conflitos
    │   ├── plugin_tools.py       # Ordem de carga (.esp/.esm/.esl) e INIs do perfil
    │   └── diagnostic_tools.py   # Leitura de logs (MO2, VFS, CrashLogger) e dono de arquivos
    └── ui/
        ├── dock_widget.py        # QDockWidget principal acoplado ao MO2
        ├── chat_panel.py         # Histórico de mensagens, streaming e atalhos rápidos
        ├── approval_card.py      # Widget de confirmação [Aprovar / Aprovar Tudo / Rejeitar]
        └── settings_dialog.py    # Configuração de API keys, URLs e detecção de CLIs
```

### 3.2 Empacotamento das Dependências Python (`plugin-requirements.txt`)
Durante o `./build.sh --only ai_assistant`, o `mo2_python_requirements` executa `pip install --target=lib -r plugin-requirements.txt`, embutindo:
- `langchain-core`
- `langgraph`
- `langgraph-checkpoint-sqlite`
- `langchain-openai` (atende OpenAI, Ollama, LM Studio, OpenRouter, DeepSeek e o endpoint compatível do Google Gemini)
- `langchain-anthropic` (atende a API da Anthropic)

No `src/__init__.py`, a pasta `lib/` é inserida no início do `sys.path` para garantir precedência consistente tanto na distrobox quanto no AppImage.

### 3.3 Modelo de Threads e Despacho Seguro no Qt
1. **Thread Principal do Qt (GUI Thread):**
   - Renderiza o `QDockWidget`, processa cliques nos botões de aprovação e executa um `MainThreadDispatcher` (`QObject` vivendo na `QApplication.instance().thread()`).
2. **Thread do Worker (`AgentWorker` — `QThread`):**
   - Executa o grafo LangGraph ou gerencia o subprocesso do CLI (`claude`, `codex`, `agy`).
3. **Thread do Servidor MCP (`MCPHttpServer` — `threading.Thread` daemon):**
   - Escuta conexões locais em `127.0.0.1:<porta_efêmera>`.
4. **Sincronização de Chamadas ao `mobase`:**
   - Quando o `AgentWorker` ou o `MCPHttpServer` precisa executar uma ferramenta que acessa ou modifica o estado do `mobase.IOrganizer`, ele envia um pedido ao `MainThreadDispatcher` via sinal Qt e aguarda um `threading.Event` até que o slot na thread principal conclua a chamada e preencha o resultado.
   - Se a ferramenta for uma `write_tool` e o modo atual for **Seguro**, o `MainThreadDispatcher` primeiro exibe o `ApprovalCard` no painel de chat e aguarda a decisão do usuário antes de executar a função `mobase`.

---

## 4. Catálogo de Ferramentas e Regras de Segurança

Todas as ferramentas são registradas no `ToolRegistry` com metadados explícitos: `name`, `description`, `args_schema` (Pydantic `BaseModel`) e `is_mutating: bool`.

### 4.1 Ferramentas de Leitura (`is_mutating = False` → `safe_tools`)
Executam sem pedir confirmação em qualquer modo:

1. `get_environment_info() -> dict`
   - Retorna: nome do jogo (`managedGame().gameName()`), `gameShortName()`, caminho da instalação do jogo, nome do perfil ativo, caminho da instância, caminho de `downloads/`, caminho de `mods/` e informações do prefixo Proton (`mobase.utils.steamApp`).
2. `list_mods(filter_query: str = "", active_only: bool = False, limit: int = 200) -> list[dict]`
   - Retorna lista de mods de `IModList.allModsByProfilePriority()`, filtrada por substring (case-insensitive), contendo: `name`, `active`, `priority`, `version`, `nexus_id` e indicadores de `is_separator` / `is_foreign`.
3. `get_mod_details(mod_name: str) -> dict`
   - Retorna metadados completos do mod (`IModInterface`), lista resumida de arquivos na raiz do mod e informações de conflitos de arquivos com outros mods ativos.
4. `list_load_order(active_only: bool = False) -> list[dict]`
   - Retorna a lista de plugins (`.esm`, `.esp`, `.esl`) de `IPluginList.pluginNames()`, ordenada por `loadOrder()`, com `name`, `active`, `load_order`, `is_master`, `is_light` e `origin_mod`.
5. `find_file_owner(virtual_subpath: str) -> dict`
   - Recebe um caminho relativo a `Data` (ex: `"SKSE/Plugins/po3_Tweaks.dll"` ou `"meshes/actors/character/..."`), resolve o caminho real via `IOrganizer.resolvePath()` e identifica a qual mod em `mods/` (ou `overwrite/` ou pasta base do jogo) o arquivo pertence, além de listar alternativas via `findFileInfos()`.
6. `list_downloads() -> list[dict]`
   - Lista os pacotes presentes em `downloads/` com nome do arquivo, tamanho, data de modificação e metadados do Nexus se disponíveis (`IDownloadManager`).
7. `read_diagnostic_logs(log_kind: str = "auto", tail_lines: int = 150) -> dict`
   - `log_kind` aceita `"mo2"`, `"crash"`, `"skse"`, `"vfs"` ou `"auto"`.
   - Localiza os arquivos de log mais recentes nas pastas de logs da instância (`logs/mo2.log`, `logs/crash.log`) e dentro do prefixo Proton do jogo (`Documents/My Games/<Jogo>/SKSE/`, procurando logs do CrashLogger, Trainwreck, NetScriptFramework e `skse64.log`).
   - Retorna apenas as últimas `tail_lines` linhas (máximo limitado a 500 linhas por chamada) e o caminho do arquivo lido.

### 4.2 Ferramentas de Escrita e CRUD (`is_mutating = True` → `write_tools`)
Exigem aprovação no **Modo Seguro** e são recusadas se o VFS estiver montado:

1. `set_mods_active(mod_names: list[str], active: bool) -> dict`
   - Ativa ou desativa um ou vários mods via `IModList.setActive(mod_names, active)`. Ignora separadores e mods `foreign` com aviso.
2. `set_mod_priority(mod_name: str, new_priority: int) -> dict`
   - Altera a prioridade de um mod via `IModList.setPriority(mod_name, new_priority)`.
3. `set_plugins_active(plugin_names: list[str], active: bool) -> dict`
   - Altera o estado de ativação de plugins `.esp`/`.esl` via `IPluginList.setState()`.
4. `install_mod_from_downloads(archive_filename: str, suggested_mod_name: str = "") -> dict`
   - Verifica se `archive_filename` é um nome simples existente dentro de `organizer.downloadsPath()` (sem `..` ou barras), e aciona `organizer.installMod()`.
5. `create_empty_mod(mod_name: str, as_separator: bool = False) -> dict`
   - Cria um novo mod vazio (ou separador, adicionando o sufixo `_separator`) via `organizer.createMod()` e atualiza a lista.
6. `remove_mod(mod_name: str) -> dict`
   - Remove a pasta do mod instalado dentro de `mods/` (após validar que é um mod real do usuário, não `foreign` nem `overwrite`) e atualiza a árvore do MO2 (`organizer.refresh()`). No card de aprovação, é destacada em vermelho como ação destrutiva.
7. `set_profile_ini_setting(ini_filename: str, section: str, key: str, value: str) -> dict`
   - Valida que `ini_filename` é um arquivo `.ini` existente dentro da pasta do perfil atual (`organizer.profilePath()`) e atualiza o par `section`/`key` preservando o restante do arquivo.

### 4.3 Proteções Obrigatórias nas Ferramentas
- **Bloqueio com VFS Montado:** Antes de executar qualquer ferramenta com `is_mutating = True`, o dispatcher consulta `/proc/mounts` (ou `--list-mounts` do `mo2-vfs`) para a instância atual. Se houver mount ativo, a ferramenta falha imediatamente retornando `"Blocked: Virtual filesystem is currently mounted (game or tool is running)."`.
- **Confinamento de Caminhos:** Qualquer leitura de log ou acesso a arquivo verifica `os.path.realpath()` para garantir que o arquivo reside estritamente dentro da pasta da instância, da pasta do jogo ou do prefixo Proton do jogo.

---

## 5. Arquitetura Dual de Execução (LangGraph + Ponte MCP para CLIs)

### 5.1 Motor Interno: LangChain + LangGraph (Provedores API e Ollama)
Para os provedores `ollama`, `gemini`, `openai` e `anthropic`, a execução ocorre dentro do `AgentWorker` usando um `StateGraph(MessagesState)`:

1. **Nós do Grafo:**
   - `agent`: invoca o modelo configurado com todas as ferramentas vinculadas (`.bind_tools(all_tools)`), precedido pelo `SystemMessage` dinâmico com o resumo da instância atual.
   - `safe_tools`: `ToolNode` contendo apenas as ferramentas com `is_mutating == False`.
   - `write_tools`: `ToolNode` contendo as ferramentas com `is_mutating == True`.
2. **Roteamento Condicional (`route_after_agent`):**
   - Se a última `AIMessage` não tiver `tool_calls` → `END`.
   - Se tiver `tool_calls` e qualquer uma delas pertencer a `write_tools` → roteia para `write_tools`.
   - Caso contrário → roteia para `safe_tools`.
3. **Checkpoint e Interrupção:**
   - O grafo é compilado com `SqliteSaver` apontando para `~/.local/share/ModOrganizer/plugins/data/ai_assistant/checkpoints.sqlite`.
   - Quando uma chamada atinge `write_tools` e o plugin está em **Modo Seguro**, o próprio wrapper da ferramenta no `MainThreadDispatcher` suspende a execução aguardando o sinal do `ApprovalCard` na GUI (ou `interrupt()` do LangGraph), garantindo que o mesmo mecanismo de aprovação proteja tanto chamadas individuais quanto lotes de ferramentas.

### 5.2 Ponte MCP Local e Runner de Agentes CLI (`claude`, `codex`, `agy`)
Para permitir o uso direto do **Claude Code**, **Codex** e **Antigravity CLI**:

1. **Servidor MCP Local (`mcp_server.py`):**
   - Inicia junto com o plugin em uma porta local disponível (`127.0.0.1:<port>`), protegido por um token aleatório gerado a cada sessão (`X-MO2-Token` / query param).
   - Implementa os métodos padrão do protocolo MCP (JSON-RPC 2.0):
     - `initialize`
     - `tools/list` (exportando os schemas JSON gerados pelo `ToolRegistry`)
     - `tools/call` (encaminhando para o `MainThreadDispatcher`, que aplica automaticamente o portão de aprovação do **Modo Seguro** na GUI do MO2 antes de retornar o resultado ao CLI).
   - Também inclui um pequeno script ponte stdio (`mcp_stdio_bridge.py`) que repassa stdin/stdout para `http://127.0.0.1:<port>/mcp`, pois alguns CLIs preferem servidores MCP via `stdio` em vez de HTTP.
2. **Execução dos CLIs pelo Painel de Chat (`cli_runner.py`):**
   - Quando o usuário seleciona `Claude Code (CLI)`, `Codex (CLI)` ou `Antigravity (CLI)` no painel do MO2 e envia uma mensagem:
     - Se o MO2 estiver rodando dentro de container (`/.dockerenv` ou `/run/.containerenv` presente), o comando é prefixado com `distrobox-host-exec` para invocar o binário instalado no host e acessar as credenciais do usuário no host.
     - O runner passa ao CLI um arquivo temporário de configuração MCP apontando para o `mcp_stdio_bridge.py` da sessão atual, mais o prompt do sistema com o contexto do MO2 e a mensagem do usuário.
     - A saída do processo é lida linha a linha de forma assíncrona e exibida em tempo real no painel de chat.

---

## 6. Interface do Usuário e Configurações

### 6.1 Painel `QDockWidget` (`dock_widget.py` e `chat_panel.py`)
- Adicionado à janela principal no `onUserInterfaceInitialized` do MO2 na área direita (`Qt.DockWidgetArea.RightDockWidgetArea`), podendo ser movido ou transformado em aba pelo usuário.
- Uma ação no menu de ferramentas (**Tools → AI Assistant**) permite reabrir ou focar o painel caso o usuário o tenha fechado.
- **Componentes do Painel:**
  1. **Barra de Controle Superior:**
     - Seletor de Backend (`Claude Code CLI`, `Codex CLI`, `Antigravity CLI`, `Ollama Local`, `Gemini API`, `OpenAI / Compatível`, `Anthropic API`).
     - Seletor de Modo (`🔒 Seguro` vs `⚡ Autônomo`).
     - Botão `🧹 Limpar Conversa` e botão `⚙️ Configurações`.
  2. **Visualizador de Mensagens:**
     - Exibe mensagens do usuário, respostas em Markdown do agente e logs compactos das ferramentas executadas.
  3. **Card de Aprovação Inline (`ApprovalCard`):**
     - Exibido quando uma ferramenta `is_mutating` é solicitada em Modo Seguro.
     - Mostra o nome da ferramenta, descrição clara dos parâmetros e botões **[Aprovar]**, **[Aprovar Tudo na Sessão]** e **[Rejeitar]**.
  4. **Barra de Atalhos Rápidos e Entrada:**
     - Botões de 1 clique: **Diagnosticar Crash**, **Analisar Conflitos** e **Revisar Ordem de Carga**.
     - Campo de texto (`QPlainTextEdit`) com envio por `Enter` (`Shift+Enter` para nova linha) e botão **Enviar / Parar**.

### 6.2 Armazenamento de Configurações (`config.py`)
- Salvo em `~/.local/share/ModOrganizer/plugins/data/ai_assistant/config.json` com permissão `0600` (`os.chmod(path, 0o600)`).
- Armazena: backend selecionado, modo de permissão padrão, modelo escolhido para cada provedor, URL do Ollama (`http://localhost:11434/v1`), URL customizada compatível com OpenAI e chaves de API.
- O diálogo de configurações inclui um detector automático que verifica se `claude`, `codex`, `agy` e o servidor `ollama` estão acessíveis a partir do ambiente atual.

---

## 7. Estratégia de Testes

1. **Testes Unitários Python (`deps/plugin_ai_assistant/tests/`):**
   - `test_tools.py`: usa um `MockOrganizer` (`IModList`, `IPluginList`, pastas temporárias de perfil e logs) para testar todas as 7 ferramentas de leitura e 7 ferramentas de escrita, incluindo tentativa de path traversal e bloqueio quando há mount FUSE simulado.
   - `test_dispatcher_approval.py`: verifica que, no Modo Seguro, uma `write_tool` aguarda a aprovação e que, ao ser rejeitada, não altera o `MockOrganizer` e retorna mensagem de rejeição ao agente; no Modo Autônomo, executa diretamente.
   - `test_mcp_server.py`: inicia o servidor MCP local em porta efêmera, testa `initialize`, `tools/list`, `tools/call` e recusa de requisições sem o token de sessão.
   - `test_langgraph_flow.py`: executa o `StateGraph` com um modelo determinístico de teste (`FakeToolChatModel`) exercitando o ciclo `agent → safe_tools → agent → write_tools → agent → END`.
2. **Integração no `./build.sh`:**
   - Novo alvo `ai_assistant` no `build.sh`; com `--tests`, executa a suíte `pytest` / `unittest` do plugin e garante que o plugin é carregado sem erros pelo runner Python do MO2.
