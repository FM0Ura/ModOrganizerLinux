# Fase 2 — VFS FUSE (`mo2-vfs`) — Design

- **Data:** 2026-10-15
- **Status:** aguardando revisão
- **Spec geral:** [`2026-10-14-mo2-linux-port-design.md`](2026-10-14-mo2-linux-port-design.md) (§3.1 resume esta fase; este documento a substitui onde divergirem)
- **Branches:** `fase-2-vfs-fuse` no fork `modorganizer` e neste repositório (o CI usa o branch do fork com o mesmo nome)

## 1. Objetivo

Substituir o USVFS no Linux por um sistema de arquivos FUSE próprio, escrito em Rust (`mo2-vfs`). Enquanto um programa iniciado pelo MO2 roda, as pastas do jogo mostram a visão mesclada dos mods ativos, sem alterar a instalação do jogo.

Nesta fase o "Executar" volta a funcionar **só para programas Linux nativos** (ELF ou script com `#!`). A execução de `.exe` via Proton é da Fase 3, que reaproveita todo o ciclo de vida definido aqui e só troca o lançador.

### 1.1 Decisões desta fase

| Tema | Decisão |
|---|---|
| Arquitetura | **Processo auxiliar** `mo2-vfs` (binário Rust) conversando com o MO2 pelo stdin/stdout do processo. Substitui a ponte em processo (`cxx`/`cbindgen` + Corrosion) prevista na spec geral. |
| Motivo | Se o MO2 travar ou fechar, o jogo continua com acesso aos arquivos; os threads FUSE ficam fora do processo Qt (sem deadlock quando o MO2 acessa uma pasta montada); o build Windows e o CMake do fork não ganham Rust. |
| Demonstração na fase | "Executar" de programas Linux nativos com o VFS montado; `.exe` continua com o aviso da Fase 3. |
| Arquivos originais do jogo | Nunca apagados nem renomeados pelo VFS: exclusão só esconde até desmontar; renomeação copia para a camada de criação. Editar o conteúdo continua alterando o original (como o USVFS). |
| Arquivos de mods | Edição, exclusão e renomeação agem no arquivo real do mod (como o USVFS). |
| Pendências da Fase 1 | Todas as marcadas para a Fase 2 em `STATE.md` entram nesta fase (§9). |

### 1.2 Resultado do spike de `pressure-vessel` (2026-10-15)

Risco que a spec geral deixava para a Fase 3, validado no Bazzite com `fuse-overlayfs` como FUSE mínimo (código descartado):

| Pergunta | Resultado |
|---|---|
| Mount FUSE feito pelo usuário no host aparece dentro do SteamLinuxRuntime_4 (`pressure-vessel`)? | Sim, sem `allow_other` (mesmo uid) |
| Mount feito depois de o container subir aparece lá dentro? | Sim (propagação `slave`) |
| Wine (Proton Experimental via `umu-run`) lista, lê (inclusive `A.TXT` para `a.txt`) e escreve pelo mount? | Sim |
| Cache de diretórios do Wine após atualização a quente | Não testado — continua risco da Fase 3 |

Consequência: o desenho "o MO2 monta no host, depois executa" vale também para a Fase 3; não é preciso montar dentro do container.

### 1.3 Fora de escopo

- Execução via Wine/Proton, descoberta de jogos e prefixos (Fase 3).
- `updateForcedLibraries` (injeção de DLL do USVFS): continua só registrando no log.
- `executableBlacklist`, `spawnDelay`, `crashDumpsPath`, `coreDumpType` do `updateParams`: são do USVFS e são ignorados.
- Windows: continua usando o USVFS sem mudanças.

## 2. Arquitetura

### 2.1 Onde fica o código

| Lugar | Conteúdo |
|---|---|
| `ModOrganizerLinux/mo2-vfs/` | Crate Rust (biblioteca + binário). Começa no superprojeto; pode virar repositório próprio depois. |
| `ModOrganizerLinux/build.sh` | Novo alvo `mo2-vfs` (`cargo build --release` + `cargo test`), instalado em `install/bin/mo2-vfs`. Roda antes do `modorganizer`. |
| `modorganizer/src/linux/vfsconnector_linux.cpp` | `LinuxVfsConnector`, substitui o `NullVfsConnector`. |
| `modorganizer/src/linux/vfsclient_linux.{h,cpp}` | Cliente do protocolo (§5): inicia o `mo2-vfs` com `QProcess`, envia mensagens pelo stdin, lê as respostas do stdout. |
| `modorganizer/src/linux/spawn_linux.cpp`, `processrunner_linux.cpp` | Execução de programas Linux nativos (§6). |

O CMake do fork não compila Rust: o `mo2-vfs` é um executável à parte, encontrado em `<pasta do ModOrganizer>/mo2-vfs` (mesmo `bin/` da instalação) ou pela variável `MO2_VFS_BINARY` (testes).

### 2.2 Módulos do crate

| Módulo | Responsabilidade | Depende de |
|---|---|---|
| `mapping` | Recebe a lista de `Mapping{source, destination, is_directory, create_target}`, normaliza caminhos e agrupa por **raiz de montagem** (§3.1). | — |
| `tree` | Árvore virtual construída a partir das camadas; lookup sem diferenciar maiúsculas/minúsculas; máscaras de exclusão. Sem FUSE: testável isoladamente. | `mapping` |
| `fs` | Implementação de `fuser::Filesystem`: inode → nó → arquivo real; operações da §4. | `tree`, `fuser` |
| `session` | Um mount por raiz: abre o fd da pasta original **antes** de montar, monta, desmonta, arquivo de estado (§5.3). | `fs` |
| `ipc` | Protocolo JSON com delimitação por linha sobre stdin/stdout (§5). | `session` |
| `bin/mo2-vfs.rs` | Argumentos, log, sinais, laço do `ipc`. | todos |

Dependências externas previstas: `fuser` 0.18 (com passthrough), `parking_lot`, `serde`/`serde_json`, `nix` ou `rustix`, `tracing`/`tracing-appender`. Testes: `tempfile`. Benchmarks: `criterion`.

## 3. Modelo de dados

### 3.1 Raízes de montagem

Os destinos dos mappings são agrupados em raízes: cada destino distinto vira uma raiz, e um destino dentro de outro entra na raiz externa (o caminho interno vira um ponto da árvore, não um segundo mount). Exemplos típicos do `game_bethesda`: `<jogo>/Data`, `My Games/<Jogo>` (saves locais), mais os destinos de `IPluginFileMapper`.

Uma raiz cujo diretório não existe é criada vazia antes de montar (a camada original fica vazia). Ao desmontar, a pasta criada é removida se continuar vazia.

### 3.2 Camadas

Por raiz:

- **Camada 0 — original:** o conteúdo da pasta antes do mount, acessado por um fd aberto antes de montar (`openat` relativo a ele).
- **Camadas 1…n — mappings,** na ordem em que o MO2 os envia: **o último vence** (como o USVFS). O `OrganizerCore::fileMapping` envia os mods em ordem crescente de prioridade e `overwrite/` por último.
- Mapping de **diretório** sobrepõe a subárvore inteira, mesclando. Mapping de **arquivo** sobrepõe só aquele caminho.
- **Camada de criação:** a camada cujo mapping tem `createTarget = true` (o `overwrite/`, ou o overwrite customizado do executável). Se mais de uma tiver a flag, vale a de maior prioridade; se nenhuma tiver, criações falham com `EROFS` e um aviso no log.

Exemplo:

```
overwrite/       (camada 3, criação)
Mod B/           textures/rock.dds
Mod A/           textures/rock.dds   meshes/tree.nif
original Data/   Skyrim.esm          textures/rock.dds
→ visão: Skyrim.esm (original), meshes/tree.nif (Mod A), textures/rock.dds (Mod B)
```

### 3.3 Nomes

- Chave de busca: nome após *case folding* Unicode simples. `A.TXT` e `a.txt` resolvem para o mesmo nó.
- A listagem mostra a grafia da camada vencedora.
- Diretório e arquivo com o mesmo nome em camadas diferentes: vence a camada de maior prioridade.
- Sufixos de `skipFileSuffixes` (ex.: `.mohidden`) e nomes de `skipDirectories` ficam fora da árvore (vêm do `updateParams`).
- Inodes estáveis por caminho normalizado dentro de um mount, inclusive depois de uma atualização da árvore.

### 3.4 Atualização da árvore

A árvore fica atrás de um `RwLock`. Ela é varrida a partir dos diretórios reais das camadas na montagem e em cada `update`; no `update`, a árvore nova é construída fora do lock e só a troca acontece com o lock de escrita (efeito de troca atômica para quem lê). Alterações feitas pelo próprio VFS (criação, exclusão, renomeação) alteram a árvore corrente no lugar, com o lock de escrita, sem varrer de novo.

As máscaras (§4) valem até desmontar e sobrevivem a `update`.

## 4. Operações

| Operação | Comportamento |
|---|---|
| `lookup`, `getattr`, `readdir` | Pela árvore; atributos do arquivo real (`fstatat`). |
| `open`, `read` | Abre o arquivo real da camada vencedora. Tenta **FUSE passthrough** (kernel ≥ 6.9); como o kernel exige `CAP_SYS_ADMIN` para registrar o arquivo, sem privilégio o caminho normal é `pread` no próprio processo. A primeira recusa desliga a tentativa até desmontar. |
| `write`, `truncate`, `chmod`, `utimens`, `fsync` | No arquivo real da camada vencedora (mod **ou** original). |
| `create`, `mkdir` | Na camada de criação, no mesmo caminho relativo; diretórios-pai que faltarem são criados nela. |
| `unlink`, `rmdir` — camada de mod | Apaga o arquivo real e **mascara** o caminho, para que uma versão de camada inferior não reapareça na sessão. |
| `unlink`, `rmdir` — camada original | Só mascara. O arquivo do jogo não é apagado. |
| `rename` — origem em camada de mod ou de criação | Move dentro da própria camada (`renameat`), criando os pais nela se preciso. Se o destino existe noutra camada, ele fica mascarado. |
| `rename` — arquivo da camada original | Copia para a camada de criação no caminho de destino (conteúdo, modo e datas) e mascara a origem. |
| `rename` — diretório presente em mais de uma camada, diretório da camada original, ou arquivo vindo de mapping de arquivo | `EXDEV`: o programa copia e apaga, como faria entre discos (e a exclusão segue as regras acima). |
| `rename` com `RENAME_EXCHANGE`/`RENAME_WHITEOUT` | `EINVAL`. |
| `symlink`, `link`, `mknod`, xattrs | `EPERM`/`ENOTSUP` (não usados pelos jogos). Links simbólicos dentro das camadas são seguidos, como no USVFS: a visão não tem links; um link quebrado fica fora da árvore com aviso no log. |
| `statfs` | Da camada de criação. |

O caso mais comum — o jogo grava `save.tmp` e renomeia para `save.ess` — fica inteiro na camada de criação.

**Cache do kernel:** TTL longo para entradas e atributos (padrão 60 s). A cada `update`, o `mo2-vfs` invalida os nós que mudaram (`notify_inval_entry`/`notify_inval_inode`).

## 5. Comunicação com o MO2

### 5.1 Processo

- O `LinuxVfsConnector` inicia `mo2-vfs --log <instância>/logs/mo2-vfs.log --log-level <nível>` com `QProcess`. O canal é o stdin (pedidos) e o stdout (respostas) do processo; o log vai só para o arquivo e o stderr.
- O `mo2-vfs` desmonta **sozinho** quando o último grupo de processos informado em `watch` termina, esteja o MO2 vivo ou não, e avisa com o evento `unmounted`.
- Se o stdin fechar (o MO2 saiu ou travou): sem grupo vivo, desmonta e sai; com grupo vivo, mantém os mounts até o último grupo terminar, desmonta e sai.

### 5.2 Mensagens (JSON, uma por linha)

| MO2 → `mo2-vfs` | Conteúdo |
|---|---|
| `mount` | `mappings`, `skip_file_suffixes`, `skip_directories` |
| `update` | mesmos campos; troca a árvore sem desmontar |
| `watch` | `pgid` do grupo de processos do programa iniciado |
| `unmount` | — (desmonta já; usado quando o programa nem chegou a iniciar) |
| `status` | — |

| `mo2-vfs` → MO2 | Conteúdo |
|---|---|
| `ready` | raízes montadas |
| `error` | `code` (`fuse_unavailable`, `mount_busy`, `bad_mapping`, `io`), `message`, `path` |
| `unmounted` | — (resposta a `unmount`, ou evento espontâneo da desmontagem automática) |
| `status` | raízes, contagem de nós, máscaras, grupos acompanhados |
| `hello` | versão; enviado uma vez ao iniciar |

Cada pedido recebe exatamente uma resposta (`watch` responde `{"event":"ok"}`); `mount`/`update` respondem só quando todos os mounts estão prontos. O único evento espontâneo é o `unmounted` da desmontagem automática.

### 5.3 Arquivo de estado e limpeza

`$XDG_RUNTIME_DIR/mo2-vfs/state-<pid>.json` lista as raízes montadas. Na inicialização, o `LinuxVfsConnector` roda `mo2-vfs --cleanup`, que procura arquivos de estado cujo pid não existe mais e, para cada raiz ainda montada com tipo `fuse.mo2-vfs` (`/proc/self/mountinfo`), roda `fusermount3 -u` (e `-uz` se falhar). Raízes que não forem desmontadas são informadas ao usuário.

### 5.4 Desmontagem

`unmount` tenta `fusermount3 -u` por até 5 s (repetindo em `EBUSY`); se não conseguir, faz `-uz` (desmonte preguiçoso) e registra no log. Um grupo informado em `watch` está vivo enquanto existir em `/proc` algum processo com esse `pgrp` que não seja zumbi (verificado a cada 500 ms); zumbis não seguram o mount.

## 6. Integração no MO2

### 6.1 `LinuxVfsConnector`

- `updateParams`: guarda os `skip*` e o nível de log; o resto é ignorado (§1.3).
- `updateMapping`: converte para `/` (como o `NullVfsConnector` faz hoje) e **só guarda** os mappings — ele também é chamado ao trocar de perfil, quando nada deve ser montado. Se já houver mount ativo, envia `update`.
- Novos métodos na interface `VfsConnector`, com implementação vazia por padrão (o Windows não muda):
  - `activate()`: inicia o `mo2-vfs` se preciso e envia `mount` (ou `update`, se já ativo) com os últimos mappings. Timeout de 30 s; `error` vira `std::runtime_error` com a mensagem do auxiliar, que o `OrganizerCore` mostra e que cancela a execução.
  - `watchProcessGroup(pgid)`: envia `watch`.
  - `releaseIfIdle()`: envia `unmount` se nenhum grupo foi informado (o programa não iniciou).
  - `isActive()` e `waitInactive(timeout)`.
  - `setDeactivatedCallback(f)`: chamado (na thread da interface) quando chega `unmounted`.
- `name()`: `"mo2-vfs"`.
- **O mount vale para o sistema inteiro, inclusive para o próprio MO2.** Enquanto `isActive()`, o `OrganizerCore::refreshDirectoryStructure` não varre: marca a atualização como pendente e a executa no callback de desativação. Sem isso, o MO2 leria a `Data` montada e confundiria arquivos de mods com os originais do jogo.

### 6.2 Execução de programas Linux

- `spawn::startBinary`: se o alvo é ELF executável ou script com `#!` e permissão de execução, inicia com `posix_spawn` num grupo de processos novo (`POSIX_SPAWN_SETPGROUP`, informado ao `mo2-vfs` com `watch`), no diretório de trabalho do executável e com o ambiente do MO2. `.exe`, `.bat` e o resto continuam com a mensagem da Fase 3.
- `ProcessRunner::run` no Linux deixa de recusar tudo: segue o fluxo comum (salvar perfil, `beforeRun` — que agora chama `activate()` —, iniciar, `watchProcessGroup`, esperar, `afterRun`) para os programas aceitos acima. Se o programa não iniciar, `releaseIfIdle()`.
- `afterRun` chama `waitInactive(5 s)` antes de atualizar a árvore de dados.
- A espera usa o acompanhamento de árvore de processos que já existe em `processrunner_linux.cpp`.
- "Executar" de novo com um programa já rodando reaproveita o mount e envia `update`, como no Windows.

## 7. Erros

| Situação | Resultado |
|---|---|
| `/dev/fuse` ou `fusermount3` ausente | `error fuse_unavailable`; programa não inicia; a mensagem diz o que falta e, dentro de distrobox/container, que `/dev/fuse` precisa estar disponível nele. |
| Raiz já montada (por outro MO2 ou resto de falha) | Limpeza da §5.3; se continuar ocupada, `error mount_busy`. |
| Mapping com origem inexistente | Camada ignorada com aviso no log (o MO2 pode mandar mods removidos entre o refresh e a execução). |
| Erro de E/S num arquivo real | Repassado ao programa com o mesmo `errno`. |
| `mo2-vfs` morre durante a execução | O MO2 detecta o fim do `QProcess`, registra no log e avisa o usuário ao fim da execução; limpeza da §5.3 na próxima abertura. |
| `panic` no `mo2-vfs` | Cada operação FUSE roda sob `catch_unwind`: o `panic` vira `EIO` para aquela operação e um erro no log; o mount continua. |

## 8. Testes e desempenho

| Nível | Cobre | Onde roda |
|---|---|---|
| Rust, unidade (`tree`, `mapping`) | Prioridade, maiúsculas/minúsculas, máscaras, regras de criação e renomeação, `skip*`, agrupamento de raízes | `cargo test` |
| Rust, integração (`tests/`) | Mount real em diretório temporário: leitura, escrita, criação, exclusão, renomeação (todas as linhas da §4), alias de diretório por grafia diferente, `update` com invalidação, `kill -9` + limpeza, passthrough ligado e desligado | Container com `/dev/fuse` |
| Rust, protocolo | `mo2-vfs` real com cliente de teste: todas as mensagens e erros da §5 | Container com `/dev/fuse` |
| C++ (`organizer_tests`) | `LinuxVfsConnector` com o `mo2-vfs` real; `fileMapping()` (pendência da Fase 1) | Container com `/dev/fuse` |
| Ponta a ponta (`tests/vfs-e2e/`) | Instância de teste; o MO2 executa um script que lista e lê a `Data` virtual e cria um arquivo; confere o arquivo em `overwrite/` e a pasta original intacta | Container com `/dev/fuse`, no CI |

**CI:** o `docker run` ganha `--device /dev/fuse --cap-add SYS_ADMIN` (e `--security-opt apparmor:unconfined` se o runner exigir); o `Containerfile` ganha o pacote `fuse3`. Os testes que exigem FUSE são pulados com aviso quando `/dev/fuse` não existe (e falham no CI, onde a variável `MO2_REQUIRE_FUSE=1` está definida).

**Desempenho** (instância de benchmark de 2.000 mods do `benchmarks/gen_instance.py`; resultados em `benchmarks/results/2026-10-phase2-vfs.md`):

| Medida | Meta |
|---|---|
| `mount` até `ready` | < 1 s |
| `readdir` recursivo da `Data` virtual, cache quente | ≤ 2× a mesma árvore numa pasta nativa |
| `open`+`read`+`close` de 10.000 arquivos pequenos, cache quente | ≤ 2× nativo |
| `update` com a mesma lista | < 1 s |

## 9. Pendências herdadas da Fase 1

Entram no plano como tarefas separadas, no fim:

1. Teste direto de `OrganizerCore::fileMapping()` (coberto pela §8, C++).
2. Varredura de pastas (`envfs_linux.cpp`): tratar `EINTR`, `fstat` da raiz, e não duplicar entradas por links simbólicos.
3. Consolidar duplicações: conversão FILETIME↔Unix (4 cópias), leitor de INI (app e `game_bethesda`), mecanismos de include do `uibase`.
4. Guard no CI contra `read/skip<u?int64_t>` nos saves (lê 4 bytes no Linux).
5. Smoke test até a janela principal: instância pronta, `--disable-gpu`, `TMPDIR` privado, hermético e sem rodar duas vezes no CI.

## 10. Critérios de pronto

1. Todos os testes da §8 passam no CI Linux; o CI Windows continua verde.
2. No Bazzite, "Executar" num programa Linux (ex.: um gerenciador de arquivos) mostra a `Data` com os mods; o que ele cria vai para `overwrite/`; a pasta do jogo continua intacta depois.
3. Metas de desempenho da §8 registradas (atingidas ou com a diferença explicada).
4. Pendências da §9 resolvidas.
5. `ROADMAP.md`, `STATE.md` e a spec geral (§3.1) atualizados com a decisão do processo auxiliar e o resultado do spike.

## 11. Riscos

| Risco | Mitigação |
|---|---|
| Alias de diretório no kernel com lookup sem diferenciar maiúsculas/minúsculas | Teste de integração específico; se necessário, invalidar a entrada antiga (`notify_inval_entry`) ao resolver por outra grafia, como o `ciopfs`. |
| `fuser` sem suporte estável a passthrough | Passthrough atrás de opção; `pread` como padrão se a versão disponível não suportar. |
| FUSE indisponível no runner do CI | Teste antecipado na primeira tarefa do plano; alternativa: runner com `--privileged`. |
| Programas que mantêm arquivos abertos impedem desmontar | Desmonte preguiçoso (§5.4). |
| Cache de diretórios do Wine após `update` | Fora desta fase (Fase 3); o spike confirmou o restante do caminho do Wine. |
