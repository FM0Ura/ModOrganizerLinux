# Fase 4b — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-18-phase4b-loot-design.md`](../superpowers/specs/2026-10-18-phase4b-loot-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 compilado na distrobox `mo2-run` (`./build.sh`, resultado em `install/`) e iniciado **pelo box**: `distrobox enter mo2-run -- <repo>/install/bin/ModOrganizer`. Instância de Skyrim SE das fases anteriores, com os mods da 4a (plugins `.esp`/`.esl` ativos) e o SKSE.

**Antes de começar:** anote se `~/.var/app/io.github.loot.loot/data/LOOT` existe (LOOT Flatpak) e qual é a ordem atual da aba de plugins.

| # | Passo | Esperado | Resultado |
|---|---|---|---|
| 1 | Aba de plugins → "Ordenar". | O diálogo do LOOT mostra o progresso (atualizar masterlist, ler plugins, ordenar) e termina com as mensagens do relatório. A masterlist fica em `~/.var/app/io.github.loot.loot/data/LOOT/games/Skyrim Special Edition/` (com o Flatpak) ou em `~/.local/share/LOOT/games/Skyrim Special Edition/`. | |
| 1b | No host: `ls <instância>/logs/`. | `lootcli.log` (linhas `[progress]`/`[info]`), `lootreport.json`, `loot-loadorder.txt`. | |
| 2 | Fechar o diálogo. Conferir a aba de plugins; rodar o jogo pelo SKSE. | A ordem nova aparece na aba; o jogo abre e carrega o save normalmente. | |
| 3 | Só com o LOOT Flatpak: criar no LOOT uma regra de userlist (ex.: um plugin "load after" outro que hoje vem depois), fechar o LOOT, "Ordenar" de novo no MO2. | A ordem respeita a regra. | |
| 4 | "Ordenar" e, durante o progresso, "Cancelar". No host: `<repo>/install/bin/mo2-vfs --list-mounts`. | O diálogo fecha cancelado; a ordem da aba não mudou; nada listado. | |
| 5 | Desligar a rede (Wi-Fi/cabo). Fechar e reabrir o MO2; "Ordenar". | Erro claro `Error downloading masterlist: …`; a masterlist anterior continua na pasta do item 1. | |
| 5b | Ainda sem rede: Configurações → Workarounds → Network → Offline Mode ligado; "Ordenar". Religar a rede e desligar o modo offline ao fim. | Ordena com a masterlist em cache, sem erro. | |

**Ao terminar:** registre aqui o resultado de cada item e, em "Observações", o que precisou de correção.

## Observações
