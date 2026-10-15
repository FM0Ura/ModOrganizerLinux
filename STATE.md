# Estado do projeto

> Atualize este arquivo ao fim de cada tarefa, checkpoint ou decisão. O plano de cada fase continua sendo a fonte dos detalhes; aqui fica só o "onde estamos".

**Última atualização:** 2026-10-15
**Fase atual:** 2 — VFS FUSE · **Status:** 🚧 iniciada · **Anterior:** Fase 1 — Build nativo ✅
**Branch de trabalho:** `fase-2-vfs-fuse` (Fase 1 em `claude/serene-maxwell-1dyrbg`)

## Repositório

Tudo fica em [`FM0Ura/ModOrganizerLinux`](https://github.com/FM0Ura/ModOrganizerLinux): build, CI, testes, docs, o app em `deps/modorganizer` e as demais dependências em `deps/`. O fork [`FM0Ura/modorganizer`](https://github.com/FM0Ura/modorganizer) (`bb46b18`) foi incorporado em 2026-10-15 e não recebe mais mudanças.

## Fase 1 — progresso

| # | Tarefa | Status |
|---|---|---|
| 1 | Dependências em `deps/` via subtree | ✅ |
| 2 | Container, `build.sh`, CI Linux | ✅ |
| 3 | `cmake_common` com GCC | ✅ |
| 4 | 7-Zip 26.01 como `7z.so` | ✅ |
| 5 | `archive` no Linux | ✅ |
| 6 | `uibase` no Linux | ✅ |
| 7 | `bsatk`, `esptk`, `lootcli`, `dds-header` | ✅ |
| 7b | Dependências refixadas no `master` do upstream | ✅ |
| 8 | CMake Linux do app + CI Windows | ✅ |
| 9 | Varredura de pastas nativa + árvore de dados | ✅ |
| 10 | Processos, shell, atalhos `.desktop`, métricas | ✅ |
| 11 | `VfsConnector` + execução desativada | ✅ |
| 12 | Compilação completa + smoke test | ✅ |
| 13 | Plugins `game_bethesda` + instaladores | ✅ |
| 14 | Benchmark de refresh (linha de base) | ✅ |
| 15 | Checklist manual + README | ✅ |
| — | Revisão final do branch + correções | ✅ |

### Critérios de pronto (spec §4.8)

| Critério | Status |
|---|---|
| 1. `build.sh` gera o app a partir de container limpo | ✅ (CI Linux) |
| 2. Checklist manual aprovado no Bazzite | ✅ aprovado pelo usuário em 2026-10-15 |
| 3. CI Linux e Windows verdes | ✅ Linux [#27](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/38806338290)/[#28](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/38807459345); Windows [#15](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/38810512689) (fork `bb46b18` contra deps do upstream — ver pendência dos `deps/`) |
| 4. Linha de base de desempenho registrada | ✅ [`benchmarks/results/2026-10-phase1-baseline.md`](benchmarks/results/2026-10-phase1-baseline.md) |
| 5. Nenhum `windows.h` fora de código só-Windows | ✅ |

## Pacote para o teste manual

Artefato `mo2-linux-install` da [execução Linux #28](https://github.com/FM0Ura/ModOrganizerLinux/actions/runs/38807459345) (passo a passo no README, seção Bazzite; checklist em `docs/testing/phase1-manual-checklist.md`).

## Próximos passos

1. ✅ Decidido (2026-10-15): seguir para a Fase 2 (VFS FUSE), na branch `fase-2-vfs-fuse`.
2. Escrever a spec e o plano da Fase 2.
3. Resolver a pendência de compilar os `deps/` com nossos patches no Windows (ver abaixo).

## Limitações conhecidas da Fase 1

- Sem VFS: mods não ficam visíveis para o jogo (Fase 2).
- "Executar" mostra aviso; jogos e ferramentas não iniciam (Fase 3).
- Sem LOOT, sem criação de BSA, sem atualização automática, sem plugins Python, sem `nxm://` (Fase 4/5).
- Logins do Nexus/Steam ficam só em memória (perdidos ao fechar) até haver integração com keyring.
- "My Games"/"AppData" apontam para pastas XDG (Fase 3 leva para o prefixo do Proton).
- Lixeira só funciona no mesmo sistema de arquivos da home; em outro disco a remoção falha sem apagar nada.
- Roda dentro de uma distrobox Fedora 44 (AppImage na Fase 5).

## Decisões tomadas

| Data | Decisão |
|---|---|
| 2026-10-14 | Ferramenta pública; código multiplataforma (Windows continua compilando) |
| 2026-10-14 | VFS = FUSE próprio em Rust; o restante do MO2 continua em C++ |
| 2026-10-14 | Jogos iniciados direto via Proton (`umu-run`); modificar arquivo de mod altera o original (como USVFS) |
| 2026-10-14 | Desktop Linux primeiro (Bazzite); Steam Deck depois; distribuição em AppImage |
| 2026-10-14 | Dependências do upstream em `deps/` via `git subtree`, acompanhando o `master` do upstream |
| 2026-10-15 | CI só em pontos de controle; CI Windows roda em commits com `[windows-ci]` ou manualmente |
| 2026-10-15 | Fase 1 concluída: teste manual aprovado no Bazzite |
| 2026-10-15 | Um repositório só: o fork `modorganizer` vira o subtree `deps/modorganizer` (commits do fork reaplicados) |

## Pendências por fase

Itens menores encontrados nas revisões da Fase 1 e adiados (lista completa no ledger da Fase 1).

**Próximo checkpoint (início da Fase 2)**
- Compilar no Windows as dependências com os nossos patches (`deps/`); hoje o CI Windows usa as dependências do upstream e nossos patches só foram conferidos por análise (`unifdef`).

**Fase 2 (VFS)**
- `fileMapping()` sem teste direto (a Fase 2 o consome).
- Varredura de pastas: tratar `EINTR`, `fstat` da raiz e links simbólicos duplicando entradas.
- Consolidar duplicações: conversão FILETIME↔Unix (4 cópias), leitor de INI (app e `game_bethesda`), mecanismos de include do `uibase`.
- Guard em CI contra `read/skip<u?int64_t>` nos saves (lê 4 bytes no Linux).
- Smoke test até a janela principal (instância pronta, `--disable-gpu`, `TMPDIR` privado), hermético e sem rodar duas vezes no CI.

**Fase 3 (Steam/Proton)**
- Identificar processos do Wine/Proton por `/proc/<pid>/cmdline` (aparecem como `wine64-preloader`).
- Espera de processos: evitar laço em processos zumbis; `AsyncPipe` (fds).
- `UntouchedCommandLineArguments` ainda usa aspas no estilo Windows.
- Extração temporária de FOMOD/readme em `/tmp` compartilhado: usar `QTemporaryDir` por processo e rejeitar `..` nos caminhos do arquivo compactado.
- Curingas (`*`, `?`) em `shellDelete` só quando o caminho literal não existir.

**Fase 4 (Ecossistema)**
- `plugin_python`: `cmake_common` exige Python 3.13 exato; ramo `pylupdate6.exe`.
- Credenciais via keyring; mensagem amigável quando o MO2 foi aberto como root antes (`/tmp/qipc_*`).
- Lixeira por ponto de montagem (`.Trash-$uid`).
- Escrita de INI atômica e respeitando a codificação do arquivo; leitor de INI tratando seções repetidas como o Windows.
- Texto do aviso da Fase 3 em inglês no código-fonte (traduções).
- Reportar ao upstream os bugs do `bsatk` (extração duplicada, `writeData` invertido, `%s` com path).

**Fase 5 (Distribuição)**
- Nota de proveniência/licença do `dxgiformat.h`; `7z.so` com libstdc++ dinâmica; separar símbolos de depuração (binário de ~345 MB).
- Cache da imagem do container no CI; avisos do Node 20 nas actions.
