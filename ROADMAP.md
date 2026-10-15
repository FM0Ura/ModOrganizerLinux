# Roadmap — Mod Organizer 2 nativo no Linux

Objetivo: MO2 nativo no Linux, com todas as funcionalidades da versão Windows, gerenciando mods de jogos da Steam executados via Proton. Plataforma de referência: Bazzite.

- Design completo: [`docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md`](docs/superpowers/specs/2026-10-14-mo2-linux-port-design.md)
- Estado atual e pendências: [`STATE.md`](STATE.md)

Cada fase passa por: especificação → plano → implementação com revisão por tarefa → revisão final → checkpoint de CI (Linux + Windows) → teste manual no Bazzite.

## Visão geral

| Fase | Nome | Entrega | Pronto quando | Status |
|---|---|---|---|---|
| 1 | Build nativo | MO2 compila e abre no Linux; plugins Bethesda + instaladores; sem VFS | Gerencia instâncias, perfis e mods no Bazzite; build Windows continua passando | 🟡 Implementada — aguardando teste manual |
| 2 | VFS FUSE | Crate Rust `mo2-vfs` + integração no `VfsConnector` | Testes de integração passando; MO2 monta a visão virtual em diretório de teste | ⚪ Não iniciada |
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

Substitui o USVFS. O MO2 monta, via FUSE, a visão mesclada dos mods sobre as pastas do jogo enquanto o programa roda.

- Entrada: a `MappingType` já gerada por `OrganizerCore::fileMapping` (hoje guardada pelo `NullVfsConnector`).
- Ponte C++ ↔ Rust por API C (`cxx`/`cbindgen`), crate `fuser`, build integrado pelo Corrosion.
- Um mount por destino (`<jogo>/Data`, `My Games/<Jogo>`, `AppData/Local/<Jogo>`), com a pasta original como camada de menor prioridade.
- Busca sem diferenciar maiúsculas/minúsculas; leitura repassada (FUSE passthrough no kernel ≥ 6.9); arquivos novos em `overwrite/`; modificação altera o arquivo do mod (como o USVFS).
- Ciclo de vida: monta antes de executar, desmonta ao fim do grupo de processos, limpeza após falha (`fusermount3 -u`).
- Testes: árvore em Rust puro, integração montando em diretório temporário, benchmarks `readdir`/`open` com 2.000 mods.
- Pendências herdadas da Fase 1: ver [`STATE.md` → Pendências por fase](STATE.md#pendências-por-fase).

## Fase 3 — Steam/Proton

Inicia jogos e ferramentas com os mods ativos.

- Módulo `steam/`: `libraryfolders.vdf`, `appmanifest_*.acf`, `config.vdf` (Proton por jogo), `compatdata/<appid>`; Steam nativa, Flatpak e bibliotecas em outros discos.
- Pastas "Meus Documentos"/"AppData" dos plugins de jogo passam a apontar para dentro do prefixo do Proton.
- Execução: monta VFS → `umu-run` (fallback `proton run`) com as variáveis da Steam → acompanha os processos (`pidfd`, `wineserver -w`) → desmonta.
- Proton, variáveis de ambiente e argumentos configuráveis por executável; ferramentas (xEdit, BodySlide, Nemesis/Pandora) no prefixo do jogo.
- Riscos a validar logo no início: visibilidade do mount FUSE dentro do `pressure-vessel`; cache de diretórios do Wine.
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
