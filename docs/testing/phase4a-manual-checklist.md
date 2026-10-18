# Fase 4a — checklist manual (Bazzite)

Spec: [`../superpowers/specs/2026-10-18-phase4a-nexus-nxm-keyring-design.md`](../superpowers/specs/2026-10-18-phase4a-nexus-nxm-keyring-design.md)

**Ambiente:** Bazzite (KDE Plasma 6), MO2 compilado na distrobox `mo2-run` (`./build.sh`, resultado em `install/`) e iniciado **pelo box**: `distrobox enter mo2-run -- <repo>/install/bin/ModOrganizer`. Firefox (Flatpak) logado no Nexus. Instância de Skyrim SE das fases anteriores.

**Antes de começar:** no host, `xdg-mime query default x-scheme-handler/nxm` (anote; esperado: vazio) e, no KWallet Manager, nenhuma entrada `Mod Organizer 2: …`.

| # | Passo | Esperado | Resultado |
|---|---|---|---|
| 1 | Configurações → Nexus → conectar (OAuth). Autorizar no navegador. | O navegador do host abre; depois de autorizar, o MO2 mostra a conta. | |
| 1b | Fechar e reabrir o MO2. | Continua conectado, sem pedir login. No KWallet Manager, entrada `Mod Organizer 2: ModOrganizer2_NEXUS_OAUTH_TOKENS`. | |
| 2 | Desconectar do Nexus nas configurações. | A entrada some do KWallet. | |
| 2b | Conectar por API key (colar a chave). Fechar e reabrir. | Continua conectado; entrada `…_APIKEY` no KWallet. | |
| 3 | No host: `xdg-mime query default x-scheme-handler/nxm`; `cat ~/.local/share/applications/org.modorganizer.ModOrganizer2.nxm.desktop`. | `org.modorganizer.ModOrganizer2.nxm.desktop`; `Exec=distrobox enter mo2-run -- <caminho>/ModOrganizer %u`. | |
| 4 | Com o MO2 aberto: no site do Nexus, "Mod Manager Download" de um mod pequeno de Skyrim SE. | O Firefox pode perguntar com qual programa abrir (aceite e lembre); o download aparece na aba Downloads e termina. | |
| 4b | Fechar o MO2 e repetir com outro mod. | O MO2 abre na instância e baixa. | |
| 5 | Instalar o mod do item 4 e um mod FOMOD baixado do mesmo jeito; ativar. | Instalação normal (FOMOD com o assistente); arquivos na aba Data. | |
| 6 | No host: `xdg-mime default org.mozilla.firefox.desktop x-scheme-handler/nxm` (simula outro handler; o Firefox é o Flatpak, e o xdg-utils ignora um padrão cujo `.desktop` não existe). Reabrir o MO2. Depois, Configurações → Nexus → `Associate with "Download with manager" links`. | Reabrir não troca (`xdg-mime query …` = `org.mozilla.firefox.desktop`); o botão mostra um diálogo ("Mod Organizer now handles nxm:// links.") e o query volta a `org.modorganizer.ModOrganizer2.nxm.desktop`. | |
| 7 | Com o MO2 fechado: `distrobox enter mo2-run -- env DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent <repo>/install/bin/ModOrganizer`; conectar por API key. | Aviso "Login not saved" uma vez; conectado durante a sessão; ao reabrir normalmente, a chave do item 2b continua no KWallet (nada foi apagado). | |

**Ao terminar:** registre aqui o resultado de cada item e, em "Observações", o que precisou de correção.

## Observações
