# Roadmap — Mod Organizer 2 nativo no Linux

Objetivo: MO2 nativo no Linux, com todas as funcionalidades da versão Windows, gerenciando mods de jogos da Steam executados via Proton. Plataforma de referência: Bazzite.

- Design completo: [`docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md`](docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md)
- Estado atual e pendências: [`STATE.md`](STATE.md)

Cada fase passa por: especificação → plano → implementação com revisão por tarefa → revisão final → checkpoint de CI (Linux + Windows) → teste manual no Bazzite.

## Visão geral

| Fase | Nome | Entrega | Pronto quando | Status |
|---|---|---|---|---|
| 1 | Build nativo | MO2 compila e abre no Linux; plugins Bethesda + instaladores; sem VFS | Gerencia instâncias, perfis e mods no Bazzite; build Windows continua passando | ✅ Concluída (2026-10-15) |
| 2 | VFS FUSE | Processo auxiliar Rust `mo2-vfs` + integração no `VfsConnector` | Testes de integração passando; "Executar" de um programa Linux nativo mostra a visão virtual montada | ✅ Concluída (2026-10-17) |
| 3 | Steam/Proton | Descoberta de jogos e prefixos; execução de `.exe` via Proton com VFS montado | Skyrim SE + SKSE rodando com mods | ✅ Concluída (2026-10-18) |
| 4a | Nexus + `nxm://` + keyring | Login do Nexus lembrado (Secret Service); MO2 registra-se como handler de `nxm://`; downloads pelo site do Nexus | Baixar e instalar mod do Nexus por `nxm://`, inclusive FOMOD (critério 3) | 🟢 Teste manual aprovado (2026-10-18); falta o checkpoint de CI e o merge |
| 4b | LOOT | `lootcli` nativo com libloot, masterlist, `plugins.txt` no prefixo | Ordenação com LOOT (critério 4) | ⚪ Não iniciada |
| 4c | Ferramentas pelo VFS e plugins C++ | BodySlide, Nemesis/Pandora, xEdit de outros jogos; BAIN, previews, checagens; pendências adiadas da Fase 2 | Ferramentas pelo VFS (critério 6) | ⚪ Não iniciada |
| 4d | Plugins Python | `plugin_python` (pybind11 + Python + PyQt6) e plugins Python (incl. `basic_games`) | Todas as funcionalidades | ⚪ Não iniciada |
| 4e | BSA/BA2 | Criação de BSA/BA2 nativa | Todas as funcionalidades | ⚪ Não iniciada |
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

Spec: [`docs/superpowers/specs/2026-10-17-phase3-steam-proton-design.md`](docs/superpowers/specs/2026-10-17-phase3-steam-proton-design.md) · Plano: [`docs/superpowers/plans/2026-10-17-phase3-steam-proton.md`](docs/superpowers/plans/2026-10-17-phase3-steam-proton.md) · Checklist manual: [`docs/testing/phase3-manual-checklist.md`](docs/testing/phase3-manual-checklist.md)

Inicia jogos e ferramentas Windows com os mods ativos, via Proton.

- Módulo de descoberta da Steam no `uibase` (`steam::`): parser de VDF, `libraryfolders.vdf`, `appmanifest_*.acf`, `config.vdf` (Proton por jogo, pela instalação da Steam dona do jogo), `compatdata/<appid>`, Protons instalados (`steamapps/common` e `compatibilitytools.d`); Steam nativa, Flatpak e bibliotecas em outros discos.
- "My Games", saves e AppData dos plugins de jogo (`game_bethesda`) apontam para dentro do prefixo do Proton; sem prefixo, voltam às pastas XDG.
- **O VFS e os programas rodam no host**, mesmo com a interface do MO2 no distrobox `mo2-run`: o `LinuxVfsConnector` inicia o `mo2-vfs` por `distrobox-host-exec` (`MO2_VFS_BRIDGE=auto|direct|host`). Programas que só existem dentro do container deixam de poder ser executados pelo MO2.
- O `mo2-vfs` inicia o programa (pedido `spawn`, evento `exited`) e o observa; para programas Windows, o `ProtonLauncher` monta `umu-run <exe>` com `WINEPREFIX`, `GAMEID`, `SteamAppId`, `PROTONPATH` etc. `umu-run` é obrigatório (sem fallback para `proton run`).
- Desmontagem: quando todos os grupos de processos terminam **e** o `wineserver` do prefixo (achado pelo `cwd` em `/tmp/.wine-<uid>/server-<dev>-<ino>`) saiu; cobre programas que se relançam, como o `skse64_loader.exe`.
- Proton e variáveis de ambiente configuráveis por executável (grupo "Proton" do diálogo de executáveis); argumentos divididos como a linha de comando do Windows.
- Escopo testado: Steam nativa e Skyrim SE no Bazzite; Flatpak e outros discos com fixtures; ferramenta validada: SSEEdit. xEdit de outros jogos, BodySlide e Nemesis/Pandora ficam para a Fase 4. Jogo e ferramenta ao mesmo tempo no mesmo prefixo não são suportados.
- Resultado dos spikes (2026-10-17, spec §1.2): `umu-run` no host vê o VFS do host; dentro do container o Wine só vê o VFS pelo mesmo caminho do mount (`/home` × `/var/home` são mounts separados lá), por isso VFS e jogo no host; os processos do Proton saem do grupo observado, mas o `umu-run` só sai depois do `wineserver` (`waitforexitandrun`); o `cwd` do `wineserver` identifica o prefixo; `distrobox-host-exec` serve de ponte para o protocolo JSON. O SKSE real foi verificado no ponto de controle da Task 3 (2026-10-18, `mo2-vfs` rodando direto no host): o `skse64_loader.exe` iniciou o `SkyrimSE.exe`, o SKSE carregou pelo `Data` montado e respondeu a `getskseversion`, e o mount caiu 0,09 s depois do `umu-run` sair, com o jogo já fechado; o grupo do `umu-run` cobriu o relançamento do loader, e a espera do `wineserver` ficou como rede de segurança.
- Meta: Skyrim SE + SKSE rodando com mods no Bazzite — **teste manual aprovado em 2026-10-18** (MO2 no box, ponte `distrobox-host-exec`, Skyrim SE + SKSE e SSEEdit); ✅ com o CI verde e o merge no `main` (PR #1, 2026-10-18).
- Pendências herdadas resolvidas e o que sobrou: ver [`STATE.md` → Pendências por fase](STATE.md#pendências-por-fase).

## Fase 4 — Ecossistema

Dividida em subfases (spec da 4a, §1.1).

Fase 4a — Spec: [`docs/superpowers/specs/2026-10-18-phase4a-nexus-nxm-keyring-design.md`](docs/superpowers/specs/2026-10-18-phase4a-nexus-nxm-keyring-design.md) · Plano: [`docs/superpowers/plans/2026-10-18-phase4a-nexus-nxm-keyring.md`](docs/superpowers/plans/2026-10-18-phase4a-nexus-nxm-keyring.md) · Checklist manual: [`docs/testing/phase4a-manual-checklist.md`](docs/testing/phase4a-manual-checklist.md)

- (4d) `plugin_python` (pybind11 + Python embutido) e os plugins Python da comunidade.
- (4b) `lootcli` nativo (libloot) → ordenação com LOOT.
- (4a) Handler `nxm://` (`.desktop` + `xdg-mime`) → downloads pelo site do Nexus.
- (4c) Demais plugins (BAIN, previews, ferramentas); detecção completa de jogos via `steam/`.
- (4e) Criação de BSA/BA2 com alternativa nativa ao `libbsarch`.
- (4a) Armazenamento seguro de credenciais (Nexus/Steam) via Secret Service/keyring.

## Fase 5 — Distribuição

- AppImage (`linuxdeploy` + plugin Qt) com 7-Zip, `lootcli` e Python embutidos.
- CI de release, separação de símbolos de depuração, documentação de usuário.
- Atualização automática.

## Fora de escopo (por enquanto)

- Steam Deck / modo Gaming.
- Jogos fora da Steam (GOG/Epic via Heroic/Lutris).
- Flatpak.
