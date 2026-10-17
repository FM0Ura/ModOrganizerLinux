# Roadmap — Mod Organizer 2 nativo no Linux

Objetivo: MO2 nativo no Linux, com todas as funcionalidades da versão Windows, gerenciando mods de jogos da Steam executados via Proton. Plataforma de referência: Bazzite.

- Design completo: [`docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md`](docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md)
- Estado atual e pendências: [`STATE.md`](STATE.md)

Cada fase passa por: especificação → plano → implementação com revisão por tarefa → revisão final → checkpoint de CI (Linux + Windows) → teste manual no Bazzite.

## Visão geral

| Fase | Nome | Entrega | Pronto quando | Status |
|---|---|---|---|---|
| 1 | Build nativo | MO2 compila e abre no Linux; plugins Bethesda + instaladores; sem VFS | Gerencia instâncias, perfis e mods no Bazzite; build Windows continua passando | ✅ Concluída (2026-10-15) |
| 2 | VFS FUSE | Processo auxiliar Rust `mo2-vfs` + integração no `VfsConnector` | Testes de integração passando; "Executar" de um programa Linux nativo mostra a visão virtual montada | ✅ Implementada — aguardando teste manual |
| 3 | Steam/Proton | Descoberta de jogos e prefixos; execução de `.exe` via Proton com VFS montado | Skyrim SE + SKSE rodando com mods | ⚪ Não iniciada |
| 4 | Ecossistema | Plugins Python, LOOT, handler `nxm://`, demais plugins e ferramentas | Critério de sucesso do projeto atendido (exceto AppImage) | ⚪ Não iniciada |
| 5 | Distribuição | AppImage, CI de release, documentação de usuário | Instalação em um clique no Bazzite | ⚪ Não iniciada |

## Fase 1 — Build nativo

Plano: [`docs/superpowers/plans/2026-10-14-phase1-native-build.md`](docs/superpowers/plans/2026-10-14-phase1-native-build.md) · Checklist manual: [`docs/testing/phase1-manual-checklist.md`](docs/testing/phase1-manual-checklist.md)

- Superprojeto com dependências do upstream em `deps/` (`git subtree`, acompanhando o `master`), container Fedora 44, `build.sh`, CI Linux e CI Windows (sob demanda, `[windows-ci]`).
- Bibliotecas portadas: `cmake_common`, 7-Zip 26.01, `archive`, `uibase`, `bsatk`, `esptk`, `lootcli` (header), `dds-header`.
- App: camada `env` Linux (`src/linux/`), árvore de dados nativa, `VfsConnector` (nulo no Linux), execução desativada com aviso, smoke test até a seleção de instância.
- Plugins: `game_bethesda` (16 jogos), `installer_quick`, `installer_manual`, `installer_fomod`.
- Linha de base de desempenho: refresh de 2.000 mods ≈ 1,6 s (cache quente); abertura até a janela principal ≈ 1,2 s.

## Fase 2 — VFS FUSE (`mo2-vfs`, Rust)

Spec: [`docs/superpowers/specs/2026-10-15-phase2-vfs-fuse-design.md`](docs/superpowers/specs/2026-10-15-phase2-vfs-fuse-design.md) · Plano: [`docs/superpowers/plans/2026-10-15-phase2-vfs-fuse.md`](docs/superpowers/plans/2026-10-15-phase2-vfs-fuse.md) · Checklist manual: [`docs/testing/phase2-manual-checklist.md`](docs/testing/phase2-manual-checklist.md) · Benchmarks: [`benchmarks/results/2026-10-phase2-vfs.md`](benchmarks/results/2026-10-phase2-vfs.md)

Substitui o USVFS. O MO2 monta, via FUSE, a visão mesclada dos mods sobre as pastas do jogo enquanto o programa roda.

- Entrada: a `MappingType` já gerada por `OrganizerCore::fileMapping` (extraída e coberta por testes).
- `mo2-vfs` é um **processo auxiliar** (crate `fuser`), iniciado pelo `LinuxVfsConnector`; conversa com o MO2 por JSON em stdin/stdout. Sem Corrosion, `cxx` nem `cbindgen`: o CMake do app não compila Rust (o binário é achado ao lado do executável ou em `MO2_VFS_BINARY`).
- Um mount por destino, com a pasta original como camada de menor prioridade; busca sem diferenciar maiúsculas/minúsculas.
- Arquivos novos vão para `overwrite/`; editar um arquivo de mod altera o arquivo do mod (como o USVFS). **Originais protegidos:** arquivos da pasta do jogo nunca são apagados nem renomeados; excluir só esconde até desmontar e renomear copia para a camada de criação.
- "Executar" funciona para **programas Linux nativos** (demonstração da fase); `.exe` continua com o aviso da Fase 3.
- Ciclo de vida: monta só ao executar e desmonta sozinho quando o último grupo de processos observado termina; limpeza de mounts órfãos ao abrir o MO2 (arquivo de estado + `fusermount3 -u`). O refresh de mods fica adiado enquanto há mount ativo e roda ao desmontar.
- Atualização a quente (`update`) com troca atômica da árvore e invalidação do cache do kernel.
- Testes: árvore em Rust puro, integração montando em diretório temporário, protocolo, testes C++ do conector, execução ponta a ponta de um programa nativo, smoke test até a janela principal.
- Desempenho (2.000 mods, 200 mil arquivos): mount 916 ms, update 906 ms, `readdir` 0,84x do nativo, `open`+`read` 3,36x (meta de 2x não atingida; causa medida, sem passthrough sem privilégio).
- Pendências herdadas da Fase 1 resolvidas; o que sobrou: ver [`STATE.md` → Pendências por fase](STATE.md#pendências-por-fase).

## Fase 3 — Steam/Proton

Inicia jogos e ferramentas com os mods ativos.

- Módulo `steam/`: `libraryfolders.vdf`, `appmanifest_*.acf`, `config.vdf` (Proton por jogo), `compatdata/<appid>`; Steam nativa, Flatpak e bibliotecas em outros discos.
- Pastas "Meus Documentos"/"AppData" dos plugins de jogo passam a apontar para dentro do prefixo do Proton.
- Execução: monta VFS (no host, não no distrobox) → `umu-run` (fallback `proton run`) com as variáveis da Steam → acompanha os processos (`pidfd`, `wineserver -w`) → desmonta.
- Proton, variáveis de ambiente e argumentos configuráveis por executável; ferramentas (xEdit, BodySlide, Nemesis/Pandora) no prefixo do jogo.
- Risco do `pressure-vessel` resolvido no spike de 2026-10-15 (o mount do host é visível lá dentro). Riscos da fase: processos do Wine que saem do grupo observado (`setsid`/`wineserver`), cache de diretórios do Wine após `update`, mounts feitos dentro do distrobox `mo2-run` invisíveis para a Steam do host (rodar o `mo2-vfs` no host ou antecipar o AppImage) e passthrough que exige `CAP_SYS_ADMIN`.
- Meta: Skyrim SE + SKSE rodando com mods no Bazzite.

## Fase 4 — Ecossistema

- `plugin_python` (pybind11 + Python embutido) e os plugins Python da comunidade.
- `lootcli` nativo (libloot) → ordenação com LOOT.
- Handler `nxm://` (`.desktop` + `xdg-mime`) → downloads pelo site do Nexus.
- Demais plugins (BAIN, previews, ferramentas); detecção completa de jogos via `steam/`.
- Criação de BSA/BA2 com alternativa nativa ao `libbsarch`.
- Armazenamento seguro de credenciais (Nexus/Steam) via Secret Service/keyring.

## Fase 5 — Distribuição

- AppImage (`linuxdeploy` + plugin Qt) com 7-Zip, `lootcli` e Python embutidos.
- CI de release, separação de símbolos de depuração, documentação de usuário.
- Atualização automática.

## Fora de escopo (por enquanto)

- Steam Deck / modo Gaming.
- Jogos fora da Steam (GOG/Epic via Heroic/Lutris).
- Flatpak.
