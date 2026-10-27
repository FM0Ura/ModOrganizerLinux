# Compatibilidade com plugins Python de Windows — Design

- **Data:** 2026-10-27
- **Status:** em revisão
- **Depende de:** pasta de plugins do usuário (PR #12, v2.5.2.2); `winreg` vazio (PR #13, v2.5.2.3).
- **Branch:** `compat-plugins-windows`, a partir de `main` (`44ca78a`).

## 1. Objetivo e porquê

O usuário quer que um plugin Python feito para o MO2 do Windows, instalado em
`~/.local/share/ModOrganizer/plugins`, funcione no MO2 Linux **sem precisar ser editado**
("nossa aplicação deve ser totalmente compatível com todos os plugins do Windows que for por
nele"). Hoje o único plugin desse tipo que ele usa é o **RootBuilder** (Kezyma), que é o caso
de teste.

Desde a v2.5.2.3, o RootBuilder carrega. Mas um teste do núcleo dele (build, sync e clear),
feito com um organizer simulado e as configurações padrão (`hash`, `backup`, `cache` e
`autobuild` ligados), mostrou que hoje ele **estraga a pasta do jogo**:

| # | Problema | Efeito observado |
|---|---|---|
| A | `os.chmod(f, stat.S_IWRITE)`, que no Windows só tira o "somente leitura", aqui grava o modo `0200` | `Game.exe` ficou `--w-------` (ilegível); o hash roda em **todos** os arquivos da raiz do jogo |
| B | `os.O_BINARY` não existe no Linux | `hashFile` quebra (`AttributeError` nas threads), o cache fica vazio e o backup não é feito; o `Vanilla.DLL` original foi apagado e se perdeu |
| C | Os dados do build usam o caminho relativo em minúsculas, e o clear apaga `<jogo>/<minúsculas>` | O clear deixa no jogo os arquivos de mod com maiúsculas no nome e não restaura os originais |
| D | `pluginDataPath()` aponta para `<bin>/plugins/data`, que na AppImage é somente leitura | Dados do build, cache e backup não são gravados, então sync e clear não fazem nada. O basic_games do Baldur's Gate 3 também grava ali (`bg3_utils.plugin_data_path`) |

Nenhum jogo ou mod do usuário foi afetado: em 2026-10-27, nenhum arquivo dos 5 jogos nem dos
mods estava com modo `0200`, e não havia nenhum mod com pasta `Root/`.

## 2. Metas, não-metas e anti-metas

**Metas**
- Com as configurações padrão, o RootBuilder sem modificações faz build, sync e clear
  corretamente na AppImage. Corretamente significa que, depois do clear, a pasta do jogo fica
  igual ao que era antes do build, com os mesmos arquivos, conteúdo e permissões.
- A camada vale para **qualquer** plugin Python da pasta do usuário, não só para o RootBuilder.

**Não-metas**
- Plugins que usam código nativo do Windows: `ctypes.windll`, `.pyd` e `.dll` compilados para
  Windows, `win32api`/pywin32 e COM. Não têm como funcionar; ficam documentados como limite.
- Plugins C++ (`.dll`) de Windows.
- Variáveis de ambiente do Windows (`LOCALAPPDATA`, `APPDATA`). Definir essas variáveis no
  processo vazaria para os jogos e ferramentas iniciados pelo MO2.
- Padrões de `glob` sem diferenciar maiúsculas (por exemplo, uma exclusão `data` casando com
  `Data`). Fica para quando algum plugin precisar.
- Modo usvfs do RootBuilder (`mappings()`). O VFS do Linux é outro; não faz parte desta fase.

**Anti-metas** (seria um fracasso mesmo funcionando)
- Mudar o comportamento dos plugins que vêm com o app (basic_games, tool_configurator,
  preview_dds, etc.) ou do próprio MO2. Decisão do usuário, 2026-10-27: a regra das
  maiúsculas vale **só para os plugins da pasta do usuário**.
- Fazer um plugin achar o arquivo errado quando existem dois nomes que só diferem em
  maiúsculas.

## 3. O que muda

### 3.1 Pasta de dados dos plugins gravável (problema D)

- No Linux, `OrganizerCore::pluginDataPath()` passa a ser `userPluginsPath() + "/data"`, ou
  seja, `~/.local/share/ModOrganizer/plugins/data`, criada quando não existe. No Windows nada
  muda. A função fica em `pluginpaths.{h,cpp}` (`userPluginDataPath()`), com teste em
  `tests/test_pluginpaths.cpp`.
- Os arquivos que o app traz em `<bin>/plugins/data` continuam onde estão e continuam
  acessíveis:
  - o pacote `DDS/` (preview_dds) e o `pyCfgDialog.py` (tool_configurator) são importados
    pelo `sys.path`. O proxy (`proxypython.cpp`, ramo Linux) passa a pôr no `sys.path` as duas
    pastas, a do usuário primeiro e depois a do app;
  - o tool_configurator lê `pluginDataPath() + "/settings.json"`. No Linux, se o arquivo não
    existe na pasta do usuário, ele lê o do app (`deps/tool_configurator`, ramo
    `sys.platform`, como as outras correções Linux desse plugin).
- A pasta `data` dentro da pasta de plugins do usuário é o mesmo layout do Windows
  (`plugins/data`). O carregador já ignora pastas sem plugin. A conferir na implementação: o
  `mergePluginEntries` deve ignorar a entrada `data` do usuário, para que ela não esconda a do
  app.

### 3.2 Camada de compatibilidade Python (problemas A, B e C)

Um módulo novo, `deps/plugin_python/src/libs/mo2_windows_compat.py`, é instalado no Linux em
`plugin_python/libs`, ao lado do `winreg.py`. O runner o importa e o instala uma vez, logo
depois de importar o `mobase` (`pythonrunner.cpp`, ramo Linux).

**Quais plugins recebem a camada.** Recebem os plugins que não vêm com o app. Ao carregar um
plugin (`ProxyPython::load`), se o caminho dele não está dentro da pasta de plugins do app,
o runner registra esse caminho como "raiz Windows" na camada: a pasta, para plugins-módulo,
ou o arquivo `.py`, para os de um arquivo só. Na prática, são os plugins da pasta do usuário.

**Como a camada sabe quem chamou.** O código chamador é de um plugin Windows quando o quadro
mais interno da pilha (`sys._getframe`) que não é da biblioteca padrão nem da camada tem o
arquivo dentro de uma raiz Windows. Isso cobre chamadas indiretas, como `shutil.copy2`
chamado pelo RootBuilder, e deixa de fora o código do app chamado de volta de dentro de um
plugin Windows (callbacks do `mobase`, laços do Qt). Decisão da revisão final, 2026-10-27: a
regra anterior, "algum quadro da pilha", aplicava a camada a esse código, contra a decisão
do usuário. Limite conhecido: um plugin que entrega o trabalho direto à biblioteca padrão
(`Thread(target=os.remove)`) não recebe a camada. A pilha só é examinada
quando a regra pode mudar alguma coisa (ver abaixo), nunca no caminho normal.

**B — flags do Windows no `os`.** `os.O_BINARY`, `os.O_TEXT`, `os.O_NOINHERIT`,
`os.O_SHORT_LIVED`, `os.O_TEMPORARY`, `os.O_RANDOM` e `os.O_SEQUENTIAL` passam a existir com
valor `0`, para todo o processo. Um atributo não tem como valer só para alguns plugins, e o
valor `0` não muda nenhum `open`. Limitação conhecida: o `O_TEMPORARY` não apaga o arquivo ao
fechar.

**A — `os.chmod` com o significado do Windows.** No Windows, `chmod` só olha o bit
`S_IWRITE`: com ele, tira o somente leitura; sem ele, põe. Quando a chamada vem de um plugin
Windows **e** o modo pedido deixaria o arquivo ilegível para o dono (`mode & 0o400 == 0`, que
inclui `stat.S_IWRITE` sozinho) ou só tem os bits `S_IREAD`/`S_IWRITE` (`0o600`, `0o400`,
idiomas do Windows; decisão da revisão final), a camada aplica esse significado:
- com `S_IWRITE`: modo atual `| 0o200`;
- sem: modo atual `& ~0o222`.

Qualquer outro modo segue normal, porque um modo legível é um pedido Linux legítimo.

**C — caminhos sem diferenciar maiúsculas, como o Wine.** Isso vale só para chamadas de
plugins Windows. A resolução de um caminho percorre os componentes a partir da raiz:
- o componente existe com o nome exato: é usado;
- senão, e há **exatamente um** item na pasta com o mesmo nome ignorando maiúsculas
  (`casefold`): esse item é usado;
- senão (nenhum, ou mais de um candidato): o resto do caminho fica como foi pedido.

Duas formas de aplicar:
- **Leitura e remoção** (`os.stat`, `os.lstat`, `open`/`io.open`/`os.open` sem `O_CREAT`,
  `os.remove`/`os.unlink`, `os.rmdir`, `os.listdir`, `os.scandir`, `os.chmod`, `os.utime`,
  `os.access`, `os.truncate`, `os.readlink`, `os.chown`/`os.lchown`, as funções de xattr que o
  `shutil.copy2` usa para copiar os rótulos do SELinux,
  `os.rename`/`os.replace` na origem, `os.link` na origem): chama o original e, só se der
  `FileNotFoundError` ou `NotADirectoryError` **e** a chamada vier de um plugin Windows,
  resolve o caminho e tenta de novo, se ele mudou. `os.path.exists`, `isfile`, `isdir`,
  `getmtime`, `getsize` e o `pathlib` passam por `os.stat` e são cobertos assim.
- **Criação** (`os.mkdir` e o `os.makedirs` por meio dele, `open`/`os.open` com escrita ou
  `O_CREAT`, destino de `os.rename`/`os.replace`/`os.link`/`os.symlink`): se a chamada vem de
  um plugin Windows, resolve antes de chamar. Assim um mod com `bin/x64/a.dll` vai para a
  `Bin/x64` existente, em vez de criar uma segunda pasta `bin`.

Também nas chamadas de plugins Windows, `\` nos caminhos vira `/` antes da resolução, porque o
Windows aceita os dois. Isso vale também para padrões passados a `glob.glob`/`glob.iglob`.
`glob.escape` não produz `\`, então os padrões escapados não são afetados.

### 3.3 Documentação

`deps/README.md` (entrada do `plugin_python` e do `tool_configurator`) e a documentação do
usuário (`docs/user/*`, seção de plugins do usuário) passam a descrever:
- o que a camada cobre, para quais plugins vale e o limite de código nativo;
- a nova pasta de dados.

## 4. Testes

- **pytest da camada** (`tests/python/test_windows_compat.py`): um "plugin Windows" de teste
  num diretório registrado como raiz e um chamador de fora da raiz fazem as mesmas operações
  numa árvore com `Bin/Vanilla.DLL`. Os casos:
  - de dentro da raiz: ler, apagar e sobrescrever `bin/vanilla.dll` acerta o arquivo;
    `makedirs('bin/x64')` usa a `Bin` existente; `chmod(S_IWRITE)` mantém o arquivo legível;
    `\` vira `/`; com `a.txt` e `A.txt`, `a.TXT` não é resolvido;
  - de fora da raiz: todo o comportamento é o padrão do Linux;
  - os flags `O_*` existem e valem 0.
- **C++**: `userPluginDataPath()` em `tests/test_pluginpaths.cpp`.
- **Verificação com o RootBuilder real** (manual e fora do repositório, porque o código é de
  terceiros): o harness desta sessão (organizer simulado, jogo e mod de teste) roda com a
  camada instalada e o RootBuilder registrado como raiz. Com `hash` ligado e desligado,
  depois de build, sync e clear, a pasta do jogo deve ficar idêntica à original (conteúdo e
  permissões) e o sync deve levar os arquivos alterados de volta ao mod e ao overwrite.
- **Sem regressão:** suíte completa (`./build.sh --tests`) e AppImage com `check-appdir`.
- **No app (usuário):** com a AppImage nova, um mod de teste com `Root/`. Build, iniciar e
  fechar o jogo (autobuild) e conferir a pasta do jogo.

## 5. Decisões do Claude (a conferir)

1. Os flags `O_*` valem para todo o processo, não só para os plugins do usuário, porque um
   atributo de módulo não tem como ser escopado e o valor `0` não muda nenhum comportamento.
2. A regra do `chmod` só age quando o modo deixaria o arquivo ilegível para o dono. Um plugin
   Windows que peça `0o755` recebe `0o755`, como no Linux.
3. Quando há mais de um candidato com o mesmo nome em maiúsculas diferentes, nada é resolvido
   (a chamada falha como hoje), em vez de escolher um.
4. Sem cache das listagens de pasta na resolução: ela só roda quando o caminho exato não
   existe, e só para os plugins do usuário.
5. Os dados que o app traz (`<bin>/plugins/data`) não são copiados para a pasta do usuário;
   são lidos de onde estão (`sys.path` com as duas pastas e fallback do `settings.json`).
6. Fica de fora a migração de dados de `<bin>/plugins/data` gravados por versões anteriores:
   na AppImage nada pôde ser gravado ali.
